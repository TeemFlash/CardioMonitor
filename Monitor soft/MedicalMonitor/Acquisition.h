#pragma once
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "PpgAutoExposure.h"

// One task owns I2C. UI, SD and Wi-Fi never touch Wire after setup.
// command: low 2 bits = idle / ECG / PPG; high bits = session generation.
// kind: 0 ECG, 1 accepted-level PPG, 2 gap, 3 withheld PPG, 4 LED settings.
// Every optical sample carries an epoch: a dropped settings event cannot mix
// samples acquired with different LED currents in a single RF window.
struct SensorSample { uint32_t command, ms, a, b; uint8_t kind; uint16_t exposureEpoch; };
struct SensorHealth {
  bool ads = false, max = false, back = false;
  float battery = 0;
  uint32_t adcMs = 0, ppgMs = 0, dropped = 0, ecgLate = 0, faults = 0;
  uint8_t ledRed = 60, ledIr = 60;
  ppg::ExposureState exposure = ppg::ExposureState::Searching;
};
QueueHandle_t sampleQueue = nullptr, healthQueue = nullptr;
std::atomic<uint32_t> sensorCommand{0};
std::atomic<bool> ppgAutoEnabled{true};
SensorHealth sensorHealth;
bool acquisitionStarted = false;

bool readI2c(uint8_t address, uint8_t reg, uint8_t* dst, size_t count) {
  Wire.beginTransmission(address); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, count) != count) return false;
  for (size_t i = 0; i < count; ++i) dst[i] = Wire.read();
  return true;
}
bool writeI2c(uint8_t address, uint8_t reg, uint16_t value, bool word) {
  Wire.beginTransmission(address); Wire.write(reg);
  if (word) Wire.write(uint8_t(value >> 8));
  Wire.write(uint8_t(value));
  return Wire.endTransmission() == 0;
}
bool clearPpgFifo() {
  return writeI2c(0x57,0x04,0,false) && writeI2c(0x57,0x05,0,false) &&
         writeI2c(0x57,0x06,0,false);
}
bool configurePpgLeds(uint8_t red, uint8_t ir) {
  // MAX30102 datasheet: mode 3 = RED+IR. Stop conversions while changing both
  // amplitudes, verify writes, discard old FIFO/averaging data, then restart.
  // ADC range, sample rate and pulse width are deliberately unchanged.
  if(red<ppg::AutoExposure::Min || red>ppg::AutoExposure::Max ||
     ir<ppg::AutoExposure::Min || ir>ppg::AutoExposure::Max) return false;
  if(!writeI2c(0x57,0x09,0x83,false) || !writeI2c(0x57,0x0c,red,false) ||
     !writeI2c(0x57,0x0d,ir,false)) return false;
  uint8_t actual[2], mode=0;
  if(!readI2c(0x57,0x0c,actual,2) || actual[0]!=red || actual[1]!=ir ||
     !clearPpgFifo() || !writeI2c(0x57,0x09,0x03,false)) return false;
  return readI2c(0x57,0x09,&mode,1) && mode==0x03;
}
void acquisitionTask(void*) {
  SensorHealth health;
  ppg::AutoExposure exposure;
  uint16_t exposureEpoch=0;
  uint32_t exposureAppliedAt=0;
  uint32_t command = UINT32_MAX, nextEcg = 0, lastBack = 0, lastBattery = 0;
  uint32_t lastPpg = 0, lastPublish = 0, retryAds = 0, retryMax = 0, adcStart = 0;
  uint32_t conversionCommand = 0;
  int adcChannel = -1;
  auto send = [&](uint8_t kind, uint32_t ms, uint32_t a, uint32_t b = 0) {
    SensorSample sample{command, ms, a, b, kind, exposureEpoch};
    if (xQueueSend(sampleQueue, &sample, 0) != pdTRUE) ++health.dropped;
  };
  auto applyExposure = [&](uint8_t red, uint8_t ir, uint32_t now) {
    if(!configurePpgLeds(red,ir)) {
      health.max=false; ++health.faults; send(2,now,0); return false;
    }
    health.ledRed=red; health.ledIr=ir; ++exposureEpoch;
    exposureAppliedAt=millis();
    exposure.applied(exposureAppliedAt);
    if(command&3) send(4,exposureAppliedAt,red,ir);
    return true;
  };
  for (;;) {
    uint32_t now = millis(), us = micros();
    uint32_t requested = sensorCommand.load();
    bool autoRequested=ppgAutoEnabled.load();
    if (requested != command || autoRequested!=exposure.enabled()) {
      command = requested; nextEcg = us;
      exposure.reset(autoRequested,now);
      if (health.max) applyExposure(exposure.red(),exposure.ir(),now);
    }
    const int mode = command & 3;
    if (!health.ads && now - retryAds >= 1000) {
      retryAds = now;
      uint8_t bytes[2];
      health.ads = readI2c(ADS1115_ADDR, 1, bytes, 2);
      adcChannel = -1;
    }
    if (!health.max && now - retryMax >= 1000) {
      retryMax = now;
      uint8_t id = 0;
      health.max = readI2c(0x57, 0xff, &id, 1) && id == 0x15;
      if (health.max) {
        health.max = pulseSensor.begin(Wire, I2C_SPEED_FAST);
        if (health.max) {
          pulseSensor.setup(60, 4, 2, 100, 411, 4096); // 100/4 = RF's 25 Hz
          uint8_t config[3];
          health.max=readI2c(0x57,0x08,config,3) && (config[0]&0xf0)==0x50 &&
                     config[1]==0x03 && config[2]==0x27;
          exposure.reset(autoRequested,now);
          if(health.max) applyExposure(exposure.red(),exposure.ir(),now);
          else { ++health.faults; send(2,now,0); }
        }
      }
    }
    if (health.ads && adcChannel >= 0) {
      uint8_t bytes[2];
      if (!readI2c(ADS1115_ADDR, 1, bytes, 2)) health.ads = false;
      else if (bytes[0] & 0x80) {
        if (!readI2c(ADS1115_ADDR, 0, bytes, 2)) health.ads = false;
        else {
          int16_t raw = int16_t((uint16_t(bytes[0]) << 8) | bytes[1]);
          health.adcMs = now;
          if (adcChannel == 0 && command == conversionCommand && mode == 1)
            send(0, adcStart, raw > 0 ? raw : 0);
          if (adcChannel == 1) {
            float voltage = raw * 0.000125f * BATTERY_DIVIDER_RATIO;
            health.battery = health.battery == 0 ? voltage : 0.8f * health.battery + 0.2f * voltage;
          }
          if (adcChannel == 2) health.back = raw > (health.back ? 10000 : 15000);
        }
        adcChannel = -1;
      } else if (now - adcStart > 20) health.ads = false;
      if (!health.ads) { ++health.faults; health.back = false; adcChannel = -1; }
    }
    if (health.ads && adcChannel < 0) {
      int channel = -1;
      if (mode == 1 && int32_t(us - nextEcg) >= 0) {
        if (int32_t(us - nextEcg) > 1500) ++health.ecgLate;
        channel = ADS_CH_ECG;
        nextEcg += 4000;
        if (int32_t(us - nextEcg) >= 0) nextEcg = us + 4000;
      } else if (mode != 1 || int32_t(nextEcg - us) >= 1500 || now - lastBack >= 35) {
        if (now - lastBack >= 20) { channel = ADS_CH_BTN_BACK; lastBack = now; }
        else if (now - lastBattery >= 2000) { channel = ADS_CH_BATTERY; lastBattery = now; }
      }
      if (channel >= 0) {
        // Single shot, gain +/-4.096 V, 860 SPS, comparator disabled.
        uint16_t config = 0x83e3 | ((4 + channel) << 12);
        if (writeI2c(ADS1115_ADDR, 1, config, true)) {
          adcChannel = channel; adcStart = now; conversionCommand = command;
        } else { health.ads = false; health.back = false; ++health.faults; }
      }
    }
    if (health.max && now - lastPpg >= 5) {
      lastPpg = now;
      uint8_t pointers[3];
      if (!readI2c(0x57, 4, pointers, 3)) { health.max = false; ++health.faults; }
      else {
        int count = (pointers[0] - pointers[2]) & 31;
        if (pointers[1]) {
          ++health.faults; send(2, now, 0); exposure.invalidate(now);
          if(!clearPpgFifo()) health.max=false;
          count = 0;
        }
        // Read directly: SparkFun's tiny software FIFO can discard batched samples.
        for (int i = 0; i < count; ++i) {
          uint8_t bytes[6];
          if (!readI2c(0x57, 7, bytes, 6)) {
            health.max = false; ++health.faults; send(2, now, 0); break;
          }
          uint32_t red = ((uint32_t(bytes[0]) << 16) | (uint32_t(bytes[1]) << 8) | bytes[2]) & 0x3ffff;
          uint32_t ir = ((uint32_t(bytes[3]) << 16) | (uint32_t(bytes[4]) << 8) | bytes[5]) & 0x3ffff;
          health.ppgMs = now;
          if (mode) {
            uint32_t sampleMs=now-uint32_t(count-i-1)*40;
            auto decision=exposure.update(ir,red,sampleMs);
            if(decision.changed) {
              applyExposure(decision.red,decision.ir,now);
              // Old FIFO entries belong to the preceding exposure. Hardware
              // flush + epoch + settling gate keep them out of the next window.
              break;
            }
            // Fixed-current mode also settles after returning from auto mode
            // or reinitializing the sensor; it must not bypass the hold.
            bool settled=int32_t(sampleMs-exposureAppliedAt)>=240;
            send(decision.usable && settled?1:3,sampleMs,ir,red);
          }
        }
      }
    }
    if (now - lastPublish >= 20) {
      health.exposure=exposure.state();
      lastPublish = now; xQueueOverwrite(healthQueue, &health);
    }
    vTaskDelay(1);
  }
}
void startAcquisition() {
  sampleQueue = xQueueCreate(384, sizeof(SensorSample));
  healthQueue = xQueueCreate(1, sizeof(SensorHealth));
  if (sampleQueue && healthQueue)
    acquisitionStarted = xTaskCreate(acquisitionTask, "sensors", 4096, nullptr, 2, nullptr) == pdPASS;
}
