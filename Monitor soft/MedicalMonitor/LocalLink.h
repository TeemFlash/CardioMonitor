#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

// UTC comes from the ESP32 SNTP client, independently of any phone.
inline uint64_t localUtcMillis() {
  timeval tv{};gettimeofday(&tv,nullptr);
  uint64_t ms=uint64_t(tv.tv_sec)*1000+tv.tv_usec/1000;
  return ms>=1704067200000ULL && ms<4102444800000ULL?ms:0;
}
inline void serviceNetworkClock() {
  static bool wasConnected=false;
  bool connected=WiFi.status()==WL_CONNECTED;
  if(connected && !wasConnected)configTime(0,0,"pool.ntp.org","time.google.com");
  wasConnected=connected;
}
// Local, read-only protocol v1. All SD access runs in the main loop.
// Two nonblocking sockets: one live stream and one archive transfer.
struct LinkSnapshot {
  int mode=0,hr=-1,spo2=-1,battery=-1,ecgHr=-1;
  bool finger=false,ecgOk=false,sd=false,recording=false;
};
class LocalLink {
  static constexpr int PORT=8765,TX=4096;
  struct Peer {
    int fd=-1;char tx[TX]{},rx[80]{};size_t sent=0,length=0,used=0;
    bool live=false,listing=false;File directory,file;
    uint32_t left=0,crc=0xffffffff,lastProgress=0,lastLive=0;
    int count=0;
  } peers[2];
  struct Point {uint32_t ms;float value;} points[80];
  int pointCount=0,listener=-1,recordCount=-1,lastMode=-1;
  uint32_t dropped=0,sequence=0,lastAttempt=0,address=0;
  char id[24]{};
  static bool validName(const char* name) {
    const char* p=nullptr;
    if(!strncmp(name,"ecg_",4))p=name+4;
    else if(!strncmp(name,"pulse_",6))p=name+6;
    else if(!strncmp(name,"spo2_",5))p=name+5;
    if(!p)return false;
    int digits=0;while(*p>='0' && *p<='9'){++digits;++p;}
    return digits>0 && digits<=9 && !strcmp(p,".csv");
  }
  void closePeer(Peer& p) {
    if(p.fd>=0)::close(p.fd);
    p.fd=-1;p.directory.close();p.file.close();p.live=p.listing=false;
    p.sent=p.length=p.used=p.left=0;
  }
  void queue(Peer& p,const char* line) {
    size_t n=strlen(line);if(n>=TX){closePeer(p);return;}
    memcpy(p.tx,line,n);p.length=n;p.sent=0;
  }
  void command(Peer& p,const LinkSnapshot& s,const char* active) {
    if(!strcmp(p.rx,"LIVE")) {
      for(auto& other:peers)if(&other!=&p && other.live){queue(p,"{\"type\":\"error\",\"message\":\"live_busy\"}\n");return;}
      p.live=true;pointCount=0;
    } else if(!strcmp(p.rx,"LIST")) {
      if(!s.sd){queue(p,"{\"type\":\"error\",\"message\":\"no_sd\"}\n");return;}
      p.directory=SD.open("/");
      if(!p.directory){queue(p,"{\"type\":\"error\",\"message\":\"sd_read\"}\n");return;}
      p.listing=true;p.count=0;p.live=false;
    } else if(!strncmp(p.rx,"GET ",4) && validName(p.rx+4)) {
      const char* name=p.rx+4;
      if(!s.sd || (active && !strcmp(name,active))){queue(p,"{\"type\":\"error\",\"message\":\"file_busy_or_no_sd\"}\n");return;}
      char path[48];snprintf(path,sizeof(path),"/%s",name);p.file=SD.open(path,FILE_READ);
      if(!p.file || p.file.isDirectory()){p.file.close();queue(p,"{\"type\":\"error\",\"message\":\"not_found\"}\n");return;}
      p.left=p.file.size();p.crc=0xffffffff;p.live=false;
      char line[160];snprintf(line,sizeof(line),"{\"type\":\"file\",\"name\":\"%s\",\"size\":%lu}\n",name,(unsigned long)p.left);queue(p,line);
    } else queue(p,"{\"type\":\"error\",\"message\":\"bad_command\"}\n");
  }
  void live(Peer& p,const LinkSnapshot& s,uint32_t now) {
    int n=snprintf(p.tx,TX,"{\"type\":\"live\",\"seq\":%lu,\"ms\":%lu,\"utc_ms\":%llu,\"mode\":%d,\"hr\":%d,\"ecg_hr\":%d,\"spo2\":%d,\"battery\":%d,\"finger\":%s,\"ecg_ok\":%s,\"sd\":%s,\"recording\":%s,\"files\":%d,\"dropped\":%lu,\"ecg\":[",
      (unsigned long)++sequence,(unsigned long)now,(unsigned long long)localUtcMillis(),s.mode,s.hr,s.ecgHr,s.spo2,s.battery,s.finger?"true":"false",s.ecgOk?"true":"false",s.sd?"true":"false",s.recording?"true":"false",recordCount,(unsigned long)dropped);
    for(int i=0;i<pointCount && s.ecgOk;++i) {
      int written=snprintf(p.tx+n,TX-n,"%s[%lu,%.3f]",i?",":"",(unsigned long)points[i].ms,points[i].value);
      if(written<0 || written>=TX-n-4){++dropped;break;}n+=written;
    }
    memcpy(p.tx+n,"]}\n",3);p.length=n+3;p.sent=0;p.lastLive=now;pointCount=0;
  }
public:
  void addEcg(uint32_t ms,float value,bool valid) {
    if(!valid || !isfinite(value))return;
    bool subscribed=false;for(auto& p:peers)subscribed|=p.live;
    if(!subscribed)return;
    if(pointCount==80){++dropped;return;}
    points[pointCount++]={ms,value};
  }
  void service(const LinkSnapshot& s,const char* active) {
    uint32_t now=millis();
    if(lastMode!=s.mode){pointCount=0;lastMode=s.mode;}
    uint32_t ip=uint32_t(WiFi.localIP());
    if(WiFi.status()!=WL_CONNECTED || (listener>=0 && address!=ip)) {
      if(listener>=0){::close(listener);listener=-1;MDNS.end();}
      for(auto& p:peers)if(p.fd>=0)closePeer(p);
      pointCount=0;return;
    }
    if(listener<0) {
      if(now-lastAttempt<3000)return;lastAttempt=now;
      listener=::socket(AF_INET,SOCK_STREAM,0);if(listener<0)return;
      int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
      sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(PORT);addr.sin_addr.s_addr=INADDR_ANY;
      if(::bind(listener,(sockaddr*)&addr,sizeof(addr))<0 || ::listen(listener,2)<0){::close(listener);listener=-1;return;}
      fcntl(listener,F_SETFL,O_NONBLOCK);address=ip;
      snprintf(id,sizeof(id),"cardio-%012llx",(unsigned long long)ESP.getEfuseMac());
      if(!MDNS.begin(id)){::close(listener);listener=-1;MDNS.end();return;}
      MDNS.addService("cardiomonitor","tcp",PORT);MDNS.addServiceTxt("cardiomonitor","tcp","version","1");
      Serial.printf("CardioMonitor LAN %s:%d\n",WiFi.localIP().toString().c_str(),PORT);
    }
    for(auto& p:peers) {
      if(p.fd<0)continue;
      if(now-p.lastProgress>15000){closePeer(p);continue;}
      if(p.sent<p.length) {
        int n=::send(p.fd,p.tx+p.sent,min(size_t(1024),p.length-p.sent),MSG_DONTWAIT);
        if(n>0){p.sent+=n;p.lastProgress=now;}
        else if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK){closePeer(p);}
        continue;
      }
      p.sent=p.length=0;
      if(p.file) {
        if(!p.left){char line[80];snprintf(line,sizeof(line),"{\"type\":\"end\",\"crc32\":%lu}\n",(unsigned long)(p.crc^0xffffffff));p.file.close();queue(p,line);continue;}
        size_t take=min(size_t(512),size_t(p.left));int got=p.file.read((uint8_t*)p.tx,take);
        if(got!=int(take)){closePeer(p);continue;}
        for(int i=0;i<got;++i){p.crc^=uint8_t(p.tx[i]);for(int bit=0;bit<8;++bit)p.crc=(p.crc>>1)^((p.crc&1)?0xedb88320:0);}
        p.left-=got;p.length=got;continue;
      }
      if(p.listing) {
        File entry=p.directory.openNextFile();
        if(!entry){p.directory.close();p.listing=false;recordCount=p.count;queue(p,"{\"type\":\"list_end\"}\n");continue;}
        String name=entry.name();if(name.startsWith("/"))name=name.substring(1);
        if(!entry.isDirectory() && validName(name.c_str()) && (!active || name!=active)) {
          char line[128];snprintf(line,sizeof(line),"{\"type\":\"entry\",\"name\":\"%s\",\"size\":%lu}\n",name.c_str(),(unsigned long)entry.size());queue(p,line);++p.count;
        }entry.close();p.lastProgress=now;continue;
      }
      for(int budget=0;budget<80;++budget) {
        char c;int n=::recv(p.fd,&c,1,MSG_DONTWAIT);
        if(n==0){closePeer(p);break;}
        if(n<0){if(errno!=EAGAIN && errno!=EWOULDBLOCK)closePeer(p);break;}
        p.lastProgress=now;
        if(c=='\n'){p.rx[p.used]=0;command(p,s,active);p.used=0;break;}
        if(c=='\r')continue;
        if(p.used>=sizeof(p.rx)-1){closePeer(p);break;}p.rx[p.used++]=c;
      }
      if(p.fd>=0 && !p.length && p.live && now-p.lastLive>=200)live(p,s,now);
    }
    int fd=::accept(listener,nullptr,nullptr);
    if(fd>=0) {
      Peer* free=nullptr;for(auto& p:peers)if(p.fd<0){free=&p;break;}
      if(!free)::close(fd);
      else {fcntl(fd,F_SETFL,O_NONBLOCK);free->fd=fd;free->lastProgress=now;
        char hello[128];snprintf(hello,sizeof(hello),"{\"type\":\"hello\",\"version\":1,\"device\":\"%s\"}\n",id);queue(*free,hello);}
    }
  }
};
