#!/usr/bin/env python3
"""Render the real firmware drawing functions into SVG on a computer.
Illustrative signals only; SVG system fonts approximate the TFT bitmap fonts.
Does not read devices or create measurement files.
"""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, default=root/'build'/'preview-light')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
source = (root/'MedicalMonitor'/'MedicalMonitor.ino').read_text()
names = ['clearDisplay', 'UI', 'ecgGraphTop', 'ecgGraphBottom', 'drawHeader', 'drawRecBadge',
         'drawMonitorNavigation', 'drawMeasurementTabs', 'drawMeasurementFrame',
         'drawOpticalNumber', 'updateDisplayStatistics', 'drawMonitorCaption',
         'drawGraphGrid', 'drawWaveColumn', 'drawMeasurementStatus',
         'eraseEcgPlotColumn', 'renderEcgWave', 'renderECG', 'renderPulseOnly', 'renderSpo2Only']
functions = []
for name in names:
    m = re.search(r'^([^\n]+\b'+name+r'\([^\n]*\))\s*\{', source, re.M)
    if not m: raise RuntimeError(name)
    end = m.end(); depth = 1
    while depth:
        c = source[end]; end += 1
        if c == '{': depth += 1
        elif c == '}': depth -= 1
    functions.append((m.group(1), source[m.start():end]))
enums = source[source.index('enum Language'):source.index('// ============================================================\n// 1.')]
palette = source[source.index('constexpr uint16_t rgb565'):source.index('// ============================================================\n// 2.')]
strings = source[source.index('static const char* const STR_EN'):source.index('// ============================================================\n// 3.')]
prelude = r'''
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <atomic>
#include "EcgProcessing.h"
#include "DisplayModel.h"
using String=std::string;
using std::max;
using std::min;
template<class T>T constrain(T x,T lo,T hi){return max(lo,min(x,hi));}
unsigned long millis(){return 5000;}
std::string hex(uint16_t c){char b[12];snprintf(b,sizeof(b),"#%02x%02x%02x",int((c>>11)*255/31),int(((c>>5)&63)*255/63),int((c&31)*255/31));return b;}
std::string xml(std::string s){std::string o;for(char c:s){if(c=='&')o+="&amp;";else if(c=='<')o+="&lt;";else if(c=='>')o+="&gt;";else o+=c;}return o;}
int glyphs(std::string s){int n=0;for(unsigned char c:s)if((c&0xc0)!=0x80)++n;return n;}
struct Canvas {
 int x=0,y=0,sz=1;uint16_t color=0;
 void fillRect(int x,int y,int w,int h,uint16_t c){if(w>0&&h>0)std::cout<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<w<<"' height='"<<h<<"' fill='"<<hex(c)<<"'/>\n";}
 void fillScreen(uint16_t c){fillRect(0,0,320,240,c);}
 void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c){std::cout<<"<rect x='"<<x<<"' y='"<<y<<"' width='"<<w<<"' height='"<<h<<"' rx='"<<r<<"' fill='"<<hex(c)<<"'/>\n";}
 void drawLine(int x,int y,int xx,int yy,uint16_t c){std::cout<<"<path d='M"<<x<<","<<y<<" L"<<xx<<","<<yy<<"' stroke='"<<hex(c)<<"' fill='none'/>\n";}
 void drawFastVLine(int x,int y,int h,uint16_t c){drawLine(x,y,x,y+h,c);}
 void drawFastHLine(int x,int y,int w,uint16_t c){drawLine(x,y,x+w,y,c);}
 void drawPixel(int x,int y,uint16_t c){fillRect(x,y,1,1,c);}
 void fillCircle(int x,int y,int r,uint16_t c){std::cout<<"<circle cx='"<<x<<"' cy='"<<y<<"' r='"<<r<<"' fill='"<<hex(c)<<"'/>\n";}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 void setTextSize(int size){sz=size;}
 void setTextColor(uint16_t c,uint16_t=0){color=c;}
 void text(std::string s,int baseline,int px){int width=glyphs(s)*px;std::cout<<"<text x='"<<x<<"' y='"<<baseline<<"' font-family='DejaVu Sans Mono,monospace' font-size='"<<px*1.65<<"' textLength='"<<width<<"' lengthAdjust='spacingAndGlyphs' fill='"<<hex(color)<<"'>"<<xml(s)<<"</text>\n";}
 void print(const char* s){text(s,y+7*sz,6*sz);}
 void print(int n){print(std::to_string(n).c_str());}
} tft;
#define FONT_SMALL 0
#define FONT_MED 1
struct Font {
 int x=0,y=0,font=0;uint16_t c=0;
 void setFont(int f){font=f;}
 void setFontMode(int){}
 void setForegroundColor(uint16_t v){c=v;}
 void setBackgroundColor(uint16_t){}
 void setCursor(int xx,int yy){x=xx;y=yy;}
 int getUTF8Width(const char* t){return glyphs(t)*(font?10:6);}
 void print(const char* t){tft.setCursor(x,y);tft.setTextColor(c);tft.text(t,y,font?10:6);}
}u8g2;
void printFit(String text,int x,int baseline,int width){
 while(glyphs(text)*6>width && !text.empty()){size_t i=text.size()-1;while(i && (uint8_t(text[i])&0xc0)==0x80)--i;text.erase(i);}
 u8g2.setCursor(x,baseline);u8g2.print(text.c_str());
}
#define SCREEN_W 320
#define HEADER_H 28
constexpr int SCR_ECG=1,SCR_PULSE=2,SCR_SPO2=3;
constexpr int GRAPH_TOP=78,GRAPH_BOTTOM=170,PPG_TOP=142,PPG_BOTTOM=192;
constexpr float PPG_GAIN=.015f;
int currentScreen=SCR_PULSE,graphX=0,prevEcgY=0;
uint32_t displayRevision=1;int displayedScreen=-1;int displayedLanguage=0;
uint32_t lastHeader=0,lastDisplayStats=0,previousEcgBin=0;
bool ppgPlotConnected=false,logFileOpen=false,sdError=false,fineGrid=true,ecgPlotConnected=false,graphHasSample=false,acquisitionStarted=true;
float graphLow=0,graphHigh=0;
uint8_t ecgGain=1,ecgMains=50;
namespace ppg {enum class ExposureState{Stable};}
struct {bool ads=true;ppg::ExposureState exposure=ppg::ExposureState::Stable;}sensorHealth;
std::atomic<bool> ppgAutoEnabled{true};
struct {bool finger=true,hrValid=true,spo2Valid=true;int beatAvg=75;float spo2=98;}poxProcessor;
struct {bool initialized=true,quality=true;uint32_t start=0;float bpm=75;}ecgDetector;
EcgPlotBuffer ecgPlot;
DisplayStatistics displayPulseStats;
void drawLargeReading(int value,uint16_t color,int x,int y){tft.setTextSize(6);tft.setTextColor(color);tft.setCursor(x+(108-int((value<0?std::string("--"):std::to_string(value)).size())*36)/2,y);if(value<0)tft.print("--");else tft.print(value);}
void updateHeaderStatus(){tft.setTextSize(1);tft.setTextColor(0x536f);tft.setCursor(212,10);tft.print("SD  WiFi  --");}
'''
main = r'''
StringID pulseState(){return poxProcessor.hrValid?S_READY:S_PLACE_FINGER;}
int main(int argc,char**argv){
 currentLanguage=LANG_RU;
 currentScreen=argc>1?atoi(argv[1]):SCR_PULSE;
 bool empty=argc>2;
 if(empty){poxProcessor.finger=poxProcessor.hrValid=poxProcessor.spo2Valid=false;ecgDetector.quality=false;}
 else {displayPulseStats.add(72,true);displayPulseStats.add(75,true);displayPulseStats.add(78,true);}
 std::cout<<"<svg xmlns='http://www.w3.org/2000/svg' width='960' height='720' viewBox='0 0 320 240'><title>Illustrative firmware rendering, synthetic data, not a measurement</title>\n";
 drawMeasurementFrame();
 int top=currentScreen==SCR_ECG?GRAPH_TOP:PPG_TOP,bottom=currentScreen==SCR_ECG?GRAPH_BOTTOM:PPG_BOTTOM;
 if(currentScreen==SCR_ECG)drawGraphGrid(top,bottom);
 if(!empty && currentScreen==SCR_ECG){
   ecgPlot.reset(0);
   for(int i=0;i<320;++i){
     float phase=fmodf(i*.01f,.8f),r=(phase-.2f)/.014f,t=(phase-.4f)/.06f;
     float value=160*expf(-r*r/2)+30*expf(-t*t/2)-25*expf(-powf((phase-.23f)/.01f,2)/2);
     if(currentScreen==SCR_SPO2)drawWaveColumn(top,bottom,12*sinf(i*.12f),12*sinf(i*.12f));
     else {ecgPlot.push(i*10,value,true);renderEcgWave();}
   }
 }
 if(currentScreen==SCR_PULSE)renderPulseOnly();else if(currentScreen==SCR_ECG)renderECG();else renderSpo2Only();
 std::cout<<"</svg>";
}
'''
with tempfile.TemporaryDirectory(prefix='cardio-display-render-') as tmp:
    cpp=Path(tmp)/'render.cpp';binary=Path(tmp)/'render'
    cpp.write_text(prelude+enums+palette+strings+'\nLanguage currentLanguage=LANG_RU;\n'+
                   '\n'.join(signature+';' for signature,_ in functions)+
                   '\nStringID pulseState();\n'+'\n'.join(body for _,body in functions)+main)
    subprocess.run(['c++','-std=c++17','-I',str(root/'MedicalMonitor'),str(cpp),'-o',str(binary)],check=True)
    for label,screen,empty in [('pulse',2,False),('oxygen',3,False),('ecg',1,False),('no-signal',1,True)]:
        args=[str(binary),str(screen)]+(['empty'] if empty else [])
        target=a.output/(label+'.svg')
        target.write_bytes(subprocess.check_output(args))
        print(target)
