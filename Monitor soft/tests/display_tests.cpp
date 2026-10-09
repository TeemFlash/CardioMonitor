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
  DisplayTextCache cache;
  assert(cache.update("75",1));
  assert(!cache.update("75",1));
  assert(cache.update("--",1));
  assert(cache.update("--",2)); // screen re-entry must redraw identical values
  assert(cache.update("",2));
  assert(!cache.update("",2));
  char longText[200];memset(longText,'x',sizeof(longText)-1);longText[199]=0;
  assert(cache.update(longText,2));
  assert(cache.update(longText,2)); // no truncation collision or out-of-bounds write
  assert(cache.update("",2));
  puts("Display session statistics: invalid samples, range, reset and dirty-field invalidation passed");
}
