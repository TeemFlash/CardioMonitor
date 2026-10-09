#include "../MedicalMonitor/SignalProcessing.h"
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

  // Rapid deliberate taps must all register, without a second "unlock" press.
  for(int key=0;key<4;++key) {
    DebouncedButton b;int events=0;
    for(uint32_t t=0;t<1000;++t)events+=b.update(t%100<50,t);
    assert(events==10);
  }
  DebouncedButton b;
  assert(!b.update(true,100));assert(!b.update(false,110));
  assert(!b.update(true,120));assert(!b.update(true,139));assert(b.update(true,140));
  for(uint32_t t=141;t<2000;++t)assert(!b.update(true,t));
  assert(!b.update(false,2000));assert(!b.update(false,2024));
  assert(!b.update(true,2024));assert(!b.update(true,2100)); // brief low glitch
  assert(!b.update(false,2200));assert(!b.update(false,2225));
  assert(!b.update(true,2250));assert(b.update(true,2270));
  DebouncedButton adc;
  assert(!adc.update(true,10));assert(adc.update(true,30));
  assert(!adc.update(false,100,false));assert(!adc.update(false,1000,false));
  assert(!adc.update(true,1100));assert(!adc.update(true,1200));
  assert(!adc.update(false,1300));assert(!adc.update(false,1325));
  assert(!adc.update(true,1350));assert(adc.update(true,1370));
  DebouncedButton repeat;
  assert(!repeat.update(true,0,true,true));assert(repeat.update(true,20,true,true));
  for(uint32_t t=21;t<520;++t)assert(!repeat.update(true,t,true,true));
  assert(repeat.update(true,520,true,true));
  assert(!repeat.update(true,739,true,true));assert(repeat.update(true,740,true,true));
  assert(repeat.update(true,1520,true,true));
  assert(!repeat.update(true,1639,true,true));assert(repeat.update(true,1640,true,true));
  assert(repeat.update(true,3020,true,true));
  assert(!repeat.update(true,3079,true,true));assert(repeat.update(true,3080,true,true));
  assert(!repeat.update(true,3080,true,true)); // no burst after slow rendering
  assert(!repeat.update(false,3140,true,true)); // repeat stops at first raw release
  assert(!repeat.update(false,3165,true,true));
  assert(!repeat.update(true,3200,true,true));assert(repeat.update(true,3220,true,true));
  assert(!repeat.update(true,3500,true,false)); // leave keyboard while held
  assert(!repeat.update(true,4000,true,true)); // entering it again cannot arm repeat
  DebouncedButton wrap;
  assert(!wrap.update(true,UINT32_MAX-10));
  assert(!wrap.update(true,8));assert(wrap.update(true,9));
  assert(!wrap.update(false,40));assert(!wrap.update(false,65));
  assert(!wrap.update(true,80));assert(wrap.update(true,100));

  assert(estimateBatteryPercent(0)==-1); assert(estimateBatteryPercent(4.2f)==100);
  assert(estimateBatteryPercent(3.3f)==0); assert(estimateBatteryPercent(NAN)==-1);
  puts("signal, validity, reset, ECG, debounce and battery tests passed");
}
