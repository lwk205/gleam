//Gravitational microlensing data
//
//Written by John G Baker NASA-GSFC (2014-15)
#ifndef MLDATA_HH
#define MLDATA_HH
#include "glens.hh"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <valarray>
#include "bayesian.hh"

using namespace std;
//plan:
// This is functional as is, but not extensible (and not logical).
// Need to abstract the model realization from the MLdata model.
// Need a new MLmodel class which can produce lightcurve data etc
// from a vector of params.
// Then the MLdata class will produce a likelihood from a set of model data together with
// a model.  The subtlety of this is that the model may need to include some instrumental 
// parameters.
// An ideal possibility might be to allow combining models with separately defined signal
// and instrument components.  There may be a need to pull params by name,etc... need to
// think more.  Maybe there is a simpler solution...

//base class for data
class ML_photometry_data : public bayes_data{
protected:
  vector<double>&times,&mags,&dmags;
  double time0;
  int idx_Mn;

public:
  ///We relabel the generic bayes_data names as times/mags/etc...
  ML_photometry_data():bayes_data(),times(labels),mags(values),dmags(dvalues),time0(label0){
    idx_Mn=-1;
  };
  int size()const{return times.size();};
  virtual void getDomainLimits(double &start, double &end)const{
    checkData();
    if(times.size()==0){
      cout<<"MLdata::getTimeLimits: Cannot get limit on empty object."<<endl;
      exit(1);
    }
    start=times.front();
    end=times.back();
  };
  //double getPeakTime(bool original=false)const{
  virtual double getFocusLabel(bool original=false)const{
    checkData();
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
    checkData();
    while (times.size()>0&&times[0]<tstart){
      times.erase(times.begin());
      mags.erase(mags.begin());
      dmags.erase(dmags.begin());
    }
  };
  //vector<double>getMags()const{return mags;};
  ///May eliminate/change to only reference the state-dependent version (now realized with mag_noise_var)
  //vector<double>getDeltaMags()const{return dmags;};
  /*
    virtual double getVariance(int i)const{
    checkData();
    if(i<0||i>size()){
      cout<<"ML_photometry_data:getVariance: Index out of range."<<endl;
      exit(1);
    }
    double var = dmags[i]*dmags[i];
    static const double logfactor=2.0*log10(2.5/log(10));
    var +=pow(10.0,logfactor+0.8*(-extra_noise_mag+mags[i]));
    return var;
    //return dmags[i]*dmags[i]+pow(10.0,logfactor+0.8*(-noise_mag+mags[i]));
    };*/
  virtual vector<double> getVariances(const state &st)const{
    checkWorkingStateSpace();//Call this assert whenever we need the parameter index mapping.
    checkData();//Call this assert whenever we need the data to be loaded
    checkSetup();//Call this assert whenever we need options to have been processed.
    //cout<<"dataVar for state:"<<st.show()<<endl;
    double extra_noise_mag=st.get_param(idx_Mn);
    //cout<<"extra_noise_mag:st["<<idx_Mn<<"]="<<extra_noise_mag<<endl;
    //cout<<"noise_mag:"<<extra_noise_mag<<endl;
    static const double logfactor=2.0*log10(2.5/log(10));
    vector<double>var(size());
    for(int i=0;i<size();i++){
      var[i] = dmags[i]*dmags[i] + pow(10.0,logfactor+0.8*(-extra_noise_mag+mags[i]));
    }
    return var;
    //return dmags[i]*dmags[i]+pow(10.0,logfactor+0.8*(-noise_mag+mags[i]));
  };
  /*
  ///Do we need a write function?
  virtual void write(ostream &out,vector<double>vparams,bool integrate=false, int nsamples=-1, double tstart=0, double tend=0)=0;
  //This one is for the nascent "signal" interface.
  virtual void write(ostream &out,state &st, int nsamples=-1, double tstart=0, double tend=0){
    write(out,st.get_params_vector(),true,nsamples,tstart,tend);
  };
  */
  /*
  virtual void set_model(state &st){
    bayes_data::set_model(st);
    extra_noise_mag=st.get_param(idx_Mn);
    cout<<"extra_noise_mag:"<<extra_noise_mag<<endl;
  };
  */
  ///from stateSpaceInterface
  virtual void defWorkingStateSpace(const stateSpace &sp){
    checkSetup();//Call this assert whenever we need options to have been processed.
    ///This is how the names were hard-coded.  We want to have these space components be supplied by the signal/data objects
    //string names[]={"I0","Fs","Mn","logq","logL","r0","phi","tE","tpass"};
    idx_Mn=sp.requireIndex("Mn");
    haveWorkingStateSpace();
  };

  ///Set up the output stateSpace for this object
  ///
  ///This is just an initial draft.  To be utilized in later round of development.
  virtual stateSpace getObjectStateSpace()const{
    checkSetup();//Call this assert whenever we need options to have been processed.
    stateSpace space(1);
    string names[]={"Mn"};
    space.set_names(names);  
    return space;
  };
  ///Optioned interface
  void addOptions(Options &opt,const string &prefix=""){
    Optioned::addOptions(opt,prefix);
    //data options
    addOption("tcut","Cut times before tcut (relative to tmax). Default=-1e20/","-1e20");
  };
protected:
  ///Initial data processing common to ML_photometry_data
  void processData(){
    double tcut;
    *optValue("tcut")>>tcut;
    cropBefore(tcut);
    haveSetup();
  };

};

//class for OGLEII-IV DIA data
class ML_OGLEdata : public ML_photometry_data {
  //OGLE-IV:Photometry data file containing 5 columns: Hel.JD, I magnitude, magnitude error, seeing estimation (in pixels - 0.26"/pixel) and sky level.
public:
  ML_OGLEdata(){};
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
	cout<<"OGLEData::OGLEData: Could not open file '"<<filepath<<"'."<<endl;
	exit(1);
      }
    }
    have_data=true;//Do we need this in addition to checkSetup?
    //This is the original style.  After initialization, time is internally referenced relative to the peak time.
    //We may want to make this relative to some externally defined reference time...
    time0=getFocusLabel();
    for(double &t : times)t-=time0;//permanently offset times from their to put the peak at 0.
    processData();
    return;
  };


};

//class for generic two column time/mag data
class ML_generic_data : public ML_photometry_data {
public:
  ML_generic_data(){};
  void setup(const string &filepath){
    ifstream file(filepath.c_str());
    if(file.good()){
      string line;
      while(getline(file,line)){
	if(line[0]=='#')continue;//skip comment lines
	double t,m,d=0.000001;
	stringstream(line)>>t>>m;
	times.push_back(t);
	mags.push_back(m);
	dmags.push_back(d);
	//cout<<size()<<": "<<t<<" "<<m<<" "<<d<<endl;
      }
    } else {
      if(filepath.size()>0){//empty path signifies go forward without data
	cout<<"ML_generic_data: Could not open file '"<<filepath<<"'."<<endl;
	exit(1);
      }
    }
    have_data=true;//Do we need this in addition to checkSetup?
    //This is the original style.  After initialization, time is internally referenced relative to the peak time.
    //We may want to make this relative to some externally defined reference time...
    time0=getFocusLabel();
    for(double &t : times)t-=time0;//permanently offset times from their to put the peak at 0.
    processData();
    return;
  };

};



#endif
