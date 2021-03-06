//Gravitational microlensing data
//
//Written by John G Baker NASA-GSFC (2014-15)
#ifndef MLDATA_HH
#define MLDATA_HH
//#include "glens.hh"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <valarray>
#include "bayesian.hh"
#include <cerrno>
#include <cstring>

using namespace std;

///base class for photometry data
class ML_photometry_data : public bayes_data{
protected:
  vector<double>&times,&mags,&dmags;
  double time0;
  int idx_Mn;
  bool have_time0;
  bayes_frame *time_frame;
  bool have_time_frame, do_extra_noise;
public:
  ///We relabel the generic bayes_data names as times/mags/etc...
  ML_photometry_data():bayes_data(),times(labels),mags(values),dmags(dvalues),time0(label0){
    have_time0=false;
    have_time_frame=false;
    do_extra_noise=false;//Soon to change to false
  };
  //int size()const{return times.size();};
  /*
  virtual void getDomainLimits(double &start, double &end)const{
    checkData();
    if(times.size()==0){
      cout<<"MLdata::getTimeLimits: Cannot get limit on empty object."<<endl;
      exit(1);
    }
    start=times.front();
    end=times.back();
    };*/
  //double getPeakTime(bool original=false)const{
  virtual double getFocusLabel(bool original=false)const{
    assertData(LABELS|VALUES|DVALUES);
    if(original||times.size()<1)return time0; 
    //we assume monotonic time and magnitude data.
    double mpk=-INFINITY;
    int ipk=0;
    for( int i=0;i<times.size();i++){
      double m=-mags[i];//minus because peak negative magnitude
      if(mpk<m){
	mpk=m;
	ipk=i;
      }
    }
    return times[ipk];
    };
  ///Crop out some early data.
  ///
  ///Permanently remove early portion of data
  virtual void cropBefore(double tstart){
    assertData(LABELS|VALUES|DVALUES);
    while (times.size()>0&&times[0]<tstart){
      times.erase(times.begin());
      mags.erase(mags.begin());
      dmags.erase(dmags.begin());
    }
  };
  virtual vector<double> getVariances(const state &st)const{
    checkWorkingStateSpace();//Call this assert whenever we need the parameter index mapping.
    assertData(LABELS|VALUES|DVALUES);
    checkSetup();//Call this assert whenever we need options to have been processed.
    double extra_noise_mag=0;
    if(do_extra_noise)extra_noise_mag=st.get_param(idx_Mn);
    static const double logfactor=2.0*log10(2.5/log(10));
    vector<double>var(size());
    for(int i=0;i<size();i++){
      var[i] = dmags[i]*dmags[i];
      if(do_extra_noise)      
	var[i] += pow(10.0,logfactor+0.8*(-extra_noise_mag+mags[i]));
    }
    return var;
  };
  ///from stateSpaceInterface
  virtual void defWorkingStateSpace(const stateSpace &sp){
    checkSetup();//Call this assert whenever we need options to have been processed.
    if(do_extra_noise)idx_Mn=sp.requireIndex("Mn");
    haveWorkingStateSpace();
  };

  ///Optioned interface
  void addOptions(Options &opt,const string &prefix=""){
    Optioned::addOptions(opt,prefix);
    addTypeOptions(opt);
    addOption("tcut","Cut times before tcut (relative to tmax). Default=-1e20","-1e20");
    opt.add(Option("model_extra_noise","Assume a data model with a parameter for extra noise, beyond that estimated in the data files."));
    opt.add(Option("Fn_max","Uniform prior magnitude limit in (optional) added noise param. Default=1.0 (18.0 additive)/","1"));
  };
  ///Here provide options for the known types of ML_photometry_data...
  ///This is provided statically to allow options to select one or more types of data before specifying the 
  ///specific data sub-class in the main calling routine.  However, because the Optioned interface cannot
  ///be accessed statically, we create temporary instance and let it do the work.  There should be no need
  ///to preserve this temporary instance for later reference.
  void static addStaticOptions(Options &opt){
    ML_photometry_data d;
    d.addTypeOptions(opt);
  };
  ///If there is an externally defined reference time then use this function to specify it before calling setup()
  virtual void set_reference_time(double t0){
    if(have_time0){
      cout<<"ML_photometry_data::set_reference_time: Cannot reset reference time."<<endl;
      exit(1);
    }
    time0=t0;
    have_time0=true;
  };
  ///If there is an externally defined reference time then use this function to specify it before calling setup()
  virtual void set_time_frame(bayes_frame &frame){
    if(have_time_frame or have_time0){
      cout<<"ML_photometry_data::set_time_frame: Cannot reset reference time frame."<<endl;
      exit(1);
    }
    time_frame=&frame;
    have_time_frame=true;
  };
  virtual void setup(){
    ///Set up the output stateSpace for this object
    if(optSet("model_extra_noise"))do_extra_noise=true;
    if(do_extra_noise){
      stateSpace space(1);
      string names[]={"Mn"};
      double Fn_max;
      *optValue("Fn_max")>>Fn_max;
      //set stateSpace
      space.set_names(names);  
      nativeSpace=space;
      //set prior
      const double MaxAdditiveNoiseMag=22;
      const int uni=mixed_dist_product::uniform;
      valarray<double>    centers(1),halfwidths(1);
      valarray<int>         types(1);
      if(Fn_max<=1)Fn_max=18.0;
      double hw=(MaxAdditiveNoiseMag-Fn_max)/2.0;
      centers[0]=MaxAdditiveNoiseMag-hw;
      halfwidths[0]=hw;
      types[0]=uni;
      setPrior(new mixed_dist_product(&nativeSpace,types,centers,halfwidths));
    }else {
      setNoParams();
    }
  };

private:
  void addTypeOptions(Options &opt){
    Optioned::addOptions(opt,"");
    addOption("OGLE_data","Filepath to OGLE data.");
    addOption("gen_data","Filepath to generic photometry data.");
    addOption("mock_data","Construct mock data.");
  };  
protected:
  ///Initial data processing common to ML_photometry_data
  void processData(){
    ///Data times are converted from the time frame in the original data files to an internal time frame
    ///time0 holds the time in the data-file frame which maps to internal-time=0
    if(!have_time0){
      if(have_time_frame){
	if(time_frame->registered()){
	  time0=time_frame->getRef()[0];
	  cout<<"ML_photometry::processData: Set from supplied frame, time0="<<setprecision(15)<<time0<<endl;
	}
	else{
	  time0=getFocusLabel();
	  cout<<"ML_photometry::processData: Defining frame based on data, time0="<<setprecision(15)<<time0<<endl;
	  vector<double> ref(1);
	  ref[0]=time0;
	  time_frame->setRegister(ref);
	}
      } else {
	time0=getFocusLabel();
      }
      have_time0=true;
    }
    cout<<"ML_photometry data offset by "<<setprecision(15)<<time0<<" -> 0"<<endl;
    for(double &t : times)t-=time0;//permanently offset times from their to put the peak at 0.
    cout<<"...first data point is recorded at t[0]= "<<setprecision(15)<<times[0]<<endl;
    double tcut;
    *optValue("tcut")>>tcut;
    cropBefore(tcut);
    haveSetup();
  };
};

///class for mock data
///It does little other than define a grid of points, and allow them to be populated...
///There is also a hook to fill the data, which mllike knows how to do.  In this case
class ML_mock_data : public ML_photometry_data {
public:
  ML_mock_data(){
    typestring="MLphotometryData";option_name="MLPMockData";option_info="Mock microlensing photometry data.";
    allow_fill=true;};
  ///The time samples are generated from a regular grid, or randomly...
  ///...not yet complete...
  ///Note that cadence is the most probable size of timestep, with fractional variance scale set by log_dtvar
  void setup(){
    double tstart,tend,cadence,jitter,noise;
    *optValue("mock_tstart")>>tstart;
    *optValue("mock_tend")>>tend;
    *optValue("mock_cadence")>>cadence;
    *optValue("mock_jitter")>>jitter;
    *optValue("mock_noise")>>noise;
    cout<<"Preparing mock data."<<endl;
    ML_photometry_data::setup();
    setup(tstart,tend,cadence,noise,jitter);
  };
  void setup(double tmin, double tmax, double cadence, double noise_lev, double log_dt_var=0){
    GaussianDist gauss(0.0,log_dt_var);
    double dt=cadence*exp(gauss.draw());
    double time=tmin+dt/2.0;
    cout<<"setting up mock mldata with noise_lev="<<noise_lev<<endl;
    while(time<tmax){
      times.push_back(time);
      dt=cadence*exp(gauss.draw());
      time+=dt;
      mags.push_back(0);
      dmags.push_back(noise_lev);
    }
    haveData();
    if(!have_time0)set_reference_time(0);
    processData();
  };
  ///Optioned interface
  void addOptions(Options &opt,const string &prefix=""){
    ML_photometry_data::addOptions(opt,prefix);
    addOption("mock_tstart","Start time for mock data sample grid (days). Default=-600","-600");
    addOption("mock_tend","End time for mock data sample grid (days). Default=150","150");
    addOption("mock_cadence","Typical sample period for mock data sample grid(days). Default=1","1");
    addOption("mock_jitter","Size of standard deviation in log(time-step-size). Default=0","0");
    addOption("mock_noise","Size of noise in the mock_data (magnitudes). Default=0.02","0.02");
  };
};


//class for OGLEII-IV DIA data
class ML_OGLEdata : public ML_photometry_data {
  //OGLE-IV:Photometry data file containing 5 columns: Hel.JD, I magnitude, magnitude error, seeing estimation (in pixels - 0.26"/pixel) and sky level.
public:
  ML_OGLEdata(){
    typestring="MLphotometryData";option_name="MLPOGLEData";option_info="OGLE microlensing photometry data.";
};
  void setup(){
    string filename;
    *optValue("OGLE_data")>>filename;
    cout<<"OGLE data file='"<<filename<<"'"<<endl;
    ML_photometry_data::setup();
    setup(filename);
  };
  void setup(const string &filepath){
    ifstream file(filepath.c_str());
    if(file.good()){
      string line;
      while(getline(file,line)){
	if(line[0]=='#')continue;//skip comment lines
	double t,m,dm;
	stringstream(line)>>t>>m>>dm;
	times.push_back(t);
	mags.push_back(m);
	dmags.push_back(dm);
      }
    } else {
      if(filepath.size()>0){//empty path signifies go forward without data
	cerr << "Error: " << strerror(errno)<<endl;
	cout<<"ML_OGLEData::ML_OGLEData: Could not open file '"<<filepath<<"'."<<endl;
	exit(1);
      }
    }
    haveData();
    processData();
    return;
  };

};

//class for generic two column time/mag data
class ML_generic_data : public ML_photometry_data {
public:
  ML_generic_data(){
    typestring="MLphotometryData";option_name="MLPGenData";option_info="Generic microlensing photometry data.";
  };
  void setup(){
    string filename;
    *optValue("gen_data")>>filename;
    cout<<"generic data file='"<<filename<<"'"<<endl;
    ML_photometry_data::setup();
    setup(filename);
  };
  void setup(const string &filepath){
    //assemble soruce column info
    double errlev,toffset;
    *optValue("gen_data_err_lev")>>errlev;
    int tcol,col,ecol,maxcol=0;
    *optValue("gen_data_time_off")>>toffset;
    *optValue("gen_data_time_col")>>tcol;
    if(tcol>maxcol)maxcol=tcol;
    *optValue("gen_data_col")>>col;
    if(col>maxcol)maxcol=col;
    if(errlev<=0){
      *optValue("gen_data_err_col")>>ecol;
      if(ecol<0)ecol=col+1;
      if(ecol>maxcol)maxcol=ecol;
    }
    cout<<"gen_data: reading data as:\ntcol,col="<<tcol<<","<<col<<" err="<<((errlev>0)?ecol:errlev)<<endl;
    ifstream file(filepath.c_str());
    if(file.good()){
      string line;
      while(getline(file,line)){
	//cout<<"reading line:\n"<<line<<endl;
	if(line[0]=='#'||line.length()==0)continue;//skip comment lines
	stringstream ss(line);
	for(int i=0;i<=maxcol;i++){
	  //cout "i="<<i<<endl;
	  double val;
	  ss>>val;
	  if(i==tcol)times.push_back(val+toffset);
	  if(i==col)mags.push_back(val);
	  if(errlev<=0&&i==ecol)dmags.push_back(val);
	}
	if(errlev>0)dmags.push_back(errlev);
	int i=times.size()-1;
	//cout<<times[i]<<" "<<mags[i]<<" "<<dmags[i]<<endl;
      }
    } else {
      if(filepath.size()>0){//empty path signifies go forward without data
	cout<<"ML_generic_data: Could not open file '"<<filepath<<"'."<<endl;
	exit(1);
      }
    }
    haveData();
    cout<<"ML_generic_data: After initial read of data first datum time is times[0]="<<times[0]<<endl;
    processData();
    return;
  };
  void addOptions(Options &opt,const string &prefix=""){
    ML_photometry_data::addOptions(opt,prefix);
    addOption("gen_data_time_col","Column with data values. Default=0","0");
    addOption("gen_data_time_off","Add this to column values for JD time. Default=0","0");
    addOption("gen_data_col","Column with data values. Default=1","1");
    addOption("gen_data_err_col","Column with data values. Default=(next after data)","-1");
    addOption("gen_data_err_lev","Set a uniform error, instead of reading from file. Default=none","-1");
  };
};



#endif
