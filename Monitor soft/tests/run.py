#!/usr/bin/env python3
"""Native regression tests; no board or Arduino libraries needed."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='cardio-tests-') as tmp:
    work=Path(tmp)
    include=work/'freertos';include.mkdir()
    (include/'FreeRTOS.h').write_text('''#pragma once
using QueueHandle_t=void*;
constexpr int pdTRUE=1,pdPASS=1;
QueueHandle_t xQueueCreate(unsigned,unsigned);
int xQueueSend(QueueHandle_t,const void*,unsigned);
int xQueueOverwrite(QueueHandle_t,const void*);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelay(unsigned);
''')
    for name in ['task.h','queue.h']:(include/name).write_text('#include "FreeRTOS.h"\n')
    for test in ['signal','acquisition','ecg','history_analysis','ppg_auto']:
        sources=[str(root/'tests'/f'{test}_tests.cpp')]
        if test=='signal':sources+=[str(root/'MedicalMonitor'/'rf_algorithm.cpp')]
        binary=work/test
        subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        '-I',str(work),*sources,'-o',str(binary)],check=True)
        # LeakSanitizer cannot run in ptrace-based sandboxes; ASan/UBSan remain enabled.
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
        subprocess.run([str(binary)],env=env,check=True)
