//===- CPHistogram.cpp ----------------------------------------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cp-histogram"

#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/CPHistogram.h"
#include "llvm/Analysis/ProfileInfoTypes.h"

#include <cmath>
#include <limits>
#include <algorithm>
#include <set>
#include <stdlib.h>


using namespace llvm;

int CPHistogram::HistID = 0;


#define DEBUG_CPHIST(s) errs() << "(#" << _id << ") " << s
//#define DEBUG_LCTOR(s) DEBUG_CPHIST(s)
//#define DEBUG_BFL(s) DEBUG_CPHIST(s)
//#define DEBUG_LIST(s) DEBUG_CPHIST(s)
#define DEBUG_LCTOR(s)
#define DEBUG_BFL(s)
#define DEBUG_LIST(s)

CPHistogram::~CPHistogram()
{
  //errs() << "(#" << _id << ") ~CPHistogram : "  << _bins;
  if(_bins != NULL)
  {
    free(_bins);
    _bins = NULL;
  }
  //errs() << " --> " << _bins << "  done\n";
}


// creates a point histogram at 0
CPHistogram::CPHistogram() :
  _min(0), _max(0), _bincount(0), _bins(0)
{
  _id = HistID++;
  _stats.clear();
  //errs() << "(#" << _id << ") CPHistogram::CPHistogram(CombinedProfile* owner)\n";
}

// copy ctor
CPHistogram::CPHistogram(const CPHistogram& rhs) :
  _min(rhs._min), _max(rhs._max), _bincount(0), _bins(0)
{
  // allocate bins  (points have 0 bins, none allocated)
  setBinCount(rhs._bincount);

  // copy data
  if(!rhs.isPoint())
    for(unsigned i = 0; i < _bincount; i++)
      _bins[i] = rhs._bins[i];

  _stats = rhs._stats;
  //_min = rhs._min;
  //_max = rhs._max;
}


CPHistogram::CPHistogram(unsigned bincount, double totalweight,
                         CPHistogramList& hl) :
  _min(0), _max(0), _bincount(bincount), _bins(0) 
{
  _id = HistID++;

  DEBUG_LCTOR("CPHistogram::CPHistogram(list:" << hl.size() << ")\n");

  clear();  // point histogram at 0

  // no list; nothing to do
	if(hl.size() == 0)
    return;

  // just copy if there is only one item; no need to merge
  if(hl.size() == 1)
  {
    *this = *hl.front();
    return;
  }

  // ... and now the real work ...

  CPHistogramList::iterator H = hl.begin();
  CPHistogramList::iterator E = hl.end();

  // sanitize the list: remove NULLs and 0s
  while(H != E)
  {
    if( (*H == NULL) || !(*H)->nonZero() )
      H = hl.erase(H);
    else
    {
      if( (*H)->min() == 0 )
      {
        errs() << "Warning: non-zero histogram in ctor list has "
               << "0 lower bound (max=" << (*H)->max() << ", w="
               << (*H)->nonZeroWeight() << ")\n";
      }
      H++;
    }
  }
  
  // done if list is empty
  if(hl.size() == 0)
  {
    DEBUG_LCTOR("list empty after sanitizing\n");
    return;
  }

  // range will be set to something sensible by first non-Zero histogram
	double minVal = std::numeric_limits<double>::max();
	double maxVal = 0;
  bool rangeUpdate = false;

  // incremental stats update
  for(H = hl.begin(), E = hl.end(); H != E; ++H)
  {
    _stats.combineStats((*H)->_stats);
        
    // _min should never be 0 except for points at 0 (filtered above)
    if( (*H)->_min < minVal )
    {
      DEBUG_LCTOR("  new min: " << (*H)->_min << "\n");
      minVal = (*H)->_min;
      rangeUpdate = true;
    }
    if( (*H)->_max > maxVal )
    {
      DEBUG_LCTOR("  new max: " << (*H)->_max << "\n");
      maxVal = (*H)->_max;
      rangeUpdate = true;
    }
  }

  // Account for any 0 histograms that were not in the list
  if(_stats.totalWeight < totalweight)
  {
    DEBUG_LCTOR("  adding " << totalweight - _stats.totalWeight << "0s\n");
    Stats s2;
    //s2.clear();
    s2.totalWeight = totalweight - _stats.totalWeight;
    _stats.combineStats(s2);
  }

  if(!rangeUpdate)
  {
    errs() << "Warning: non-empty list did not update range in ctor\n";
    minVal = maxVal = 0;
  }

  DEBUG_LCTOR("  setting range: [" << minVal << ", " << maxVal << "]\n");
  setRange(minVal, maxVal);

  if(isPoint())
    setBinCount(0);
  else
  {
    setBinCount(bincount);

    // Add the weight from each histogram proportionally to bins
    for(unsigned i = 0; i < _bincount; i++) 
      for(H = hl.begin(), E = hl.end(); H != E; ++H)
      {
        double l = getBinLowerLimit(i);
        double u = getBinUpperLimit(i);
        double w = (*H)->getRangeWeight(l, u);
        addToBin(i, w);
      }
  }
  
  //errs() << "<-- ctor CPHistogram(list)\n";
  //errs() << "(#" << _id << ") list constructor done (" << hl.size() << ")\n";
}


void CPHistogram::copyBins(const CPHistogram& other)
{
  if(_bins != NULL)
  {
    free(_bins);
    _bins = NULL;
  }

  setBinCount(other._bincount);

  // these two test should be redundant
  if((other._bincount > 0) && (!other.isPoint()))
  {
    for(unsigned i = 0; i < _bincount; i++)
      _bins[i] = other._bins[i];
  }
}


/****************  Operators *******************/

// Assignment.  Does not copy any values pending in the add list.
CPHistogram& CPHistogram::operator=(const CPHistogram& rhs)
{
  if(this == &rhs)
    return(*this);
  
  copyBins(rhs);

  _stats = rhs._stats;

  _min = rhs._min;
  _max = rhs._max;

  return(*this);
}


// Re-build as if parametric distributions

CPHistogram* CPHistogram::asUniform() const
{

  // points are already a 0-range uniform
  if(isPoint())
    return(new CPHistogram(*this));

  CPHistogram* rc = new CPHistogram();

  double SoW = 0;

  // put equal weight into each bin
  double wpb = nonZeroWeight()/_bincount;
  //errs() << nonZeroWeight() << " / " << _bincount << " = " << wpb << "\n";
  //errs() << wpb << " * " << _bincount << " = " << wpb*_bincount << "\n";
  for(unsigned i = 0, e = bins(); i < e; ++i)
  {
    rc->addToList(getBinCenter(i), wpb);
    SoW += wpb;
  }


  if( fabs(SoW - nonZeroWeight()) > 1.0e-10)
  {
    errs() << "CPHistogram::asUniform: wrong weight: " << SoW << " vs " 
           << nonZeroWeight() << "\n";
  }

  // build the uniform histogram with the correct range and total weight
  rc->buildFromList(_bincount, totalWeight(), _min, _max);

  return(rc);
}

CPHistogram* CPHistogram::asNormal() const
{

  // points are already a "0-stdev normal"
  if(isPoint())
    return(new CPHistogram(*this));

  CPHistogram* rc = new CPHistogram();
  double SoW = 0;

  // we actually have a range-limitted normal, so scale the weight to
  // include the truncated parts
  double phiLB = _stats.phi(_min);  // P(x < LB)
  double phiUB = 1 - _stats.phi(_max);  // P(x > UB)
  double truncated = phiLB + phiUB;
  double adjustedWeight = nonZeroWeight()/(1-truncated);

  //errs() << "N(" << mean() << ", " << stdev()<< "), [" << _min << ", " 
  //       << _max << "] --> phi [" << phiLB << ", " << phiUB << "]\n";
  //errs() << "Truncated: " << truncated << "\n";
  //errs() << "Adjusted: " << adjustedWeight << "\n";

  // put appropriate weight into each bin
  // rotate the phis so we only need to calculate one each iteration
  for(unsigned i = 0, e = bins(); i < e; ++i)
  {
    double phiUB = _stats.phi(getBinUpperLimit(i));  // P(x < UB)
    double proportion = phiUB - phiLB;              // P(LB < x < UB)
    double weight = proportion * adjustedWeight;
    SoW += weight;
    rc->addToList(getBinCenter(i), weight);
    phiLB = phiUB;
  }

  if( fabs(SoW - nonZeroWeight()) > 1.0e-10)
  {
    errs() << "CPHistogram::asNormal: wrong weight: " << SoW << " vs " 
           << nonZeroWeight() << "\n";
  }

  // build the histogram with the correct range and total weight
  rc->buildFromList(_bincount, totalWeight(), _min, _max);

  return(rc);
}


double CPHistogram::earthMover(const CPHistogram& other) const
{
  
  if(isPoint()) return(0);

  double moved = 0;
  //double distance = getBinWidth();
  double dirt = _bins[0] - other._bins[0];

  for(unsigned b = 1; b < _bincount; b++)
  {
    //moved += fabs(dirt) * distance;
    moved += fabs(dirt);
    dirt += (_bins[b] - other._bins[b]);
  }
  // dirt on exit of loop should be 0!

  // divide by bincount ==> distance = 1/bincount (leave it for post-processing)
  return( moved/nonZeroWeight() );

}

CPHistogram* CPHistogram::cross(const CPHistogram& other) const
{

  double tw = _stats.totalWeight * other._stats.totalWeight;
  CPHistogram* rc = new CPHistogram();
  WeightedValue wv;
  unsigned bincount = _bincount;

  // if either histogram has no data, the result also has no data
  if( ! (nonZero() && other.nonZero()) )
  {
    return(rc);
  }

  // Points don't have bins; _min == _max == point values
  if( isPoint() || other.isPoint() )
  {
    if( isPoint() && other.isPoint() )
    {
      wv.first = _min * other._min;
      wv.second = _stats.sumOfWeights*other._stats.sumOfWeights;
      rc->addToList(wv);
    }
    else if( isPoint() )
    {
      bincount = other._bincount;  // we have 0 bins; use other's
      for(unsigned i = 0; i < other._bincount; i++)
      {
        wv.first = _min * other.getBinCenter(i);
        wv.second = _stats.sumOfWeights * other._bins[i];
        rc->addToList(wv);
      }
    }
    else // other.isPoint()
    {
      for(unsigned i = 0; i < _bincount; i++)
      {
        wv.first = getBinCenter(i) * other._min;
        wv.second = _bins[i] * other._stats.sumOfWeights;
        rc->addToList(wv);
      }
    }
  }
  else  // neither is a point
  {
    for(unsigned i = 0; i < _bincount; i++)
      for(unsigned j = 0; j < other._bincount; j++)
      {
        wv.first = getBinCenter(i) * other.getBinCenter(j);
        wv.second = _bins[i] * other._bins[j];
        rc->addToList(wv);
      }
  }

  // we only used midpoints, so get the true range
  double lb = _min*other._min;
  double ub = _max*other._max;

  // drop the weight into the bins
  if( (bincount == 0) && ! (isPoint() && other.isPoint()) )
  {
    errs() << "CPHistogram::cross Error: crossing full histogram but 0 bins\n";
  }
  rc->buildFromList(bincount, tw, lb, ub);

  return(rc);
}


CPHistogram* CPHistogram::cross(const CPHistogramList& others) const
{

  // zero histogram nullifies everything.  Return a zero histogram.
  if( ! nonZero() )
    return(new CPHistogram());

  // scan list for any 0s
  bool hasZero = false;
  CPHistogramList::const_iterator Hist, E;  // we keep E around
  for(Hist = others.begin(), E = others.end(); Hist != E; ++Hist)
  {
    if( ! (*Hist)->nonZero() )
    {
      hasZero = true;
      break;
    }
  }
  if(hasZero)
    return(new CPHistogram());

  // Cross has worst-case complexity _bincount^others.size() when all
  // bins of all histograms contain weight. Never put 0s into the list,
  // because they cause exponential work without any benefit.

  WeightedValueVec invals;
  WeightedValueVec outvals;

  // initialize with this
  //double rw = _stats.sumOfWeights;
  double tw = _stats.totalWeight;
  double min = _min;
  double max = _max;
  if( isPoint() )
  {
    invals.push_back(std::make_pair(_min, _stats.sumOfWeights));
  }
  else
  {
    for(unsigned i = 0; i < _bincount; i++)
    {
      double w = _bins[i];
      if(w != 0)
      {
        double v = getBinCenter(i);
        invals.push_back(std::make_pair(v, w));
      }
    }
  }
    


  for(Hist = others.begin(); Hist != E; ++Hist)
  {
    CPHistogram& H = **Hist;
    
    tw *= H._stats.totalWeight;
    //rw *= H._stats.sumOfWeights;
    min *= H._min;
    max *= H._max;

    if( H.isPoint() )
    {
      double w = H._stats.sumOfWeights;
      double v = H._min;
      for(unsigned i = 0, E = invals.size(); i < E; i++)
      {
        double iv = invals[i].first;
        double iw = invals[i].second;
        outvals.push_back(std::make_pair(iv*v, iw*w));
      }
    }
    else for(unsigned j = 0; j < H._bincount; j++)
    {
      double w = H._bins[j];
      if(w != 0)
      {
        double v = H.getBinCenter(j);
        for(unsigned i = 0, E = invals.size(); i < E; i++)
        {
          double iv = invals[i].first;
          double iw = invals[i].second;
          outvals.push_back(std::make_pair(iv*v, iw*w));
        }
      }
    }
    
    invals.swap(outvals);
  }

  // add any missing 0-value weight
  // !! buildFromList does this anyway !!
  //if(rw < tw)
  //  invals.push_back(std::make_pair(0.0, tw-rw));

  // construct final histogram
  CPHistogram* rc = new CPHistogram();
  for(unsigned i = 0, E = invals.size(); i < E; i++)
    rc->addToList(invals[i]);
  rc->buildFromList(_bincount, tw, min, max);
  
  return(rc);
}



void CPHistogram::setRange(double min, double max)
{
  double tmp;
  if(min > max)
  {
    errs() << "CPHistogram::setRange Warning: minimum > maximum, reversing (" 
           << min << " > " << max << ")\n";
    tmp = min;
    min = max;
    max = tmp;
  }

  // histogram range is strictly non-negative
  if(min < 0) min = 0;
  if(max < 0) max = 0;

  if( (min == 0) && (max != 0) )
    errs() << "Warning: setting lower bound to 0!\n";

  _min = min;
  _max = max;
  //errs() << "(#" << _id << ") range set: [" << min << ", " << max << "]\n";
}


double CPHistogram::occupancy() const 
{
  if(_stats.totalWeight == 0)
    return(0);
  else if(_stats.totalWeight < _bincount)
    return((double)getBinsUsed()/_stats.totalWeight);
  else
    return((double)getBinsUsed()/(double)_bincount);
}


double CPHistogram::coverage() const 
{
  if(_stats.totalWeight == 0) 
    return 0;
  double rc = _stats.sumOfWeights/_stats.totalWeight;
  if(rc < FP_FUDGE_EPS) rc = 0;
  return(rc);
}


double CPHistogram::maxLikelyhood() const 
{
  if(_stats.sumOfWeights == 0) 
    return 0;
  double rc = maxWeight()/_stats.sumOfWeights;
  if(rc < FP_FUDGE_EPS) rc = 0;
  return(rc);
}


double CPHistogram::span() const 
{
  if(!nonZero())
    return(0);
  
  if(isPoint())
    return(0);

  return( (_max-_min)/_max );
}


double CPHistogram::nonZeroWeight() const 
{
  if(_stats.sumOfWeights < FP_FUDGE_EPS)
    return(0);
  else
    return(_stats.sumOfWeights);
}


double CPHistogram::zeroWeight() const 
{
  if(_stats.totalWeight < FP_FUDGE_EPS)
    return(0);
  else
    return (_stats.totalWeight - _stats.sumOfWeights);
}


double CPHistogram::totalWeight() const 
{
  if(_stats.totalWeight < FP_FUDGE_EPS)
    return(0);
  else
    return(_stats.totalWeight);
}


double CPHistogram::maxWeight() const
{
  if(!nonZero())
    return(0);

  if(isPoint())
  {
    return(nonZeroWeight());
  }
  else
  {
    if(_bins == NULL) return(0);

    double max = 0;
    for(unsigned i = 0; i < _bincount; i++)
    {
      if(_bins[i] > max) max = _bins[i];
    }
    return(max);
  }
}

double CPHistogram::min() const
{
  if(!nonZero()) return(0);
	return _min;
}

double CPHistogram::max() const 
{
  if(!nonZero()) return(0);
	return _max;
}

double CPHistogram::getBinWidth() const 
{
  if(!nonZero()) return(0);
  if(isPoint()) return(0);
  assert(_bincount > 0);
	return ((_max - _min)/_bincount);
}

bool CPHistogram::isPoint() const 
{
  if(!nonZero()) return(true);
  return(_min == _max);
}

double CPHistogram::getBinCenter(unsigned b) const 
{
  if(!nonZero()) return(0);
	return _min + getBinWidth()*(b+0.5);
}

double CPHistogram::getBinUpperLimit(unsigned b) const 
{
  if(!nonZero()) return(0);
	return _min + getBinWidth()*(b+1);
}

double CPHistogram::getBinLowerLimit(unsigned b) const 
{
  if(!nonZero()) return(0);
	return _min + getBinWidth()*b;
}


double CPHistogram::mean(bool inclZeros) const
{
  return(_stats.mean(inclZeros));
}

double CPHistogram::stdev(bool inclZeros) const
{
  return(_stats.stdev(inclZeros));
}

// find the value corresponding to: P(X < val) == q
// Ignores 0s
double CPHistogram::quantile(double q) const
{
  if(!nonZero()) return(0);

  if(isPoint())
  {
    //errs() << "CPHistogram::quantile Error: Points don't have quantiles\n";
    //debug
    errs() << " q(" << format("%.2f", q) << ") = " << format("%.2f", _min) << " ";
    return(_min);
  }

  // q outside [0,1] doesn't make any sense
  if( (q < 0) || (q > 1) )
    errs() << "CPHistogram::quantile Error: Quantile out of range [0,1]: " 
           << q << "\n";

  if(q <= 0) return(_min);
  if(q >= 1) return(_max);

  double q2 = q * nonZeroWeight();  // denormalize quantile to weight
  double w = 0;
  unsigned i = 0;

  while( (w + _bins[i]) < q2)
  {
    w += _bins[i++];
  }
  
  double p = (q2-w)/_bins[i];  // proportion of bin weight still needed
  double val = getBinLowerLimit(i) + getBinWidth()*p;

  // debug
  errs() << " q(" << format("%.2f", q) << ") = " << format("%.2f", _min) << " ";

  return(val);
}

// computing lower and upper quantiles points together takes the same
// computation (linear scan of bins) as just calculating the upper.
std::pair<double,double> CPHistogram::quantileRange(double min, double max)
{
  // error range: [-1,-1]
  std::pair<double,double> errRange = std::make_pair(-1,-1);

  if(!nonZero())
  {
    errs() << "CPHistogram::quantileRange Error: empty histograms don't have quantiles\n";
    return(errRange);
  }

  if(isPoint())
  {
    errs() << "CPHistogram::quantileRange Error: Points don't have quantiles\n";
    return(std::make_pair(_min,_min));
  }

  if(min > max)
  {
    errs() << "CPHistogram::quantileRange Error: min > max: (" 
           << min << ", " << max << ")\n";
    return(errRange);
  }

  // q outside [0,1] doesn't make any sense, but we actually deal with
  // it directly when computing the quantile points
  if( (min < 0) || (min > 1) || (max < 0) || (max > 1) )
    errs() << "CPHistogram::quantileRange Truncating invalid range: (" 
           << min << ", " << max << ")\n";

  unsigned i = 0;
  double w = 0;
  double vmin, vmax;

  if(min <= 0)
    vmin = _min;
  else if(min >=1)
    vmin = _max;
  else
  {
    double qmin = min * nonZeroWeight();  // denormalize quantile to weight
    while( (w + _bins[i]) < qmin)
    {
      w += _bins[i++];
    }
    double p = (qmin-w)/_bins[i];  // proportion of last bin's weight needed
    vmin = getBinLowerLimit(i) + getBinWidth()*p;
  }
    
  if(max >= 1)
    vmax = _max;
  else if(max <= 0)
    vmax = _min;
  else
  {
    // we can continue where we left off calculating the min above
    double qmax = max * nonZeroWeight();  // denormalize quantile to weight
    while( (w + _bins[i]) < qmax)
    {
      w += _bins[i++];
    }
    double p = (qmax-w)/_bins[i];  // proportion of last bin's weight needed
    vmax = getBinLowerLimit(i) + getBinWidth()*p;
  }

  return(std::make_pair(vmin,vmax));
}

// find the probability corresponding to: p = P(X < v)
// Ignores 0s
double CPHistogram::probLessThan(double v) const
{
  if(!nonZero()) return(0);
  double rc = getRangeWeight(0, v)/_stats.sumOfWeights;
  if(rc < FP_FUDGE_EPS) rc = 0;
  return(rc);
}

// find the probability corresponding to: p = P(l < X < u)
// Ignores 0s
double CPHistogram::probBetween(double l, double u) const
{
  if(!nonZero()) return(0);
  double rc = getRangeWeight(l, u)/_stats.sumOfWeights;
  if(rc < FP_FUDGE_EPS) rc = 0;
  return(rc);
}


// Estimate of P(this < Y)
// Uses rangeWeight on this vs impulses of Y
// Ignores 0s
double CPHistogram::estProbLessThan(const CPHistogram& Y) const
{
  if( ! (nonZero() && Y.nonZero()) )
  {
    errs() << "CPHistogram::estProbLessThan Error: can't compare empty histograms\n";
    return(0);
  }

  // trivial if no overlap
  if(_max < Y._min) 
    return 1.0;
  if(_min > Y._max)
    return 0.0;

  // 
  double p = 0;
  for(unsigned i = 0; i < Y._bincount; i++)
  {
    // P(X < y && Y = y)
    double y = Y.getBinCenter(i);
    p += probLessThan(y) * (Y._bins[i]/Y._stats.sumOfWeights);
  }

  assert(p >= 0);
  assert(p <= 1);
  return(p);
}



// return index of bin that v falls into
unsigned CPHistogram::whichBin(double v) const 
{
  if(!nonZero())
  {
    errs() << "CPHistogram::whichBin Error: empty histograms don't have bins\n";
    return(0);
  }

  // use an int so that v < _min is negative
  int bin = (int)floor((v-_min) / getBinWidth());

  // point distributions only use bin[0]
  if(isPoint())
  {
    if(v != _min)
      errs() << "(#" << _id << ") warning: value " << v 
             << " is not at point distribution " << _min << "\n";
    return(0);
  }


  if(bin < 0)
  {
    bin = 0;
    if((v-_min) < -FP_FUDGE_EPS)
      errs() << "(#" << _id << ") warning: value " << v << " below range [" 
             << _min << ", " << _max << "]\n";
  }
  else if((unsigned)bin >= _bincount)
  {
    bin = _bincount-1;
    if((v-_max) > FP_FUDGE_EPS)
      errs() << "(#" << _id << ") warning: value " << v << " above range [" 
             << _min << ", " << _max << "]\n";
  }

	return ((unsigned)bin);
}

unsigned CPHistogram::bins() const 
{
	return _bincount;
}

// get number of non-0 bins
unsigned CPHistogram::getBinsUsed() const 
{
  if(!_bins)  // this covers point histograms and empty histograms
    return(0);

	unsigned binsUsed = 0;
	for(unsigned i = 0; i < _bincount; i++ )
		if( _bins[i] > FP_FUDGE_EPS )
			binsUsed++;

	return(binsUsed);
}

// this might be a bit unituitive for point histograms that have no
// bins but still have non-zero weight...
double CPHistogram::getBinWeight(unsigned b) const 
{
  if(_bins)
    return(_bins[b]);
  else
    return(0);
}

// return the total weight in the histogram between lb and ub. If the
// range only partially overlaps a bin range, include weight from that
// bin according to the proportion of overlap.
double CPHistogram::getRangeWeight(double lb, double ub) const 
{

  if(!nonZero()) 
  {
    errs() << "CPHistogram::getRangeWeight Error: empty histograms don't have weight\n";
    return(0);
  }

  // is the range empty?
  if(lb > ub) 
    return(0);
  if( (ub-lb) < FP_FUDGE_EPS )
    return(0);

  // if we're a point distribution, just check the bounds
  if(isPoint())
  {
    if( (lb <= _min) && (ub >= _max) )
      return(nonZeroWeight());
    else
      return(0);
  }

  if(_bins == NULL)
    return(0);

  // Query completely out of range?
	if( ub <= _min || lb >= _max)
		return(0);
		
	// Fit the query range to our range if it's bigger
	if(lb < _min)
		lb = _min;
	if(ub > _max)
		ub = _max;

	double weight = 0;
  double w = 0;
  double bw = getBinWidth();
  double boundary = 0;
	unsigned lbBin = whichBin(lb);
	unsigned ubBin = whichBin(ub);

  // "snap" to bin boundaries within FP_FUDGE_EPS. Must snap both
  // directions so that we don't duplicate/miss any weight if we're
  // walking piecewise over a range (eg, merging histograms)

  bool ubIsBoundary = false;  // did we snap the upper bound?
  bool lbIsBoundary = false;

  // snap up to top of bin
  boundary = getBinUpperLimit(ubBin);
  if(ub > (boundary-FP_FUDGE_EPS) )
  {
    ubIsBoundary = true;
    ub = boundary;
  }
  else
  {
    // snap down to bottom of bin; decrement top bin number
    boundary = getBinLowerLimit(ubBin);
    if(ub < (boundary+FP_FUDGE_EPS) )
    {
      // Don't wrap the bin number!
      if(ubBin > 0)
      {
        ubBin--;
        ub = boundary;
        ubIsBoundary = true;
      }
      else  // query max is less than histogram min
        return(0);
    }
  }

  // ... and similarly for the lower bound...
  // snap to bottom of bin
  boundary = getBinLowerLimit(lbBin);
  if(lb < (boundary+FP_FUDGE_EPS) )
  {
    lbIsBoundary = true;
    lb = boundary;
  }
  else
  {
    // snap up to top of bin; increment bottom bin number
    boundary = getBinUpperLimit(lbBin);
    if(lb > (boundary-FP_FUDGE_EPS) )
    {
      // Don't go past the last bin!
      if(lbBin < _bincount-1)
      {
        lbBin++;
        lb = boundary;
        lbIsBoundary = true;
      }
      else // query min is greater than histogram max
        return(0);
    }
  }
  assert(bw > 0);

  // *** DANGER *** 
  // fp subtraction of very similar values is prone to generating
  // small, unexpectedly negative values.  Check all w for this
  // effect.  We should probably also check that the subtraction did
  // indeed generate a *small* negative value, but we take that for
  // granted.

	// Just take a section out of a single bin
	if( lbBin == ubBin )
  {
    // is it the whole bin, exactly?
    if(lbIsBoundary && ubIsBoundary)
      return(_bins[lbBin]);
    else
    {
      w = (ub-lb)/bw * _bins[lbBin];
      if(w < 0) w = 0;
      return(w);
    }
  }

	// grab the weights from parts of the end bins
  if(lbIsBoundary)  // whole bin
    w = _bins[lbBin];
  else              // partial bin
    w = (getBinUpperLimit(lbBin) - lb)/bw * _bins[lbBin];
  weight += (w < 0) ? 0 : w;

  if(ubIsBoundary)  // whole bin
    w = _bins[ubBin];
  else              // partial bin
    w = (ub - getBinLowerLimit(ubBin))/bw * _bins[ubBin];
  weight += (w < 0) ? 0 : w;

  // and get the weight for any full bins between the ends
	for( unsigned i = lbBin + 1; i < ubBin; i++ )
  {
		weight += _bins[i];
  }

  // the checks above should prevent negative weight here
  assert(weight >= 0);
  return(weight);
}


double CPHistogram::applyOnRange(double min, double max, CPHistFunc F)
{

  errs() << "CPHistogram::applyOnRange [" << format("%.2f", min) << "," 
         << format("%.2f", max) << "]\n";

  if(!nonZero()) return(0);
  
  if(min >= max) return(0);

  // point is all-or-nothing, in-range or not
  if(isPoint())
  {
    if((min <= _min) && (max >= _min))
      return( (*F)(_min, 1.0) );
    else
      return( (*F)(_min, 0.0) );
  }

  unsigned bmin = whichBin(min);
  unsigned bmax = whichBin(max);
  double bw = getBinWidth();
  double nzw = nonZeroWeight();

  // range is within one bin: apply F to midpoint of range and
  // proportion of weight
  if(bmin == bmax)
  {
    double v = (max+min)/2.0;
    double p = (max-min)/bw;
    double w = getBinWeight(bmin)*p/nzw;
    return((*F)(v,w));
  }

  double rc = 0;

  // apply F to proportion of lower bin
  double ub = getBinUpperLimit(bmin);
  double v = (ub+min)/2.0;
  double p = (ub-min)/bw;
  double w = getBinWeight(bmin)*p/nzw;
  rc += (*F)(v,w);
  
  // apply F to middle bins
  for(unsigned b = bmin+1; b < bmax; b++)
  {
    v = getBinCenter(b);
    w = getBinWeight(b)/nzw;
    rc += (*F)(v,w);
  }

  // apply F to proportion of upper bin
  double lb = getBinLowerLimit(bmax);
  v = (lb+max)/2.0;
  p = (max-lb)/bw;
  w = getBinWeight(bmax)*p/nzw;
  rc += (*F)(v,w);

  return(rc);
}


double CPHistogram::applyOnQuantile(double min, double max, CPHistFunc F)
{
  double rc = 0;

  errs() << "CPHistogram::applyOnQuantile [" << format("%.2f", min) << ","
         << format("%.2f", max) << "]\n";

  if(!nonZero()) return(0);

  if(min >= max) return(0);

  if(min < 0) min = 0;
  if(max > 1) max = 1;

  // for a point, apply to the point but weigh by "covered quantile"
  // to prevent double-counting on subsequent queries with different
  // quantile ranges; eg, 0.25-0.75 should only get half the weight
  if(isPoint())
  {
    double p = max - min;
    double w = p*nonZeroWeight();
    return( (*F)(_min,w) );
  }

  std::pair<double,double> range = quantileRange(min, max);

  rc = applyOnRange(range.first, range.second, F);
  return(rc);
}



void CPHistogram::setBinWeight(unsigned b, double w) 
{
  if(isPoint())
    errs() << "warning: setting bin weight on point histogram! (ignored)\n";
  else
  {
    assert(_bins);
    _bins[b] = w;
  }
  //errs() << "(#" << _id << ")[" << b << "] = " << v << "\n";
}

double CPHistogram::addToBin(unsigned b, double w) 
{
  if(isPoint())
    errs() << "warning: adding bin weight on point histogram! (ignored)\n";
  else
  {
    assert(_bins);
    if(b >= _bincount)  // unsigned always >= 0
      errs() << "(#" << _id << ")[" << b << "] : bin out of range!\n";
    else
      _bins[b] += w;
  }
  //if(w != 0)
  //  errs() << "(#" << _id << ")[" << b << "] += " << w << " --> " << _bins[b] << "\n";
  return(_bins[b]);
}


// reset to a point histogram at 0 (no bins)
void CPHistogram::clear()
{
  setBinCount(0);  // no bins
  _stats.clear();  // zero out stats
  // clearList();     // no pending adds 
	_min = 0;        // no range
	_max = 0;
}


void CPHistogram::clearList() {
	_addList.clear();
}

// allocate a new set of bins
void CPHistogram::setBinCount(unsigned n)
{
  if(_bins != NULL)
  {
    free(_bins);
    _bins = NULL;
  }

  _bincount = n;

  if(_bincount > 0)
  {
    _bins = (double*)calloc(_bincount, sizeof(double));
  }
}


// min and max initialize histogram range.  Range will expand to data
// but will not shrink.  Defaults on min/max fit data exactly.
void CPHistogram::buildFromList(unsigned bincount, double totalweight,
                                double min, double max) 
{
  //errs() << "(#" << _id << ")--> BuildFromAddList (" 
  //       << _newFreqs.size() << ")\n";
  
  clear();  // a point histogram at 0

  DEBUG_BFL("addList(" << _addList.size() << "), bins: " << bincount 
            << ", [" << min << ", " << max << "] tw=" << totalweight << "\n");

  if(_addList.size() == 0)
  {
    _stats.totalWeight = totalweight;
    return;
  }

  // The add list (_addList) contains WeightedValue pairs: <val, weight>

  double minVal = min;
  double maxVal = max;
  double weight = 0;
  WeightedValueVec vals;
  vals.reserve(_addList.size());  // max possible size; avoid resizing

  WeightedValueList::iterator WV, E;
  for(WV = _addList.begin(), E = _addList.end(); WV != E; ++WV)
  {
    // We assume all weights and values are positive!
    if( (WV->first > FP_FUDGE_EPS)         // value is not 0 
        && (WV->second > FP_FUDGE_EPS) )   // weight is not 0
    {
      vals.push_back(*WV);
      DEBUG_BFL("  list: " << WV->first << ", " << WV->second << "\n");
      weight += WV->second;
      if(WV->first < minVal) minVal = WV->first;
      if(WV->first > maxVal) maxVal = WV->first;
    }
    else
    {
      DEBUG_BFL("  list: " << WV->first << ", " << WV->second << " == 0\n");
    }
  }

	// Break out if there are no values to add
	if(vals.size() == 0)
			return;

  // compute statistics.  All this happens regardless of point/histogram
  _stats = Stats(vals);

  if( fabs(_stats.sumOfWeights - weight) > FP_FUDGE_EPS)
  {
    errs() << "CPHistogram::buildFromList:  SoW != weight: " 
           << _stats.sumOfWeights << " vs " << weight << "\n";
  }

  // add a weighted 0 if needed.  Weight in list == totalweight.
  if(weight < totalweight)
  {
    DEBUG_BFL("adding " << totalweight - weight << " 0s\n");
    //vals.push_back(std::make_pair(0.0, totalweight-weight));
    _stats.totalWeight += totalweight - weight;
  }

  if( (_stats.totalWeight <= 0) || (fabs(_stats.totalWeight - totalweight) > 1.0e-10) )
  {
    errs() << "CPHistogram::buildFromList: Total weight incorrect: " 
           << _stats.totalWeight << " vs " << totalweight 
           << "(" << _stats.totalWeight - totalweight << ")\n";
    errs() << "(added " << totalweight - weight << " 0s\n";
  }

  // histogram will have data; set the range
  setRange(minVal, maxVal);
  // points don't have bins; everything is handled by range+stats
  if( !isPoint() )
  {
    setBinCount(bincount);
    // Add the new values into the histogram.  Vals has all 0s filtered out.
    for(WeightedValueVec::iterator V = vals.begin(), VE = vals.end(); 
        V != VE; ++V)
      addToBin(whichBin(V->first), V->second);
  }
  
	clearList();
  //errs() << "(#" << _id << ")<-- BuildFromAddList\n";
}

// insert weighted-value pair in the add list
// Filter out true 0 value or 0 weight pairs
void CPHistogram::addToList(const WeightedValue& wv)
{
  if( (wv.first > 0) && (wv.second > 0) )
  {
    DEBUG_LIST("  add: " << wv.first << ", " << wv.second << "\n");
    _addList.push_back(wv);
  }
}

// insert weighted-value pair in the add list
void CPHistogram::addToList(double v, double w) 
{
	_addList.push_back(std::make_pair(v,w));
}

// write binary representation to f
bool CPHistogram::serialize(unsigned ID, FILE* f) const
{
  CPHistogramHeader entry;

  entry.ID = ID;
  entry.sumOfSquares = _stats.sumOfSquares;
  entry.sumOfValues = _stats.sumOfValues;
  entry.sumOfWeights = _stats.sumOfWeights;
  entry.min = _min;
  entry.max = _max;

  if( (_stats.sumOfWeights - _stats.totalWeight) > FP_FUDGE_EPS)
  {
    errs() << "CPHistogram::serialize: SoW: " << _stats.sumOfWeights 
           << ", tw: " << _stats.totalWeight << ", delta = " 
           << _stats.sumOfWeights - _stats.totalWeight << "\n";
  }

  // Set very-nearly-zero FP values to 0
  if(entry.sumOfSquares < FP_FUDGE_EPS)
    entry.sumOfSquares = 0;
  if(entry.sumOfValues < FP_FUDGE_EPS)
    entry.sumOfValues = 0;
  if(entry.sumOfWeights < FP_FUDGE_EPS)
    entry.sumOfWeights = 0;
  if(entry.min < FP_FUDGE_EPS)
    entry.min = 0;
  if(entry.max < FP_FUDGE_EPS)
    entry.max = 0;
  if(_bins != NULL)
    for(unsigned b = 0; b < _bincount; b++)
      if(_bins[b] < FP_FUDGE_EPS) _bins[b] = 0;
  entry.binsUsed = getBinsUsed();


  if( (entry.min == 0) && (entry.max > 0) )
    errs() << "Warning: writing non-point histogram with 0 lower bound: " << ID << "\n";

  if( fwrite(&entry, sizeof(CPHistogramHeader), 1, f) != 1) 
  {
    return(false);
  }

  // no bins for point histogram
  if(isPoint()) return(true);

  // Write data from each bin
  for(unsigned j = 0; j < _bincount; j++ ) 
  {
    // Don't write empty bins
    if(getBinWeight(j) == 0)
      continue;
    
    CPHistogramBin newBin;
    newBin.index  = j;
    newBin.weight = getBinWeight(j);
    
    // write the bin to the file
    if( !fwrite(&newBin, sizeof(CPHistogramBin), 1, f) )
    {
      errs() << "CPHistogram::serialize Error: failed to write bin [" 
             << newBin.index << ", " << newBin.weight << "]\n";
      return(false);
    }
  }
  return(true);
}

// read binary representation from f.  Return the ID, since we don't
// store that internally.
int CPHistogram::deserialize(unsigned bincount, double totalweight, FILE* f)
{
  CPHistogramHeader entry;

  // Read in the header
  if ( fread(&entry, sizeof(CPHistogramHeader), 1, f) != 1)
  {
    return(-1);
  }

  clear();

  _stats.sumOfSquares = entry.sumOfSquares;
  _stats.sumOfValues = entry.sumOfValues;
  _stats.sumOfWeights = entry.sumOfWeights;
  _stats.totalWeight = totalweight;

  if( (_stats.sumOfWeights - totalweight) > FP_FUDGE_EPS)
  {
    errs() << "CPHistogram::deserialize: SoW: " << _stats.sumOfWeights << ", tw: " << totalweight << ", delta = " << _stats.sumOfWeights - totalweight << "\n";
  }

  _min = entry.min;
  _max = entry.max;
  if( (_min == 0) && (_max != 0) )
    errs() << "Warning: read non-point histogram with 0 lower bound: " 
           << entry.ID << "\n";

  if(isPoint())  // points have no bins, we're done
    return(entry.ID);

  // allocate the bins
  setBinCount(bincount);

  // Make sure bins/ binsUsed are reasonable
  if(bincount < entry.binsUsed) 
  {
    errs() << "Error: histogram bin data corrupt: " << (unsigned)entry.binsUsed 
           << " of " << bincount << " bins used!\n";
    return(-1);
  }

  // Get the data for each bin
  for(unsigned b = 0; b < entry.binsUsed; b++)
  {
    CPHistogramBin newBin;

    // Read in the bin
    if( fread(&newBin, sizeof(CPHistogramBin), 1, f) != 1 )
    {
      errs() << "warning: could not read histogram bin entry " << b << "\n";
      return(-1);
    }
    setBinWeight(newBin.index, newBin.weight);
  }
  return(entry.ID);
}


void CPHistogram::print(llvm::raw_ostream& stream) const
{
  stream << "Sums (Val / W:!0+0 / Sq): " 
         << format("%.3f", _stats.sumOfValues) << " / " 
         << format("%.3f", _stats.totalWeight) << ":" 
         << format("%.3f", _stats.sumOfWeights) << "+" 
         << format("%.3f", zeroWeight()) << " / "
         << format("%.3f", _stats.sumOfSquares) << "\n";
  stream << "Range [" << format("%.5f", _min) << ", " << format("%.5f", _max) 
         << "] by " << getBinWidth() << " (" << getBinsUsed() << "/" 
         << _bincount << ")\n";
  
  if(isPoint())
  {
    if(nonZero())
      stream << "point[" << _min << "] " 
             << nonZeroWeight() << "\n";
    else
      stream << "zero\n";
  }
  else
  {
    for( unsigned b = 0; b < _bincount; b++ )
    {
      double w = getBinWeight(b);
      if(w != 0)  // don't clutter output with empty bins!
        stream << "b" << b << " [" << format("%.5f", getBinLowerLimit(b)) 
               << ", " << format("%.5f", getBinUpperLimit(b)) << ") "
               << w << "\n";
    }
  }
}


// P/H Pval Occupancy Coverge maxLikelyhood Span emdU emdN
void CPHistogram::printStats(llvm::raw_ostream& stream) const
{
  if(isPoint())
  {
    // occupancy = 0, ML=1, span = 1, earthmover = 0
    stream << "P\t" << _min << "\t0.0\t" << "\t" << coverage() 
           << "\t1.0\t1\t0\t0";
  }
  else
  {
    CPHistogram* tmpH;

    tmpH = asUniform();
    double emdU = earthMover(*tmpH);
    delete tmpH;
    
    tmpH = asNormal();
    double emdN = earthMover(*tmpH);
    delete tmpH;

    stream << "H\t*\t" << occupancy() << "\t" 
           << coverage() << "\t" << maxLikelyhood() << "\t" << span() << "\t"
           << format("%0.4f", emdU) << "\t" << format("%0.4f", emdN);
  }
}


bool CPHistogram::nonZero() const
{
  return(_stats.sumOfWeights > FP_FUDGE_EPS);
}

double CPHistogram::overlap(const CPHistogram& other, bool includeZero) const
{
  double min = this->min();
  double max = this->max();
  //unsigned bins = this->_bincount;
  double omin = other.min();
  double omax = other.max();
  //unsigned obins = other._bincount;
  double weight;
  double rc = 0;

  // full overlap of two zero histograms, always
  if(!nonZero() && !other.nonZero())
    return(1);

  // require identical bin count
  if( bins() != other.bins() )
  {
    errs() << "overlap: Error: differnt numbers of bins! " << bins() 
           << " vs " << other.bins() << "\n";
    return(0);
  }

  // require identical ranges.  This also catches the case where only
  // one of the histograms is zero.
  if( (min != omin) || (max != omax) )
  {
    errs() << "overlap: Error: range mismatch: [" << min << ", " << max 
           << "] vs [" << omin << ", " << omax << "]\n";
    return(0);
  }

  // require identical weight
  if(totalWeight() != other.totalWeight())
  {
    errs() << "overlap: Error: total weight differs! " << totalWeight() 
           << " vs " << other.totalWeight() << "\n";
    return(0);
  }
  if(nonZeroWeight() != other.nonZeroWeight())
  {
    errs() << "overlap: Error: weight differs! " << nonZeroWeight() 
           << " vs " << other.nonZeroWeight() << "\n";
    return(0);
  }


  if(includeZero)
  {
    weight = totalWeight();
    //oweight = other.totalWeight();
    //double zw = zeroWeight()/weight;
    //double ozw = other.zeroWeight()/oweight;
    double zw = zeroWeight();
    double ozw = other.zeroWeight();
    double overlap = (zw < ozw) ? zw : ozw;
    if(overlap < FP_FUDGE_EPS)
      overlap = 0;
    rc = overlap;
  }
  else
  {
    weight = nonZeroWeight();
    //oweight = other.nonZeroWeight();
  }

  // 0 overlap with empty histogram
  if(weight == 0)
  {
    return(0);
  }

  // points don't overlap unless they are equal
  if(isPoint() || other.isPoint())
  {
    if( (isPoint() && other.isPoint()) && (min == omin) )
    {
      //double w = nonZeroWeight()/weight;
      //double ow = other.nonZeroWeight()/oweight;
      double w = nonZeroWeight();
      double ow = other.nonZeroWeight();
      double overlap = (w < ow) ? w : ow;
      if(overlap < FP_FUDGE_EPS)
        overlap = 0;
      rc += overlap;
    }
    return(rc/weight);
  }

  // We should have two histograms, with:
  //   - same range
  //   - same number of bins
  //   - same weight
  // Therefore, we can just do a bin-by-bin comparison.

  for(unsigned b = 0; b < bins(); b++)
  {
    //double w = getBinWeight(b)/weight;
    //double ow = other.getBinWeight(b)/oweight;
    double w = getBinWeight(b);
    double ow = other.getBinWeight(b);

    double overlap = (w < ow) ? w : ow;
    if(overlap < FP_FUDGE_EPS)
      overlap = 0;
    rc += overlap;
  }

  /*  // OLD boundary-set overlap. This method is SUPER prone to FP
      // inaccuracies! Something similar is needed if we're going to
      // consider cases where range and/or bincount is not
      // equal... but do those cases even make sense?

  // make a set of all bin boundaries over both histograms
  // use a set to eliminate duplicates
  std::set<double> boundarySet;
  boundarySet.insert(min);
  boundarySet.insert(omin);
  for(unsigned b = 0; b < bins; b++)
    boundarySet.insert(getBinUpperLimit(b));
  for(unsigned b = 0; b < obins; b++)
    boundarySet.insert(other.getBinUpperLimit(b));

  // put the boundaries into a vector and sort them
  std::vector<double> boundaries;
  for(std::set<double>::iterator B = boundarySet.begin(), E = boundarySet.end();
      B != E; B++)
    boundaries.push_back(*B);

  sort(boundaries.begin(), boundaries.end());

  //errs() << "boundaries: " << boundaries.size() << "\n";
  if(boundaries.size() > bins+1)
  {
    errs() << "Boundary mismatches:  " << boundaries.size() << " Ranges:";
    for(unsigned i = 0; i < boundaries.size(); i++)
      errs() << boundaries[i] << " ";
    errs() << "\n";
  }
  
  // find the overlap on each range between boundaries
  for(unsigned i = 1; i < boundaries.size(); i++)
  {
    double overlap;
    double lower = boundaries[i-1];
    double upper = boundaries[i];
    double w = getRangeWeight(lower, upper)/weight;
    double ow = other.getRangeWeight(lower,upper)/oweight;

    // overlap = min of weights in range
    if(w < ow) overlap = w;
    else overlap = ow;
    rc += overlap;

    //errs() << "  [" << lower << ", " << upper << "]: " << overlap << "\n";
  }

  */  // OLD boundary-set overlap

  // normalize at the end!
  rc = rc/weight;

  if(rc > 1.0)
  {
    if( rc > (1.0 + FP_FUDGE_EPS) )
      errs() << "Error: overlap >1: " << rc << "\n";
    rc = 1.0;
  }

  return(rc);
}



/******************* Stats inner class ********************/


CPHistogram::Stats::Stats(const WeightedValueVec& vals) :
  sumOfSquares(0), sumOfValues(0), sumOfWeights(0), totalWeight(0)
{
  unsigned count = vals.size();

  if(count == 0)  // nothing to do with an empty list
    return;

  // Compute sums of weights, weighted values
  for(unsigned i = 0; i < count; i++)
  {
    if(vals[i].second == 0) // 0 weight
      continue;

    totalWeight += vals[i].second;

    if(vals[i].first != 0)  // ignore weights of 0 values
    {
      sumOfWeights += vals[i].second;
      sumOfValues += vals[i].first * vals[i].second;  // value*weight
    }
  }    

	// Compute the weighted sum of squared deviations
  double mean = sumOfValues/sumOfWeights;
	for(unsigned i = 0; i < count; i++) 
  {
    if(vals[i].second == 0) // 0 weight
      continue;

		double delta = vals[i].first - mean;
		sumOfSquares += delta*delta*vals[i].second;
	}

  if( (sumOfWeights - totalWeight) > 1.0e-10)
    errs() << "Bad Stats: weight: " << sumOfWeights 
           << ", total: " << totalWeight << " (" << sumOfWeights - totalWeight << ")\n";
  //print(errs());
}


void CPHistogram::Stats::combineStats(const Stats& s2)
{
  // check for div-by-0 hazards
  if(s2.totalWeight == 0)
    return;

  // just copy if we have no data yet
  if(totalWeight == 0)
  {
    *this = s2;
    return;
  }

  sumOfValues += s2.sumOfValues;
  sumOfWeights += s2.sumOfWeights;
  totalWeight += s2.totalWeight;
  
  if( fabs(sumOfWeights - totalWeight) > 1.0e-10)
  {
    errs() << "Stats::combineStats: SoW: " << sumOfWeights << ", tw: " 
           << totalWeight << ", delta = " << sumOfWeights - totalWeight << "\n";
  }


  // SS = SSa + SSb + ( na*nb/(na+nb) * (Sa/na - Sb/nb)^2 )
  
  // PB: don't include 0s until calc of stdev
  double na = sumOfWeights; //totalWeight;
  double nb = s2.sumOfWeights; //s2.totalWeight;

  //double SSa = sumOfSquares;
  double SSb = s2.sumOfSquares;
  double Sa = sumOfValues;
  double Sb = s2.sumOfValues;

  double delta = Sa/na - Sb/nb;
  sumOfSquares += SSb + ( na*nb/(na+nb) * delta*delta );

  if(sumOfWeights > totalWeight)
    errs() << "Bad Stats: weight: " << sumOfWeights 
           << ", total: " << totalWeight << "\n";
  //print(errs());
}

CPHistogram::Stats& CPHistogram::Stats::operator=(const CPHistogram::Stats& s)
{
  if(this == &s) return(*this);
  sumOfSquares = s.sumOfSquares;
  sumOfValues = s.sumOfValues;
  sumOfWeights = s.sumOfWeights;
  totalWeight = s.totalWeight;
  return(*this);
}

double CPHistogram::Stats::mean(bool inclZeros) const 
{ 
  if(sumOfWeights == 0)
    return(0);

  if(inclZeros)
    return(sumOfValues/totalWeight);
  else
    return(sumOfValues/sumOfWeights);
}

double CPHistogram::Stats::stdev(bool inclZeros) const 
{
  if(sumOfWeights == 0)
    return(0);

  if(!inclZeros)
  {
    return(sqrt(sumOfSquares/sumOfWeights));
  }
  else
  {
    // add in the 0s to sum of squared deviations
    double zeros = totalWeight - totalWeight;

		double delta = sumOfValues/sumOfWeights;    // 0 - mean == mean
		double ss0 = sumOfSquares + delta*delta*zeros;

    return(sqrt(ss0/totalWeight));
  }

}


void CPHistogram::Stats::print(llvm::raw_ostream& stream)
{
  stream << "v=" << sumOfValues << ", T=" << totalWeight << ", w=" 
         << sumOfWeights << ", s=" << sumOfSquares << "\n";
}

// From: http://www.johndcook.com/cpp_phi.html  (public domain)
// Stand-alone C++ implementation of Φ(x)
// Modified that implementation to use the mean and stdev of this Stats 
// object instead of assuming standard normal.
double CPHistogram::Stats::phi(double x) const
{

  // PB: check that this Stats isn't empty
  if(sumOfWeights == 0)
    return(0);

  // PB: auto-normalization here:  (all x below changed to z)
  double z = (x - mean())/stdev();


  // constants
  double a1 =  0.254829592;
  double a2 = -0.284496736;
  double a3 =  1.421413741;
  double a4 = -1.453152027;
  double a5 =  1.061405429;
  double p  =  0.3275911;
  
  // Save the sign of z
  int sign = 1;
  if (z < 0)
    sign = -1;
  z = fabs(z)/sqrt(2.0);
  
  // A&S formula 7.1.26
  double t = 1.0/(1.0 + p*z);
  double y = 1.0 - (((((a5*t + a4)*t) + a3)*t + a2)*t + a1)*t*exp(-z*z);
  
  return 0.5*(1.0 + sign*y);
}
