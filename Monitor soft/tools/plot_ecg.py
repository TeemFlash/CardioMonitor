#!/usr/bin/env python3
"""Plot an SD ECG CSV or a clearly labelled synthetic firmware-filter example."""
import argparse
import csv
import io
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('csv',nargs='?',type=Path)
p.add_argument('--demo',action='store_true',help='Synthetic signal through the actual C++ firmware filter')
p.add_argument('--start',type=float,default=4)
p.add_argument('--seconds',type=float,default=4)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if bool(a.csv)==a.demo or a.start<0 or not 0<a.seconds<=60:
    p.error('Specify either CSV or --demo; start >= 0, seconds in (0, 60].')
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='cardio-plot-') as tmp:
    os.environ.setdefault('MPLCONFIGDIR',tmp)
    if a.demo:
        source=Path(tmp)/'demo.cpp';binary=Path(tmp)/'demo'
        source.write_text(r'''
#include "EcgProcessing.h"
#include <cstdio>
int main(){
  EcgBeatDetector e;
  puts("millis_ms,kind,adc_code,filtered_relative");
  for(int i=0;i<20000;++i){
    double t=i/250.0,phase=fmod(t,0.8),r=(phase-0.2)/0.014;
    double p=(phase-0.08)/0.025,tw=(phase-0.4)/0.06;
    double clean=600*exp(-r*r/2)+70*exp(-tw*tw/2)+35*exp(-p*p/2);
    double raw=2048+clean+100*sin(6.28318530718*0.15*t)+250*sin(6.28318530718*50*t);
    e.update(raw,1000+i*4);
    printf("%d,ecg,%.1f,%.3f\n",i*4,raw*26400/4095,e.filtered);
  }
}
''')
        subprocess.run(['c++','-std=c++17','-I',str(root/'MedicalMonitor'),str(source),'-o',str(binary)],check=True)
        stream=io.StringIO(subprocess.check_output([str(binary)],text=True))
    else:
        stream=a.csv.open(encoding='utf-8-sig')
    time=[];raw=[];filtered=[]
    with stream:
        rows=csv.DictReader(line for line in stream if not line.startswith('#'))
        for row in rows:
            if row.get('kind','ecg')!='ecg':continue
            try:
                t=float(row['millis_ms'])/1000
                adc=float(row['adc_code'])*4095/26400
                value=float(row['filtered_relative'])
            except (KeyError,ValueError,TypeError):continue
            if t<a.start:continue
            if t>a.start+a.seconds:break
            time.append(t);raw.append(adc);filtered.append(value)
    if not time:raise SystemExit('No ECG samples found in the requested time interval.')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    raw=np.array(raw);raw-=np.median(raw)
    fig,axes=plt.subplots(2,1,figsize=(11,5.6),sharex=True,layout='constrained')
    fig.set_facecolor('#f4f8f9')
    axes[0].plot(time,raw,color='#82959c',lw=0.85)
    axes[1].plot(time,filtered,color='#007770',lw=1.35)
    axes[0].set_title('Вход АЦП: постоянная составляющая убрана только для сравнения',loc='left',fontsize=10)
    axes[1].set_title('После фильтра прошивки: 0,5–40 Гц + режектор 50 Гц' if a.demo else
                      'После фильтра прошивки: параметры см. в метаданных CSV',loc='left',fontsize=10)
    for ax in axes:
        ax.set_facecolor('white');ax.grid(color='#dce5e8',lw=0.6)
        ax.set_ylabel('Отн. единицы');ax.spines[['top','right']].set_visible(False)
    axes[1].set_xlabel('Время, с')
    fig.suptitle('СИНТЕТИЧЕСКИЙ ПРИМЕР · 75 уд/мин · помеха 50 Гц и дрейф' if a.demo else
                 'ЭКГ из CSV · фильтрованный мониторный сигнал, не диагностическое заключение',fontsize=12)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    fig.savefig(a.output,dpi=160)
    print(a.output)
