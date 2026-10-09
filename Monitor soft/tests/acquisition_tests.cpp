// Execute the real acquisition loop against deterministic I2C devices.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <cmath>
using std::size_t;
#define ADS1115_ADDR 0x48
#define ADS_CH_ECG 0
#define ADS_CH_BATTERY 1
#define ADS_CH_BTN_BACK 2
#define BATTERY_DIVIDER_RATIO 2.0f
#define I2C_SPEED_FAST 400000
uint64_t simulatedUs=0;
uint32_t millis(){return simulatedUs/1000;}
uint32_t micros(){return simulatedUs;}
bool adcUnplugged=false;
bool maxUnplugged=false;
int adcConversions[3]={};
enum class TestCase { stable, weak, fixed, enable, failedWrite, mismatch, session };
TestCase activeCase=TestCase::stable;
uint32_t stopMs=7000,failureMs=0;
struct FakeWire {
  uint8_t address=0,reg=0;
  std::vector<uint8_t> tx,rx;
  uint16_t config=0;
  uint64_t ready=0;
  uint8_t writePtr=0,readPtr=0;
  std::array<uint8_t,256> registers{};
  std::deque<std::array<uint32_t,2>> fifo;
  uint64_t nextPpg=0;
  bool injectedFailure=false;
  struct LedWrite { uint32_t ms;uint8_t reg,value; };
  std::vector<LedWrite> ledWrites;
  void updateFifo(){
    if(registers[9]!=3 || maxUnplugged){nextPpg=simulatedUs+40000;return;}
    while(nextPpg<=simulatedUs){
      const bool weak=activeCase!=TestCase::stable;
      // The optical level responds to actual hardware current registers.
      float wave=1.0f+0.01f*std::sin(float(nextPpg)*0.0000062831853f);
      uint32_t red=uint32_t((weak?30000.0f:90000.0f)*registers[0x0c]/60.0f*wave);
      uint32_t ir=uint32_t((weak?40000.0f:100000.0f)*registers[0x0d]/60.0f*wave);
      fifo.push_back({std::min(red,262143u),std::min(ir,262143u)});
      writePtr=(writePtr+1)&31;
      if(fifo.size()>31){fifo.pop_front();readPtr=(readPtr+1)&31;registers[5]=1;}
      nextPpg+=40000;
    }
  }
  void clearFifo(){fifo.clear();readPtr=writePtr=0;registers[5]=0;nextPpg=simulatedUs+40000;}
  void beginTransmission(uint8_t a){address=a;tx.clear();}
  void write(uint8_t a){tx.push_back(a);}
  int endTransmission(bool=true){
    simulatedUs+=60;
    if(address==0x48 && adcUnplugged) return 2;
    if(address==0x57 && maxUnplugged) return 2;
    reg=tx[0];
    if(address==0x48 && tx.size()==3){
      config=(uint16_t(tx[1])<<8)|tx[2];ready=simulatedUs+1250;
      ++adcConversions[((config>>12)&7)-4];
    }
    if(address==0x57 && tx.size()>=2){
      updateFifo();
      if(activeCase==TestCase::failedWrite && !injectedFailure && reg==0x0d && tx[1]>60){
        injectedFailure=true;failureMs=millis();return 2;
      }
      for(size_t i=1;i<tx.size();++i){
        uint8_t target=reg+uint8_t(i-1),value=tx[i];
        registers[target]=value;
        if(target==0x0c || target==0x0d) ledWrites.push_back({millis(),target,value});
        if(target==4 || target==5 || target==6) clearFifo();
        if(target==9) nextPpg=simulatedUs+40000;
      }
    }
    return 0;
  }
  size_t requestFrom(uint8_t a,size_t n){
    simulatedUs+=60;rx.clear();
    if((a==0x48 && adcUnplugged)||(a==0x57 && maxUnplugged))return 0;
    if(a==0x48){
      uint16_t value=0;
      if(reg==1) value=(config&0x7fff)|(simulatedUs>=ready?0x8000:0);
      if(reg==0){int channel=((config>>12)&7)-4;value=channel==0?13200:(channel==1?15200:26400);}
      rx={uint8_t(value>>8),uint8_t(value)};
    } else if(a==0x57){
      updateFifo();
      if(reg==0xff)rx={0x15};
      else if(reg==4)rx={writePtr,registers[5],readPtr};
      else if(reg==7){
        assert(!fifo.empty());auto values=fifo.front();fifo.pop_front();readPtr=(readPtr+1)&31;
        for(uint32_t value:values){rx.push_back(uint8_t(value>>16));rx.push_back(uint8_t(value>>8));rx.push_back(uint8_t(value));}
      } else {
        for(size_t i=0;i<n;++i)rx.push_back(registers[uint8_t(reg+i)]);
        if(activeCase==TestCase::mismatch && !injectedFailure && reg==0x0c && registers[0x0d]>60){
          injectedFailure=true;failureMs=millis();rx[1]^=1;
        }
      }
    }
    return std::min(n,rx.size());
  }
  int read(){int v=rx.front();rx.erase(rx.begin());return v;}
} Wire;
struct FakePulse {
  bool begin(FakeWire&,int){return true;}
  void setup(int level,int average,int mode,int rate,int width,int range){
    assert(average==4 && mode==2 && rate==100 && width==411 && range==4096);
    Wire.registers[8]=0x5f;Wire.registers[9]=3;Wire.registers[0x0a]=0x27;
    Wire.registers[0x0c]=Wire.registers[0x0d]=level;clearFIFO();
  }
  void clearFIFO(){Wire.clearFifo();}
} pulseSensor;
#include "../MedicalMonitor/Acquisition.h"

struct FakeQueue{size_t capacity,itemSize;std::deque<std::vector<uint8_t>> items;};
QueueHandle_t xQueueCreate(unsigned count,unsigned size){return new FakeQueue{count,size,{}};}
int xQueueSend(QueueHandle_t h,const void* item,unsigned){
  auto q=static_cast<FakeQueue*>(h);if(q->items.size()==q->capacity)return 0;
  auto b=static_cast<const uint8_t*>(item);q->items.emplace_back(b,b+q->itemSize);return 1;
}
int xQueueOverwrite(QueueHandle_t h,const void* item){
  auto q=static_cast<FakeQueue*>(h);q->items.clear();return xQueueSend(h,item,0);
}
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*){return 1;}
std::vector<SensorSample> received;
struct HealthAt {uint32_t ms;SensorHealth state;};
std::vector<HealthAt> healthReceived;
void vTaskDelay(unsigned){
  auto q=static_cast<FakeQueue*>(sampleQueue);
  while(!q->items.empty()){
    SensorSample s;std::memcpy(&s,q->items.front().data(),sizeof(s));q->items.pop_front();received.push_back(s);
  }
  auto h=static_cast<FakeQueue*>(healthQueue);
  if(!h->items.empty()){
    SensorHealth state;std::memcpy(&state,h->items.front().data(),sizeof(state));
    healthReceived.push_back({millis(),state});h->items.clear();
  }
  adcUnplugged=activeCase==TestCase::stable && millis()>=3000 && millis()<4000;
  maxUnplugged=activeCase==TestCase::stable && millis()>=4000 && millis()<5000;
  if(activeCase==TestCase::enable && millis()>=3000)ppgAutoEnabled.store(true);
  if(activeCase==TestCase::session && millis()>=8000)sensorCommand.store(9);
  simulatedUs+=1000;
  if(millis()>stopMs) throw std::runtime_error("done");
}
void run(TestCase test,uint32_t duration=10000){
  activeCase=test;stopMs=duration;simulatedUs=0;failureMs=0;Wire=FakeWire{};
  adcUnplugged=maxUnplugged=false;std::fill(std::begin(adcConversions),std::end(adcConversions),0);
  received.clear();healthReceived.clear();
  ppgAutoEnabled.store(test!=TestCase::fixed && test!=TestCase::enable);
  startAcquisition();sensorCommand.store(5);
  try{acquisitionTask(nullptr);}catch(const std::runtime_error&){}
  delete static_cast<FakeQueue*>(sampleQueue);delete static_cast<FakeQueue*>(healthQueue);
  sampleQueue=healthQueue=nullptr;
}
void assertEpochsAndSettling(bool autoInitially=true){
  uint16_t epoch=0;uint32_t configuredAt=0;bool withheld=false;
  for(const auto& s:received){
    if(s.kind==4){
      assert(s.exposureEpoch>epoch);epoch=s.exposureEpoch;configuredAt=s.ms;withheld=false;
      assert(s.a>=ppg::AutoExposure::Min && s.a<=ppg::AutoExposure::Max);
      assert(s.b>=ppg::AutoExposure::Min && s.b<=ppg::AutoExposure::Max);
    }
    if(s.kind==1 || s.kind==3){
      assert(epoch>0 && s.exposureEpoch==epoch);
      if(s.kind==3)withheld=true;
      const bool automatic=autoInitially || (activeCase==TestCase::enable && configuredAt>=3000);
      if(s.kind==1){assert(withheld);assert(s.ms-configuredAt>=240);}
      if(s.kind==1 && automatic){assert(withheld);assert(s.ms-configuredAt>=1100);}
    }
  }
}
int countSamples(int kind,uint32_t start,uint32_t end){
  return std::count_if(received.begin(),received.end(),[&](const SensorSample& s){return s.kind==kind && s.ms>=start && s.ms<end;});
}
void stableAndRecovery(){
  run(TestCase::stable,7500);
  int ecg=0,ppg=0;uint32_t previous=0;
  for(const auto& s:received){
    assert(s.command==5);
    if(s.kind==0 && s.ms>=1500 && s.ms<2500){++ecg;assert(s.ms>previous);previous=s.ms;}
    if((s.kind==1 || s.kind==3) && s.ms>=1500 && s.ms<2500)++ppg;
  }
  std::printf("acquisition: ECG %d/s, PPG %d/s, BACK conversions %d, battery %d\n",ecg,ppg,adcConversions[2],adcConversions[1]);
  assert(ecg>=220 && ecg<=255);assert(ppg==25);
  bool backRead=false,adsMissing=false,adsRecovered=false,maxMissing=false,maxRecovered=false;
  for(const auto& h:healthReceived){
    if(h.ms>1500 && h.ms<3000)backRead|=h.state.back;
    if(h.ms>3200 && h.ms<3900)adsMissing|=!h.state.ads && !h.state.back;
    if(h.ms>4500)adsRecovered|=h.state.ads;
    if(h.ms>4300 && h.ms<4900)maxMissing|=!h.state.max;
    if(h.ms>6200)maxRecovered|=h.state.max;
    assert(h.state.ledRed==60 && h.state.ledIr==60);
  }
  assert(backRead && adsMissing && adsRecovered && maxMissing && maxRecovered);
  assert(adcConversions[1]>=2);
  assert(countSamples(1,2500,3000)>0 && countSamples(1,6500,7500)>0);
  assert(countSamples(1,4050,5000)==0);
  assertEpochsAndSettling();
}
void weakAdjustment(){
  run(TestCase::weak);
  assertEpochsAndSettling();
  bool raised=false;
  for(const auto& s:received)if(s.kind==4 && s.b>60)raised=true;
  assert(raised && countSamples(3,1000,4500)>50 && countSamples(1,7000,10000)>50);
  assert(Wire.registers[0x0d]>60 && Wire.registers[0x0d]<=ppg::AutoExposure::Max);
  int ecg=countSamples(0,1500,2500);assert(ecg>=220 && ecg<=255);
  assert(Wire.registers[8]==0x5f && Wire.registers[0x0a]==0x27);
}
void fixedAndReenable(){
  run(TestCase::fixed,5000);assertEpochsAndSettling(false);
  for(const auto& write:Wire.ledWrites)assert(write.value==60);
  assert(countSamples(1,1500,4500)>50);
  run(TestCase::enable,11000);assertEpochsAndSettling(false);
  bool raised=false,restarted=false;
  for(const auto& s:received)if(s.kind==4){
    if(s.ms<3000)assert(s.a==60 && s.b==60);
    if(s.ms>=3000 && s.a==60 && s.b==60)restarted=true;
    if(s.ms>=3000 && s.b>60)raised=true;
  }
  assert(restarted && raised && countSamples(1,8000,11000)>50);
  assert(countSamples(1,3050,4100)==0);
}
void configurationFailure(TestCase test){
  run(test,12000);assertEpochsAndSettling();assert(failureMs>0);
  uint32_t recoveredAt=0;bool sawFault=false,sawGap=false;
  for(const auto& h:healthReceived)if(h.ms>=failureMs && h.state.faults)sawFault=true;
  for(const auto& s:received){
    if(s.kind==2 && s.ms>=failureMs)sawGap=true;
    if(s.kind==4 && s.ms>failureMs && !recoveredAt)recoveredAt=s.ms;
    if(s.kind==1 && s.ms>=failureMs)assert(recoveredAt && s.ms-recoveredAt>=1100);
  }
  assert(sawFault && sawGap && recoveredAt>failureMs);
  assert(countSamples(1,9500,12000)>30);
}
void sessionReset(){
  run(TestCase::session,14000);assertEpochsAndSettling();
  bool baseline=false;
  for(const auto& s:received){
    if(s.command==9 && s.kind==4 && !baseline){assert(s.a==60 && s.b==60);baseline=true;}
    if(s.command==9 && s.kind==1)assert(s.ms>=9100);
  }
  assert(baseline && countSamples(1,12500,14000)>20);
}
int main(){
  // Rejected amplitudes must not touch the sensor or partially change one LED.
  Wire.registers[0x0c]=Wire.registers[0x0d]=60;
  assert(!configurePpgLeds(ppg::AutoExposure::Min-1,60));
  assert(!configurePpgLeds(60,ppg::AutoExposure::Max+1));
  assert(Wire.ledWrites.empty());
  assert(Wire.registers[0x0c]==60 && Wire.registers[0x0d]==60);
  stableAndRecovery();weakAdjustment();fixedAndReenable();
  configurationFailure(TestCase::failedWrite);configurationFailure(TestCase::mismatch);sessionReset();
  puts("acquisition timing, auxiliary channels, exposure isolation, checked LED writes and I2C recovery passed");
}
