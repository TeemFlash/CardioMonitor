#pragma once
#include <stdint.h>
#include <math.h>

// One accepted optical HR per second of this viewing/recording session.
// Fixed storage: no samples or history retained in RAM.
class DisplayStatistics {
  uint64_t sum_=0;
  uint32_t count_=0;
  int low_=0,high_=0;
public:
  void reset(){sum_=0;count_=0;low_=high_=0;}
  void add(int value,bool valid){
    if(!valid || value<=0 || value>300 || count_==UINT32_MAX)return;
    if(!count_)low_=high_=value;
    if(value<low_)low_=value;
    if(value>high_)high_=value;
    sum_+=uint32_t(value);++count_;
  }
  int minimum()const{return count_?low_:-1;}
  int maximum()const{return count_?high_:-1;}
  int average()const{return count_?int((sum_+count_/2)/count_):-1;}
};
