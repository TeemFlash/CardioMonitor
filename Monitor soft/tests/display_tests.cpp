#include "../MedicalMonitor/DisplayModel.h"
#include <cassert>
#include <cstdio>
int main(){
  DisplayStatistics s;
  assert(s.minimum()==-1 && s.average()==-1 && s.maximum()==-1);
  s.add(75,false);s.add(0,true);s.add(-1,true);s.add(301,true);
  assert(s.average()==-1);
  s.add(60,true);s.add(90,true);s.add(75,true);
  assert(s.minimum()==60 && s.average()==75 && s.maximum()==90);
  for(int i=0;i<100000;++i)s.add(75,true);
  assert(s.average()==75);
  s.reset();assert(s.minimum()==-1 && s.maximum()==-1 && s.average()==-1);
  puts("Display session statistics: invalid samples, range and reset passed");
}
