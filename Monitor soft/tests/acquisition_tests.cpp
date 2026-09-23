// Execute the real acquisition loop against deterministic I2C devices.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#include <cstdio>
#include <stdexcept>
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
int adcConversions[3]={};
struct FakeWire {
  uint8_t address=0,reg=0;
  std::vector<uint8_t> tx,rx;
  uint16_t config=0;
  uint64_t ready=0;
  uint8_t writePtr=0,readPtr=0;
  uint32_t produced=0;
  void beginTransmission(uint8_t a){address=a;tx.clear();}
  void write(uint8_t a){tx.push_back(a);}
  int endTransmission(bool=true){
    simulatedUs+=60;
    if(address==0x48 && adcUnplugged) return 2;
    reg=tx[0];
    if(address==0x48 && tx.size()==3){
      config=(uint16_t(tx[1])<<8)|tx[2];ready=simulatedUs+1250;
      ++adcConversions[((config>>12)&7)-4];
    }
    return 0;
  }
  size_t requestFrom(uint8_t a,size_t n){
    simulatedUs+=60;rx.clear();
    if(a==0x48){
      uint16_t value=0;
      if(reg==1) value=(config&0x7fff)|(simulatedUs>=ready?0x8000:0);
      if(reg==0){int channel=((config>>12)&7)-4;value=channel==0?13200:(channel==1?15200:26400);}
      rx={uint8_t(value>>8),uint8_t(value)};
    } else if(reg==0xff) rx={0x15};
    else if(reg==4){
      uint32_t current=millis()/40;
      writePtr=(writePtr+current-produced)&31;produced=current;
      rx={writePtr,0,readPtr};
    } else if(reg==7){rx={1,95,144,1,134,160};readPtr=(readPtr+1)&31;}
    return std::min(n,rx.size());
  }
  int read(){int v=rx.front();rx.erase(rx.begin());return v;}
} Wire;
struct FakePulse {
  bool begin(FakeWire&,int){return true;}
  void setup(int,int,int,int,int,int){clearFIFO();}
  void clearFIFO(){Wire.produced=millis()/40;Wire.readPtr=Wire.writePtr=0;}
} pulseSensor;
#include "../Acquisition.h"

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
bool backRead=false,recovered=false,missing=false;
void vTaskDelay(unsigned){
  auto q=static_cast<FakeQueue*>(sampleQueue);
  while(!q->items.empty()){
    SensorSample s;std::memcpy(&s,q->items.front().data(),sizeof(s));q->items.pop_front();received.push_back(s);
  }
  auto h=static_cast<FakeQueue*>(healthQueue);
  if(!h->items.empty()){
    SensorHealth state;std::memcpy(&state,h->items.front().data(),sizeof(state));
    if(millis()>1500 && millis()<3000) backRead|=state.back;
    if(millis()>3200 && millis()<3900) missing|=!state.ads && !state.back;
    if(millis()>4500) recovered|=state.ads;
  }
  adcUnplugged=millis()>=3000 && millis()<4000;
  simulatedUs+=1000;
  if(millis()>6000) throw std::runtime_error("done");
}
int main(){
  startAcquisition();sensorCommand.store(5);
  try{acquisitionTask(nullptr);}catch(const std::runtime_error&){}
  int ecg=0,ppg=0;uint32_t previous=0;
  for(const auto& s:received){
    assert(s.command==5);
    if(s.kind==0 && s.ms>=1500 && s.ms<2500){++ecg;assert(s.ms>previous);previous=s.ms;}
    if(s.kind==1 && s.ms>=1500 && s.ms<2500)++ppg;
  }
  std::printf("acquisition: ECG %d/s, PPG %d/s, BACK conversions %d, battery %d\n",ecg,ppg,adcConversions[2],adcConversions[1]);
  assert(ecg>=220 && ecg<=255);assert(ppg==25);
  assert(backRead && missing && recovered);assert(adcConversions[1]>=2);
  delete static_cast<FakeQueue*>(sampleQueue);delete static_cast<FakeQueue*>(healthQueue);
  puts("acquisition timing, auxiliary channels and I2C recovery passed");
}
