#pragma once
#include <stdint.h>
#include <math.h>

// Monitoring filters, not diagnostic ECG calibration. Fixed 250 Hz coefficients
// are fed by a timestamp-based interpolator; raw ADC samples remain in the CSV.
struct EcgBiquad {
  float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
  void configure(float frequency, float q, int type) {
    float w=6.28318530718f*frequency/250.0f, c=cosf(w), alpha=sinf(w)/(2*q);
    float a0=1+alpha;
    if(type==0) { b0=(1-c)/2; b1=1-c; b2=b0; } // low pass
    else if(type==1) { b0=(1+c)/2; b1=-(1+c); b2=b0; } // high pass
    else { b0=1; b1=-2*c; b2=1; } // notch
    b0/=a0; b1/=a0; b2/=a0; a1=-2*c/a0; a2=(1-alpha)/a0;
    z1=z2=0;
  }
  float process(float x) {
    float y=b0*x+z1;
    z1=b1*x-a1*y+z2; z2=b2*x-a2*y;
    return y;
  }
};
struct EcgFilter {
  EcgBiquad high,notch,low;
  uint8_t mains=50;
  EcgFilter() { configure(50); }
  void configure(uint8_t hz) {
    mains=(hz==50 || hz==60)?hz:0;
    high.configure(0.5f,0.70710678f,1);
    low.configure(40,0.70710678f,0);
    notch.configure(mains?mains:50,20,2);
  }
  float process(float x) {
    float y=high.process(x);
    if(mains) y=notch.process(y);
    return low.process(y);
  }
};

struct EcgBeatDetector {
  EcgFilter filter;
  float baseline=0, filtered=0, envelope=20, bpm=0, previousRaw=0;
  uint32_t lastBeat=0,lastSample=0,start=0,nextUniform=0,previousInterval=0;
  int intervals=0,crossings=0,polarity=0;
  uint32_t crossingWindow=0;
  bool noisy=false;
  bool initialized=false,armed=true,quality=false;
  uint8_t mains=50;
  void reset() {
    baseline=filtered=previousRaw=bpm=0; envelope=20;
    lastBeat=lastSample=start=nextUniform=previousInterval=0;
    intervals=crossings=polarity=0; crossingWindow=0; noisy=false;
    initialized=quality=false; armed=true;
    filter.configure(mains);
  }
  void setMains(uint8_t hz) { mains=(hz==50 || hz==60)?hz:0; reset(); }
  void expire(uint32_t now) {
    if(initialized && now-lastSample>100) reset();
    if(lastBeat && now-lastBeat>2500) { bpm=0; intervals=0; }
  }
  void acceptUniform(float raw,uint32_t now) {
    filtered=filter.process(raw-baseline);
    float amplitude=fabsf(filtered);
    envelope=fmaxf(amplitude,envelope*0.996f);
    // Settling/contact heuristic only: an in-range voltage cannot prove contact.
    // Excessive hysteretic zero crossings indicate residual mains/EMG noise.
    // This is a conservative engineering gate, not an electrode-contact test.
    if(filtered>8 && polarity!=1) { ++crossings; polarity=1; }
    if(filtered<-8 && polarity!=-1) { ++crossings; polarity=-1; }
    if(now-crossingWindow>=1000) {
      noisy=crossings>24; crossings=0; crossingWindow=now;
    }
    quality=now-start>=3000 && envelope>12 && !noisy;
    if(!quality) { bpm=0; intervals=0; return; }
    float threshold=fmaxf(12,envelope*0.6f);
    if(armed && amplitude>threshold && (!lastBeat || now-lastBeat>300)) {
      if(lastBeat) {
        uint32_t interval=now-lastBeat;
        float rate=60000.0f/interval;
        // Avoid presenting a stable number from erratic detections. This also
        // withholds HR in some real irregular rhythms; it is not arrhythmia detection.
        bool consistent=!previousInterval || fabsf(float(interval)-previousInterval)<0.25f*previousInterval;
        if(rate>=40 && rate<=200 && consistent) {
          ++intervals;
          if(intervals>=2) bpm=bpm>0?bpm*0.7f+rate*0.3f:rate;
        } else { intervals=0; bpm=0; }
        previousInterval=interval;
      }
      lastBeat=now; armed=false;
    }
    if(amplitude<threshold*0.45f) armed=true;
    if(lastBeat && now-lastBeat>2500) { bpm=0; intervals=0; }
  }
  void update(float raw,uint32_t now) {
    if(!isfinite(raw) || raw<150 || raw>3950) { reset(); return; }
    if(initialized && now-lastSample>12) reset(); // Do not bridge missing waveform.
    if(!initialized) {
      initialized=true; baseline=previousRaw=raw; lastSample=start=now;
      nextUniform=now+4; crossingWindow=now; return;
    }
    uint32_t elapsed=now-lastSample;
    if(!elapsed) return;
    // At most three interpolated samples; no unbounded catch-up after a stall.
    while(int32_t(now-nextUniform)>=0) {
      float fraction=float(nextUniform-lastSample)/elapsed;
      acceptUniform(previousRaw+(raw-previousRaw)*fraction,nextUniform);
      nextUniform+=4;
    }
    previousRaw=raw; lastSample=now;
  }
};

// Agreement compares independent, fresh HR estimates; it never edits the ECG
// or averages two disagreeing instruments into a deceptively precise number.
enum class HeartRateAgreement { Unavailable, Consistent, Different };
inline HeartRateAgreement compareHeartRates(float ecg,bool ecgValid,float optical,bool opticalValid) {
  if(!ecgValid || !opticalValid || !isfinite(ecg) || !isfinite(optical) || ecg<=0 || optical<=0)
    return HeartRateAgreement::Unavailable;
  float tolerance=fmaxf(8,0.1f*ecg); // Engineering threshold, not a clinical criterion.
  return fabsf(ecg-optical)<=tolerance?HeartRateAgreement::Consistent:HeartRateAgreement::Different;
}

// The screen advances at 100 pixels/second from acquisition time, regardless
// of frame rate. Retain a bounded queue of extrema so short QRS peaks survive.
struct EcgPlotColumn { uint32_t bin; float low,high; bool valid; };
struct EcgPlotBuffer {
  static constexpr unsigned Capacity=128;
  EcgPlotColumn columns[Capacity]{};
  unsigned head=0,count=0;
  uint32_t origin=0,bin=0;
  float low=0,high=0;
  bool active=false,valid=false;
  void reset(uint32_t start) { origin=start; head=count=0; active=false; }
  void push(uint32_t t,float v,bool good) {
    uint32_t next=(t-origin)/10;
    if(!active) { active=true; bin=next; low=high=v; valid=good; return; }
    if(next==bin) { low=fminf(low,v); high=fmaxf(high,v); valid=valid && good; return; }
    if(count==Capacity) { head=(head+1)%Capacity; --count; }
    columns[(head+count)%Capacity]={bin,low,high,valid}; ++count;
    bin=next; low=high=v; valid=good;
  }
  bool pop(EcgPlotColumn& out) {
    if(!count) return false;
    out=columns[head]; head=(head+1)%Capacity; --count; return true;
  }
};
