# Modify these three as appropriate for the system

export ROOT
export LIB = ${ROOT}/lib
export INCLUDE = ${ROOT}/include
MCMC = ${ROOT}/ptmcmc
export CXX
export CFLAGS
INC = ${ROOT}/include
LIB = ${ROOT}/lib

ifneq ($(findstring discover,$(shell hostname)),)
#discover, choose option below
  COMP=gcc5
    ifeq (${COMP},intel)
    #this is needed for profiling with tau.  This only works for intel compiler
    #(tau not installed for gcc on discover) however it doesn't support quad-precision.
    #assumes: module load comp/intel-13.0.1.117
    $(info "Building for intel-13")
    CXX = icpc
    CC = icc
    F90 =ifort
    LD = ifort
    GSLROOT = /usr/local/other/SLES11.1/gsl/1.16/intel-13.0.1.117
    CFLAGS = -O3 -openmp -lirc -lifcore
    CFLAGS += -g -Wall -Wl,--no-as-needed -lmkl_intel_lp64 -lmkl_sequential -lmkl_core -ldl -lpthread -lm
  else ifeq (${COMP},gcc5)
    #assumes: module load other/comp/gcc-5.3-sp3
    $(info "Building for gcc-5")
    CXX = g++ -fcx-fortran-rules
    CC = gcc -fcx-fortran-rules
    F90=gfortran -fcx-fortran-rules
    LD = gcc
    GSLROOT=/usr/local/other/SLES11.1/gsl/1.16/gnu-4.8.1
    CFLAGS = -g -lquadmath -Wuninitialized -O2  -fopenmp
    CXXFLAGS = $(CFLAGS) -std=c++11 -O3
  else  
    #assumes: module load other/comp/gcc-4.8.4-sp3
    $(info Building for gcc-4)
    CXX = g++
    CC = gcc
    F90=gfortran
    LD = gcc
    GSLROOT=/usr/local/other/SLES11.1/gsl/1.16/gnu-4.8.1
    CFLAGS = -O3 -lquadmath -Wuninitialized -fopenmp -lgfortran
    #add the following for debugging
    CFLAGS += -g -fstack-check -I$(GSLROOT)/include
    #add the following for profiling (with gprof, too slow for production)
    #CFLAGS += -pg
    CXXFLAGS = $(CFLAGS) -std=c++11 -O2
  endif
else 
#laptop
  CXX = g++-mp-5 -fcx-fortran-rules
  CC = gcc-mp-5 -fcx-fortran-rules
  F90 = gfortran-mp-5 -fcx-fortran-rules
  #Note the lens code seems faster (compiled with x86_64-apple-darwin13-gfortran-mp-4.8) with -O2 than -O3
  #CFLAGS = -march=corei7 -g -lquadmath -Wuninitialized -fno-inline -fno-default-inline  -fopenmp
  CFLAGS = -march=corei7 -g -lquadmath -Wuninitialized -O2  -fopenmp
  #CXXFLAGS = $(CFLAGS) -std=c++11 -g -fno-inline -fno-default-inline
  CXXFLAGS = $(CFLAGS) -std=c++11 -O3
  GSLROOT=/opt/local

endif

GSLDIR=$(GSLROOT)/lib
GSLINC=$(GSLROOT)/include
CFLAGS += -I${INCLUDE} -I${MCMC} 
LDFLAGS+= -L${LIB} -L${FFTW}/lib -lfftw3	

