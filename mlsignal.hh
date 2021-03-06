//Gravitational microlensing signal models
//
//Written by John G Baker NASA-GSFC (2014-2015)
#ifndef MLSIGNAL_HH
#define MLSIGNAL_HH
#include "glens.hh"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <valarray>
#include "bayesian.hh"

using namespace std;
extern bool debug_signal;


double approxerfinv(double x){
   double tt1, tt2, lnx, sgn;
   sgn = (x < 0) ? -1.0f : 1.0f;

   x = (1 - x)*(1 + x); 
   lnx = logf(x);

   tt1 = 2/(M_PI*0.147) + 0.5f * lnx;
   tt2 = 1/(0.147) * lnx;

   return(sgn*sqrt(-tt1 + sqrt(tt1*tt1 - tt2)));
}
///Base class for ml_photometry_signal
///
///This object contains information about a photometric microlensing signal model
///The signal is constructed from a GLens object, together with a Trajectory
///There are several options for controlling/modifying the form of the parameters
///applied in constructing the model, with some rough motivations.
class ML_photometry_signal : public bayes_signal{
  Trajectory *traj;
  GLens *lens;
  int idx_I0, idx_Fs, idx_dtsm;
  stateSpace localSpace;
  shared_ptr<const sampleable_probability_function> localPrior;
  //vector<double>variances;
  //bool have_variances;
  bool smearing;
  int nsmear;
  double dtsmear_save;
  double smear_unk;
  bool vary_dtsm;
public:
  ML_photometry_signal(Trajectory *traj_,GLens *lens_):lens(lens_),traj(traj_){
    //have_variances=false;
    idx_I0=idx_Fs=-1;
    localPrior=nullptr;
    vary_dtsm=false;
  };
  ~ML_photometry_signal(){};
  //Produce the signal model
  vector<double> get_model_signal(const state &st, const vector<double> &times, vector<double> &variances)const override{
    //Caution!  Global/Member variables should not change or there will be problems with openmp
    //cout<<"enter get_model_signal"<<endl;
    checkWorkingStateSpace();
    double result=0;
    double I0,Fs;
    get_model_params(st,I0,Fs);
    double dtsmear;
    if(vary_dtsm) dtsmear=pow(10.0,st.get_param(idx_dtsm));
    else dtsmear=dtsmear_save;
    vector<double> xtimes,model,modelmags;
    vector<vector<Point> > thetas;
    vector<int> indices;

    //cout<<"cloning"<<endl;
    //We need to clone lens/traj before working with them so that each omp thread is working with different copies of the objects.
    GLens *worklens=lens->clone();
    worklens->setState(st);
    Trajectory *worktraj=traj->clone();
    worktraj->setState(st);

    //If specified, implement smearing across a small time band
    if(smearing){
      //cout<<"smearing"<<endl;
      //various tuning controls
      const int trimcountmax=3;      
      const double min_var_scale=1e-8;
      const double smear_trim_level=5, trim_level2=smear_trim_level*smear_trim_level;

      //prepare the xtimes array and mapping table
      int nt=times.size();
      vector<double>deltas(nsmear);
      //Define the grid of smearing points
      //cout<<"deltas:";
      for(int j=0;j<nsmear;j++){
	//In this case we weight points near the center, with normal density
	//ds/dx = norm(x) -> s = (erf(x)+1)/2 -> x = erfinv(2s-1)
	double s = (j+0.5)/nsmear;  
	deltas[j]=dtsmear*worktraj->tEinstein()*approxerfinv(2*s-1);
	//cout<<deltas[j]<<" ";	  
      }
      //cout<<endl;
      typedef pair< pair<int,int>,double > entry;
      vector< entry > table;
      for(int i=0;i<nt;i++)
	for(int j=0;j<nsmear;j++)
	  table.push_back(make_pair(make_pair(i,j),times[i]+deltas[j]));
      //cout<<"before: ";for(int i=0;i<20 and i<nsmear*nt;i++)cout<<table[i].second<<" ";cout<<endl;
      //cout<<"      : ";for(int i=0;i<20 and i<nsmear*nt;i++)cout<<"("<<table[i].first.first<<","<<table[i].first.second<<") ";cout<<endl;
      sort(table.begin(),table.end(),
	   [](entry left,entry right){return left.second<right.second;});
      //cout<<"after: ";for(int i=0;i<20 and i<nsmear*nt;i++)cout<<table[i].second<<" ";cout<<endl;
      //cout<<"      : ";for(int i=0;i<20 and i<nsmear*nt;i++)cout<<"("<<table[i].first.first<<","<<table[i].first.second<<") ";cout<<endl;
      xtimes.resize(nt*nsmear);
      for(int i=0;i<nt*nsmear;i++)xtimes[i]=table[i].second;

      //compute the magnifications
      worktraj->set_times(xtimes);
      variances.resize(0);
      worklens->compute_trajectory(*worktraj,xtimes,thetas,indices,modelmags,variances);

      //Variables for the averaging
      vector<double>sum(nt);
      vector<double>vsum(nt);
      vector<double>sum2(nt);
      vector< vector<double> >magsarray(nt,vector<double>(nsmear));
      vector< vector<double> >dmagsarray(nt,vector<double>(nsmear));
      
      //conduct averaging to get results for original time grid
      for(int i=0;i<nt*nsmear;i++){
	double val=modelmags[indices[i]];
	int idata=table[i].first.first;
	int ismear=table[i].first.second;
	sum[idata]+=val;
	sum2[idata]+=val*val;
	//sum[idata]+=val*weight[ismear];
	//sum2[idata]+=val*val*weight[ismear];
	magsarray[idata][ismear]=val;
      }
      if(variances.size()>0){
	for(int i=0;i<nt*nsmear;i++){
	  double val=variances[indices[i]];
	  int idata=table[i].first.first;
	  int ismear=table[i].first.second;
	  vsum[idata]+=val;
	  dmagsarray[idata][ismear]=val;
	}
      }
      modelmags.resize(nt);
      variances.resize(nt);

      //cout<<"vals/t,avg,var:"<<endl;
      for(int i=0;i<nt;i++){
	double avg = sum[i]/nsmear;
	double var = vsum[i]/nsmear + ( sum2[i] - nsmear*avg*avg)/(nsmear-1.0);
	if(smear_trim_level>0){
	  //We will recompute the average and variance after trimming back any values especially far from the smeared average.
	  //The trim level is expressed as some maximum number of sigma away from the mean.
	  double newavg=avg,newvar=var;
	  int jmax=nsmear;
	  bool done=false;
	  int trimcount=0;
	  while(not done){
	    done=true;
	    int jstop=jmax;
	    trimcount++;
	    if(trimcount>trimcountmax)jstop=0;//quit looping after trimcountmax cycles
	    for(int j=0;j<jstop;j++){
	      double val=magsarray[i][j];
	      double dev=(val-newavg)*nsmear/(nsmear-1.0);
	      double dev2=dev*dev;
	      double othersvar=(newvar-dev2/nsmear)*(nsmear-1.0)/(nsmear-2.0)+min_var_scale;
	      if(dev2>trim_level2*othersvar*(1.0+1.0/nsmear)){//outlier detected  (the final factor makes a little buffer)
		double newdev=dev*sqrt(trim_level2*othersvar/dev2);
		double newval=val+newdev-dev;
		magsarray[i][j]=newval;
		newavg=newavg+(newdev-dev)/nsmear;         //This is how the avg changes when we scale down dev
		newvar=newvar+(newdev*newdev-dev2)/nsmear; //This is how the var changes when we scale down dev
		#pragma omp_critical
		if(0)
		{
		  cout<<"Trimming outlier ["<<i<<","<<j<<"] dev: "<<dev<<" -> "<<newdev<<"    othersvar="<<othersvar<<endl;
		  cout<<"           val: "<<val<<" -> "<<newval<<endl;
		  cout<<"            avg,var: "<<avg<<","<<var<<" -> "<<newavg<<","<<newvar<<endl;
		}
		done=false;//need to go back to the start to look for an outliers relative to the new avg/var.
		jmax=j;
	      }
	    }
	  }
	  avg=newavg;
	  var=newvar;
	}
	modelmags[i]=avg;
	variances[i]=var;
	//for(int j=0;j<nsmear;j++)cout<<magsarray[i][j]<<" ";
	//cout<<"\n  "<<times[i]<<", "<<avg<<", "<<var<<endl;
      }

      //have_variances=true;
      bool burped=false;
      
      //Prepare the magnitude results
      //cout<<"t,Ival,var:"<<endl;
      for(int i=0;i<times.size();i++ ){
	double mu=modelmags[indices[i]];
	double Ival = I0 - 2.5*log10(Fs*mu+1-Fs);
	model.push_back(Ival);
	double fac=2.5/(mu-1+1/Fs)*smear_unk;
	variances[i]*=fac*fac;
	//cout<<times[i]<<", "<<Ival<<", "<<variances[i]<<endl;
	if(!isfinite(Ival)&&!burped){
	  cout<<"get_model_signal(smear): model infinite: modelmags="<<modelmags[indices[i]]<<" at state="<<st.show()<<endl;
	  burped=true;
	}
      }
    } else {//no smearing
      //cout<<"not smearing"<<endl;
      worktraj->set_times(times);
      vector<double>dmags;
      //cout<<"calling compute traj"<<endl;
      worklens->compute_trajectory(*worktraj,xtimes,thetas,indices,modelmags,dmags);
      //cout<<"prep variance"<<endl;
      variances.resize(times.size());
      bool burped=false;
      for(int i=0;i<times.size();i++ ){
	double mu=modelmags[indices[i]];
	double Ival = I0 - 2.5*log10(Fs*mu+1-Fs);
	model.push_back(Ival);
	if(!isfinite(Ival)&&!burped){
	  cout<<"get_model_signal: model infinite: modelmags="<<modelmags[indices[i]]<<" at t="<<times[i]<<" state="<<st.get_string()<<endl;
	  burped=true;
	}
	if(dmags.size()>0)variances[i]=dmags[indices[i]]*dmags[indices[i]];
	//cout<<i<<" var="<<variances[i]<<endl;
      }
    }
      
    delete worktraj;
    delete worklens;
    return model;
  };
  
  ///Get modeled variance in the signals from modeled stochastic signal features.
  /*
  virtual vector<double> getVariances(const state &st,const vector<double> times)override{
    if(smearing){
      if(have_variances){
	return variances;
      } else {
	get_model_signal(st, times);
	return variances;
      }
    } else return bayes_signal::getVariances(st,times);
    };*/
  
  ///From StateSpaceInterface (via bayes_signal)
  ///
  void defWorkingStateSpace(const stateSpace &sp)override{
    checkSetup();//Call this assert whenever we need options to have been processed.
    idx_I0=sp.requireIndex("I0");
    idx_Fs=sp.requireIndex("Fs");
    if(vary_dtsm)idx_dtsm=sp.requireIndex("log-dtsm");
    haveWorkingStateSpace();
    //cout<<"signal::defWSS: about to def lens"<<endl;
    lens->defWorkingStateSpace(sp);
    traj->defWorkingStateSpace(sp);
  };
  
  void addOptions(Options &opt,const string &prefix=""){
    Optioned::addOptions(opt,prefix);
    addOption("MLPsig_nsmear","Number of points in time smear the magnification model. (default: no smearing).","0");
    addOption("MLPsig_dtsmear","Time-width (tE units) over which to smear the magnification model or prior center if free parameter. (Default=0.001)","0.001");
    addOption("MLPsig_dtsm_range","Time-width log10-Gaussian prior width. (Default=-1,fixed)","-1");
    addOption("MLPsig_smear_unk","Uncertainty factor for time smearing.","0.1");
  };
  void setup(){
    haveSetup();
    double dtsmear_range;
    *optValue("MLPsig_nsmear")>>nsmear;
    *optValue("MLPsig_dtsmear")>>dtsmear_save;
    *optValue("MLPsig_dtsm_range")>>dtsmear_range;
    *optValue("MLPsig_smear_unk")>>smear_unk;
    nsmear=abs(nsmear);
    smearing=(nsmear>1);
    vary_dtsm = ( dtsmear_range>0 and smearing );
    if(vary_dtsm)cout<<"MLPhotometry_signal:Varying dtsmear"<<endl; 
    ///Set up the full output stateSpace for this object
    const int uni=mixed_dist_product::uniform, gauss=mixed_dist_product::gaussian, pol=mixed_dist_product::polar; 
    string                names[]=                        { "I0",   "Fs",     "log-dtsm"};
    valarray<double>    centers((initializer_list<double>){ 18.0,    0.5, log10(dtsmear_save)});
    valarray<double> halfwidths((initializer_list<double>){  5.0,    0.5,  dtsmear_range});
    valarray<int>         types((initializer_list<int>){   gauss,    uni,          gauss});
    //set the space
    stateSpace space(2);
    if(smearing and vary_dtsm){
      space=stateSpace(3);
    }
    space.set_names(names);  
    localSpace=space;
    nativeSpace=localSpace;
    nativeSpace.attach(*lens->getObjectStateSpace());
    nativeSpace.attach(*traj->getObjectStateSpace());
    //set the prior
    localPrior.reset(new mixed_dist_product(&localSpace,types,centers,halfwidths));
    setPrior(new independent_dist_product(&nativeSpace,localPrior.get(),lens->getObjectPrior().get(),traj->getObjectPrior().get()));
  };    

private:

  void get_model_params(const state &st, double &I0, double &Fs)const{
    checkWorkingStateSpace();//Call this assert whenever we need the parameter index mapping.
    //Light level parameters
    //  I0 baseline unmagnitized magnitude
    //  Fs fraction of I0 light from the magnetized source
    I0=st.get_param(idx_I0);
    Fs=st.get_param(idx_Fs);
  };

public:
  GLens *clone_lens()const{return lens->clone();};
  //Here we always make a square window, big enough to fit the trajectory (over the specified domain) and the lens window
  //Points referenced in this function refer to *lens frame* //consider shifting
  void getWindow(const state &s, Point &LLcorner,Point &URcorner, double tstart=0, double tend=0){//, int cent=-2){
    double I0,Fs;//,r0,tE,tmax;//2TRAJ: As in model_lightcurve
    //get_model_params(s.get_params_vector(),I0,Fs,r0,tE,tmax);//2TRAJ
    get_model_params(s,I0,Fs);
    Point pstart(0,0),pend(0,0);
    double margin=0,width=0,x0,y0,wx,wy;
    GLens *worklens=lens->clone();
    worklens->setState(s);

    //We start work in the lens frame
    {
      Trajectory *tr=traj->clone();
      tr->setState(s);
      double tleft=tr->get_frame_time(tstart),tright=tr->get_frame_time(tend);
      pstart=tr->get_obs_pos(tleft);
      pend=tr->get_obs_pos(tright);
      cout<<"making mag-map window between that fits points: ("<<pstart.x<<","<<pstart.y<<") and ("<<pend.x<<","<<pend.y<<")"<<endl;
      //margin=1;
      double dx=pstart.x-pend.x;
      double dy=pstart.y-pend.y;
      margin=sqrt(dx*dx+dy*dy)*0.1;
      delete tr;
    }
    delete worklens;
    width=wx=abs(pstart.x-pend.x);
    wy=abs(pstart.y-pend.y);
    if(wy>width)width=wy;
    x0=pstart.x;
    if(pend.x<x0)x0=pend.x;
    y0=pstart.y;
    if(pend.y<y0)y0=pend.y;
    width+=margin;
    y0=y0-(width-wy)/2.0;
    x0=x0-(width-wx)/2.0;
    cout<<"x0,y0,width="<<x0<<", "<<y0<<", "<<width<<endl;
    LLcorner=Point(x0,y0);
    URcorner=Point(x0+width,y0+width);
    cout<<"returning: LL=("<<LLcorner.x<<","<<LLcorner.y<<") UR=("<<URcorner.x<<","<<URcorner.y<<")"<<endl;
  };    

  ///Dump the trajectory
  ///Probably moves to trajectory eventually.
  void dump_trajectory(ostream &out, state &s, vector<double> &times, double tref){
    double I0,Fs;
    get_model_params(s, I0,Fs);

    GLens *worklens=lens->clone();
    worklens->setState(s);
    double xcm  =  worklens->getCenter().x;
    Trajectory *tr=traj->clone();
    tr->setState(s);
    cout<<"Dumping trajectory:"<<tr->print_info()<<endl;
    tr->set_times(times);
    cout<<"times range from "<<tr->t_start()<<" to "<<tr->t_end()<<endl;
    //Trajectory::verbose=true;
    out<<"#"<<s.get_string()<<endl;
    out<<"#";
    for(int i=0;i<s.size();i++)out<<s.getSpace()->get_name(i)<<" ";
    out<<"\n";
    out<<"#1.t   2. t_rel  3.x   4.y "<<endl;
    for(auto tph:times){
      double t=tr->get_frame_time(tph);
      Point p=tr->get_obs_pos(t);//Note: here p comes out in traj frame.
      out<<setprecision(15)<<t+tref<<" "<<t<<" "<<p.x<<" "<<p.y<<endl;
    }
  };

};

#endif




