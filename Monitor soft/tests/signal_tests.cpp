#include "../SignalProcessing.h"
#include <cassert>
#include <cstdio>
#include <random>

void rfTest(uint32_t* ir, uint32_t* red, int count, bool expected) {
  float spo2=42, ratio=42, corr=42;
  int32_t hr=42; int8_t hv=1,sv=1;
  rf_reset_state();
  rf_heart_rate_and_oxygen_saturation(ir,count,red,&spo2,&sv,&hr,&hv,&ratio,&corr);
  assert(bool(hv)==expected);
  assert(isfinite(ratio) && isfinite(corr));
  if(!expected) assert(!sv && hr==-999 && spo2==-999);
  else { assert(hr>=70 && hr<=80); assert(sv && spo2>=90 && spo2<=100); }
}
int main() {
  uint32_t ir[BUFFER_SIZE],red[BUFFER_SIZE];
  for(int i=0;i<BUFFER_SIZE;++i) ir[i]=red[i]=100000;
  rfTest(ir,red,BUFFER_SIZE,false);
  rfTest(ir,red,0,false); rfTest(ir,red,BUFFER_SIZE-1,false); rfTest(ir,red,BUFFER_SIZE+1,false);
  for(int i=0;i<BUFFER_SIZE;++i) {
    double wave=sin(2*3.141592653589793*i/20.0);
    ir[i]=100000+3000*wave; red[i]=90000+1500*wave;
  }
  rfTest(ir,red,BUFFER_SIZE,true);
  std::mt19937 gen(42);
  for(int i=0;i<BUFFER_SIZE;++i) { ir[i]=100000+gen()%6000; red[i]=90000+gen()%6000; }
  rfTest(ir,red,BUFFER_SIZE,false);

  PulseOxProcessor p;
  for(int i=0;i<150;++i) {
    double wave=sin(2*3.141592653589793*i/20.0);
    p.update(100000+3000*wave,90000+1500*wave,1000+i*40);
    if(i<99) assert(!p.hrValid && !p.spo2Valid);
  }
  assert(p.hrValid && p.spo2Valid && p.beatAvg==75);
  p.update(0,0,7000); assert(!p.finger && !p.hrValid && !p.spo2Valid && p.spo2==0);
  p.update(260000,90000,7040); assert(!p.hrValid && p.count==0);
  p.update(100000,90000,7080); assert(p.count==1);
  p.expire(7500); assert(p.count==0 && !p.finger);
  for(int i=0;i<300;++i) p.update(100000,90000,8000+i*40);
  assert(!p.hrValid && !p.spo2Valid);

  EcgBeatDetector ecg;
  for(int i=0;i<1000;++i) ecg.update(2048,1000+i*4);
  assert(!ecg.quality && ecg.bpm==0);
  for(int i=0;i<3000;++i) {
    int phase=i%200;
    int raw=2048+(phase<6 ? 800 : 0);
    ecg.update(raw,5000+i*4);
  }
  assert(ecg.bpm>70 && ecg.bpm<80);
  ecg.update(4095,17000); assert(!ecg.quality && ecg.bpm==0);
  ecg.update(2048,17004); ecg.expire(17500); assert(!ecg.initialized);

  DebouncedButton b;
  assert(!b.update(true,100)); assert(!b.update(false,110));
  assert(!b.update(true,120)); assert(!b.update(true,140));
  assert(b.update(true,155)); assert(!b.update(true,170));
  assert(!b.update(true,800)); assert(b.update(true,810,true));
  assert(!b.update(false,900)); assert(!b.update(false,935));
  assert(estimateBatteryPercent(0)==-1); assert(estimateBatteryPercent(4.2f)==100);
  assert(estimateBatteryPercent(3.3f)==0); assert(estimateBatteryPercent(NAN)==-1);
  puts("signal, validity, reset, ECG, debounce and battery tests passed");
}
