#!/usr/bin/env python3
"""Stage the firmware as a self-contained Arduino sketch from explicit sources."""
import argparse
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, default=root / 'build')
p.add_argument('--upload', help='Serial device, e.g. /dev/ttyACM0 (only when explicitly supplied)')
p.add_argument('--stage-only', action='store_true')
args = p.parse_args()
out = args.output.resolve()
sketch = out / 'MedicalMonitor'
sketch.mkdir(parents=True, exist_ok=True)
for name in ['MedicalMonitor.ino', 'SignalProcessing.h', 'EcgProcessing.h', 'Acquisition.h',
             'HistoryAnalysis.h', 'HistoryText.h', 'PpgAutoExposure.h', 'rf_algorithm.cpp']:
    shutil.copy2(root / name, sketch / name)
rf = Path('Pulse+SpO2/MAX30102_by_RF-master')
(sketch / rf).mkdir(parents=True, exist_ok=True)
for name in ['algorithm_by_RF.cpp', 'algorithm_by_RF.h']:
    shutil.copy2(root / rf / name, sketch / rf / name)
if args.stage_only:
    print(sketch)
    raise SystemExit(0)
fqbn = 'esp32:esp32:esp32c3:CDCOnBoot=cdc,FlashSize=4M,PartitionScheme=default'
subprocess.run(['arduino-cli', 'compile', '--fqbn', fqbn, '--warnings', 'all',
                '--build-path', str(out / 'objects'), '--output-dir', str(out / 'firmware'),
                str(sketch)], check=True)
if args.upload:
    subprocess.run(['arduino-cli', 'upload', '--fqbn', fqbn, '--port', args.upload,
                    '--input-dir', str(out / 'firmware'), str(sketch)], check=True)
