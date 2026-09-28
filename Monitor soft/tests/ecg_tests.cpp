#include "../MedicalMonitor/EcgProcessing.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

static constexpr double Pi=3.141592653589793;
double response(float frequency,uint8_t mains) {
  EcgFilter f; f.configure(mains);
  double square=0; int count=0;
  for(int i=0;i<10000;++i) {
    float y=f.process(1000*sin(2*Pi*frequency*i/250));
    assert(isfinite(y));
    if(i>=5000) { square+=double(y)*y; ++count; }
  }
  return sqrt(square/count)/(1000/sqrt(2));
}
float ecgSignal(float seconds,float rate) {
  float period=60/rate;
  float phase=fmodf(seconds,period);
  float r=(phase-0.2f)/0.014f, t=(phase-0.4f)/0.06f;
  return 600*expf(-r*r/2)+70*expf(-t*t/2);
}
int main() {
  double pass=response(10,50), notch50=response(50,50), notch60=response(60,60);
  double drift=response(0.1f,50), high=response(80,0);
  assert(pass>0.95 && pass<1.05);
  assert(notch50<response(50,0)*0.018); // >35 dB additional suppression at exact mains frequency.
  assert(notch60<response(60,0)*0.018);
  assert(drift<0.05); assert(high<0.2);
  printf("ECG filter gains: 10Hz %.3f, 0.1Hz %.4f, 50Hz %.7f, 60Hz %.7f, 80Hz %.3f\n",
         pass,drift,notch50,notch60,high);
  for(float rate:{60.0f,75.0f,120.0f}) {
    for(float sign:{-1.0f,1.0f}) {
      EcgBeatDetector e;
      // Deterministic +/-1 ms scheduling jitter, drift, and a strong 50 Hz contaminant.
      const int jitter[]={0,1,0,-1};
      for(int i=0;i<7500;++i) {
        uint32_t ms=1000+i*4+jitter[i%4];
        float t=(ms-1000)/1000.0f;
        float raw=2048+sign*ecgSignal(t,rate)+100*sin(2*Pi*0.15*t)+250*sin(2*Pi*50*t);
        e.update(raw,ms);
      }
      printf("synthetic noisy ECG %.0f bpm polarity %.0f -> %.1f bpm\n",rate,sign,e.bpm);
      assert(e.quality && fabsf(e.bpm-rate)<5);
      e.update(2048,32000); assert(!e.quality && e.bpm==0);
      e.update(4095,32004); assert(!e.initialized && e.bpm==0);
    }
  }
  for(float frequency:{49.0f,50.0f,60.0f,80.0f}) {
    EcgBeatDetector pureNoise;
    for(int i=0;i<5000;++i) pureNoise.update(2048+500*sin(2*Pi*frequency*i/250),1000+i*4);
    assert(pureNoise.bpm==0 && !pureNoise.quality);
  }
  EcgBeatDetector mode;
  mode.setMains(60);mode.reset();assert(mode.mains==60 && mode.filter.mains==60);
  mode.setMains(99);assert(mode.mains==0);
  assert(compareHeartRates(75,true,76,true)==HeartRateAgreement::Consistent);
  assert(compareHeartRates(75,true,120,true)==HeartRateAgreement::Different);
  assert(compareHeartRates(75,true,76,false)==HeartRateAgreement::Unavailable);
  assert(compareHeartRates(0,true,76,true)==HeartRateAgreement::Unavailable);

  EcgPlotBuffer plot; plot.reset(1000);
  plot.push(1000,0,true);plot.push(1004,800,true);plot.push(1008,-100,true);plot.push(1012,0,true);
  EcgPlotColumn c;assert(plot.pop(c));assert(c.bin==0 && c.high==800 && c.low==-100);
  plot.push(1050,0,true);assert(plot.pop(c));assert(c.bin==1);
  plot.push(1060,0,false);assert(plot.pop(c));assert(c.bin==5); // gap is not concealed
  for(int i=0;i<1000;++i) plot.push(2000+i*10,i,true);
  assert(plot.count==EcgPlotBuffer::Capacity);
  uint32_t last=0;
  while(plot.pop(c)){assert(c.bin>=last);last=c.bin;}
  assert(last==1098);
  puts("ECG interference, jitter, polarity, gap, comparison and waveform buffer tests passed");
}
