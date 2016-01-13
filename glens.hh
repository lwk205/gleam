//Gravitational lens equation for microlensing
//Written by John G Baker NASA-GSFC (2014)

#ifndef GLENS_HH
#define GLENS_HH
#include <vector>
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include "bayesian.hh"
using namespace std;
extern bool debug;

///Code for computing binary microlensing light curves and images.
///This part deals with the binary lens and the lens map.
///Generally the reference frame is defined by the source and lens centers
///with the observer moving through the some observing plane.

///We are thinking here of a binary lens, but there are other possibilities.
///The core methods for any lens are the lensing map, magnification, etc.
///Finite source effects would generally require integration over the source 
///plane.
///There are various possible implementations for a point lens everything can 
///be done analytically.  For a fixed binary, the lens map can be reduced to a 
///polynomial root-finding problem.  Generically, we can grid the lens plane and
///apply brute force. This would need to be done only once per set of lens params (masses,separation).

///First we define a struct for representing 2D points
typedef struct Point {
  double x;
  double y;
  Point():Point(0,0){};
  Point(double x, double y):x(x),y(y){};
  friend Point operator+(const Point &p1, const Point &p2);
  friend Point operator-(const Point &p1, const Point &p2);
}Point;
//

///Next is a class for trajectories through the observer plane
///base class implements a straight-line trajectory
class Trajectory {
protected:
  Point p0;
  Point v0;
  double t0;
  double tf;
  double cad;
  double toff;
  bool have_times;
  vector<double> times;
public:
  Trajectory(Point pos0,Point vel0,double t_end,double cadence, double t0=0):p0(pos0),v0(vel0),cad(cadence),tf(t_end),t0(t0),toff(0),have_times(false){
    //cout<<"Trajectory({"<<pos0.x<<","<<pos0.y<<"},{"<<vel0.x<<","<<vel0.y<<"},...)"<<endl;
  };
  Trajectory(Point pos0,Point vel0):p0(pos0),v0(vel0),cad(1),tf(1),t0(0),have_times(false){
    //cout<<"Trajectory({"<<pos0.x<<","<<pos0.y<<"},{"<<vel0.x<<","<<vel0.y<<"})"<<endl;
  };
  virtual Trajectory* clone(){
    return new Trajectory(*this);
  };
  virtual void setup(const Point &pos0, const Point &vel0){p0=pos0;v0=vel0;};
  ///set the required eval times.  
  virtual void set_times(vector<double> times,double toff){this->times=times;t0=times[0];tf=times.back();have_times=true;this->toff=toff;};//cout<<"tf="<<tf<<", times="<<this->times.size()<<", times["<<times.size()-1<<"]="<<times[times.size()-1]<<endl;};
  virtual double t_start()const {return t0;};
  virtual double t_end()const {return tf;};
  virtual int Nsamples()const {if(have_times)return times.size(); else return (int)((t_end()-t_start())/cad)+1;};
  virtual double get_obs_time(int ith)const {if(have_times)return times[ith]; else return t0-toff+cad*ith;};
  virtual Point get_obs_pos(double t)const {double x=p0.x+(t-toff)*v0.x,y=p0.y+(t-toff)*v0.y;return Point(x,y);};
  virtual Point get_obs_vel(double t)const {return v0;};
  virtual string print_info()const {ostringstream s;s<<"Trajectory({"<<p0.x<<","<<p0.y<<"},{"<<v0.x<<","<<v0.y<<"})"<<endl;return s.str();};
};

///Next is a class for trajectories through the observer plane
///class implements a straight-line trajectory
class ParallaxTrajectory : public Trajectory {
protected:
  double source_lon,source_lat;
public:
  ParallaxTrajectory(Point pos0, Point vel0, double t_end, double caden, double source_ra, double source_dec, double t2000off, double t0=0):Trajectory(pos0,vel0,t_end,caden,t0){
    equatorial2ecliptic(source_ra,source_dec,source_lat,source_lon);};
  virtual ParallaxTrajectory* clone(){
    return new ParallaxTrajectory(*this);
  };
  double t_start()const {return t0;};
  double t_end()const {return tf;};
  double get_obs_time(int ith)const {return t0+cad*ith;};
  Point get_obs_pos(double t)const {return Trajectory::get_obs_pos(t)+get_obs_pos_offset(t);};
  Point get_obs_vel(double t)const {return Trajectory::get_obs_vel(t)+get_obs_vel_offset(t);};
  ///The following functions are to help with computing the parallax
  ///First we need to define the observer orbital motion in barycentric coordinates
  void get_barycentric_observer(double t, double &r, double &theta, double &phi){};
protected:
  ///We need to know the approximate location of the source to compute the parallax
  double source_ra, source_dec;
  ///These are internal functions needed to realize the parallax
  ///First note that we will use time standardized to in JD since J2000
  ///Define the observer trajectory position in SSB coordinates
  virtual void get_obs_pos_ssb(double t, double &x, double &y, double &z)const;
  ///Define the observer trajectory velocity in SSB coordinates
  ///For this class we define an approx Earth-Moon barycenter trajectory but that can be overloaded.
  virtual void get_obs_vel_ssb(double t, double &x, double &y, double &z)const;
  ///Using the approx location of the source, transform from ssb in source-lens line-of-sight frame.
  virtual Point ssb_los_transform(double x, double y, double z)const;
  ///Compute observer position offset from ssb in source-lens line-of-sight frame.
  virtual Point get_obs_pos_offset(double t)const{
    double x,y,z;
    get_obs_pos_ssb(t,x,y,z);
    return ssb_los_transform(x,y,z);
  };
  ///Compute observer velocity offset from ssb in source-lens line-of-sight frame.
  virtual Point get_obs_vel_offset(double t)const{
    double x,y,z;
    get_obs_vel_ssb(t,x,y,z);
    return ssb_los_transform(x,y,z);
  };
  ///approximately convert equatorial to ecliptic coordinates
  void equatorial2ecliptic(double source_ra, double source_dec, double source_lat, double source_lon){
    const double eps=23.4372; //as of 2016.0, decreasing at 0.00013/yr
    const double ceps=cos(eps),seps=sin(eps);
    double sa=sin(source_ra),ca=cos(source_ra);
    double sd=sin(source_dec),cd=cos(source_dec);
    source_lon=atan2(sa*ceps+sd/cd*seps,ca);
    source_lat=sd*ceps-cd*seps*sa;
  };
};

///This is a generic abstract base class for thin gravitational lens objects.
class GLens :public bayes_component{
protected:
  int NimageMax;
  static const double constexpr dThTol=1e-9;
  ///For temporary association with a trajectory;
  const Trajectory *trajectory;
  ///For use with GSL integration
  static int GSL_integration_func (double t, const double theta[], double thetadot[], void *instance);
  static int GSL_integration_func_vec (double t, const double theta[], double thetadot[], void *instance);
  ///For use with GSL integration
  double kappa=.1;
  int Ntheta;
  bool use_integrate,have_integrate,do_verbose_write;
  double GL_int_tol,GL_int_mag_limit;
  virtual bool testWide(const Point & p,double scale)const{return false;};//test conditions to revert to perturbative inversion

public:
  GLens(){have_integrate=false;do_verbose_write=false;};
  virtual GLens* clone()=0;
  ///Lens map: map returns a point in the observer plane from a point in the lens plane.
  virtual Point map(const Point &p)=0;
  ///Inverse sens map: invmap returns a set of points in the lens plane which map to some point in the observer plane.  Generally multivalued;
  virtual vector<Point> invmap(const Point &p)=0;
  ///Given a point in the lens plane, return the magnitude
  virtual double mag(const Point &p)=0;
  ///Given a set of points in the lens plane, return the combined magnitude
  virtual double mag(const vector<Point> &plist){
    double m=0;
    for(Point p : plist){
      m+=abs(mag(p));//syntax is C++11
      if(debug)cout<<"    ("<<p.x<<","<<p.y<<") --> mg="<<m<<endl;
    }
    if(plist.size()==0)return 1; //hack to more gracefully fail in trivial regions
    return m;
  };
  ///returns J=det(d(map(p))/dp)^-1, sets, j_ik = d(map(pi))/dpk
  virtual double jac(const Point &p,double &j00,double &j01,double &j10,double &j11)=0;
  ///returns J=det(d(map(p))/dp))^-1, sets, j_ik = (d(map(pi))/dpk)^-1
  virtual double invjac(const Point &p,double &j00,double &j01,double &j10,double &j11)=0;
  ///compute images and magnitudes along some trajectory
  void compute_trajectory (const Trajectory &traj, vector<double> &time_series, vector<vector<Point> > &thetas_series, vector<int> &index_series,vector<double>&mag_series,bool integrate=false);
  void set_integrate(bool integrate_or_not){use_integrate=integrate_or_not;have_integrate=true;}
  //For the Optioned interface:
  virtual void addOptions(Options &opt,const string &prefix="");
  virtual void setup();
  virtual string print_info()const=0;
  //For stateSpaceInterface
  virtual void defWorkingStateSpace(const stateSpace &sp)=0;
  virtual stateSpace getObjectStateSpace()const=0;
  virtual void setState(const state &s)=0;
  //Write a magnitude map to file.
  virtual Point getCenter(int option=-2)const=0;
  virtual void writeMagMap(ostream &out, const Point &LLcorner,const Point &URcorner,int samples){//,bool output_nimg=false){
    double xc=getCenter().x;
    cout<<"GLens::writeMagMap from ("<<LLcorner.x+xc<<","<<LLcorner.y<<") to ("<<URcorner.x+xc<<","<<URcorner.y<<")"<<endl;
    double dx=(URcorner.x-LLcorner.x)/(samples-1);    
    double dy=(URcorner.y-LLcorner.y)/(samples-1);    
    //cout<<"mag-map ranges from: ("<<x0<<","<<y0<<") to ("<<x0+width<<","<<y0+width<<") stepping by: "<<dx<<endl;
    int output_precision=out.precision();
    double ten2prec=pow(10,output_precision-2);
    out<<"#x-xc  y  magnification"<<endl;
    for(double y=LLcorner.y;y<=URcorner.y;y+=dy){
      Trajectory traj(Point(LLcorner.x+xc,y), Point(1,0), URcorner.x-LLcorner.x, dx);
      vector<int> indices;
      vector<double> times,mags;
      vector<vector<Point> >thetas;
      compute_trajectory(traj,times,thetas,indices,mags);
      for(int i : indices){
	Point b=traj.get_obs_pos(times[i]);
	double mtruc=floor(mags[i]*ten2prec)/ten2prec;
	out.precision(output_precision);
	out<<b.x<<" "<<b.y<<" "<<setiosflags(ios::scientific)<<mtruc<<setiosflags(ios::fixed);
	if(do_verbose_write){
	  out<<" "<<thetas[i].size();
	  if(true){
	    for(int j=0;j<thetas[i].size();j++){
	      out<<" "<<thetas[i][j].x<<" "<<thetas[i][j].y;
	    }	
	  }
	}
	out<<endl;
	
      }
      out<<endl;
    }	  
  };   
  void verboseWrite(bool state=true){do_verbose_write=state;};
  
};

///A rigid binary lens implementation
///Working in units of total mass Einstein radius, the only parameters are
/// mass ratio q and separation L
class GLensBinary : public GLens{
  double q;
  double L;
  //mass fractions
  double nu;
  vector<Point> invmapAsaka(const Point &p);
  vector<Point> invmapWittMao(const Point &p);
  double rWide;
  int idx_q,idx_L;
  ///test conditions to revert to perturbative inversion
  bool testWide(const Point & p,double scale)const{
    double rs=rWide*scale;
    if(rs<=0)return false;
    double r2=p.x*p.x+p.y*p.y;
    //if(( L>rs||r2>rs*rs)&&scale!=1.0)cout<<sqrt(r2)<<" <> "<<rs<<" <> "<<L<<endl;
    //return L>rs||r2>rs*rs;
    return L>rs||r2>rs*rs||(q+1/q)>2*rs*rs;
  };  
public:
  GLensBinary(double q=1,double L=1);
  virtual GLensBinary* clone(){
    return new GLensBinary(*this);
  };
  Point map(const Point &p);
  //For the GLens interface:
  vector<Point> invmap(const Point &p);
  vector<Point> invmapWideBinary(const Point &p);
  double mag(const Point &p);
  using  GLens::mag;
  double jac(const Point &p,double &j00,double &j01,double &j10,double &j11);
  double invjac(const Point &p,double &j00,double &j01,double &j10,double &j11);
  //specific to this class:
  double get_q(){return q;};
  double get_L(){return L;};
  double set_WideBinaryR(double r){rWide=r;};
  /*
  void setup(double q_, double L_){
    GLens::setup();
    q=q_;
    L=L_;
    nu=1/(1+q);
    }*/
  virtual string print_info()const{ostringstream s;s<<"GLensBinary(q="<<q<<",L="<<L<<")"<<(have_integrate?(string("\nintegrate=")+(use_integrate?"true":"false")):"")<<endl;return s.str();};
  //virtual string print_info()const{ostringstream s;s<<"GLensBinary(q="<<q<<",L="<<L<<")"<<endl;return s.str();};

  ///From StateSpaceInterface (via bayes_component)
  ///
  void defWorkingStateSpace(const stateSpace &sp){
    checkSetup();
    idx_q=sp.requireIndex("q");
    idx_L=sp.requireIndex("L");
    haveWorkingStateSpace();
  };  
  ///Set up the output stateSpace for this object
  ///
  stateSpace getObjectStateSpace()const{
    checkSetup();//Call this assert whenever we need options to have been processed.
    stateSpace space(2);
    string names[]={"q","L"};
    space.set_names(names);  
    return space;
  };
  ///Set state parameters
  ///
  void setState(const state &st){;
    checkWorkingStateSpace();
    q=st.get_param(idx_q);
    L=st.get_param(idx_L);
    nu=1/(1+q);
  };
  ///This is a class-specific variant which 
  void setState(double q_, double L_){;
    checkWorkingStateSpace();
    q=q_;
    L=L_;
    nu=1/(1+q);
  };
  Point getCenter(int option=-2)const{
    double x0=0,y0=0,width=0,wx,wy,xcm = (q/(1.0+q)-0.5)*L;
    cout<<"q,L,option="<<q<<", "<<L<<", "<<option<<endl;
    //center on {rminus-CoM,CoM-CoM,rplus-CoM}, when cent={-1,0,1} otherwise CoM-nominalorigin;
    switch(option){
    case -1://minus lens rel to CoM
      x0=-0.5*L-xcm;
      break;
    case 0:
      x0=0;
      break;
    case 1:
      x0=0.5*L-xcm;//plus lens rel to CoM
      break;
    default:
      x0=xcm;
      cout<<"case def"<<endl;
    }
    cout<<" GLensBinary::getCenter("<<option<<"):Returning x0="<<x0<<endl;
    return Point(x0,0);
  };
};

#endif
