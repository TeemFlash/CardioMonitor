#pragma once
#include <stdint.h>

namespace ppg {

// Exposure is an engineering check of raw optical levels, NOT a measurement of
// SpO2 accuracy, perfusion, or clinical signal quality. RF validity still applies.
enum class ExposureState : uint8_t {
  Off, Searching, Adjusting, Stable, Weak, Clipped, Unstable
};

struct ExposureDecision {
  bool changed;
  uint8_t red, ir;
  bool usable;
};

class AutoExposure {
 public:
  static constexpr uint8_t Min = 8, Max = 100, Default = 60;

  AutoExposure() { reset(); }

  void reset(bool enabled = true, uint32_t now = 0) {
    enabled_ = enabled;
    red_ = ir_ = Default;
    haveChange_ = false;
    lastChange_ = now;
    invalidate(now);
  }

  // Also call after an acquisition discontinuity/FIFO error. An interrupted
  // window can never authorize feeding samples into the pulse/SpO2 processor.
  void invalidate(uint32_t now) {
    clearWindow();
    usable_ = false;
    haveSample_ = false;
    absent_ = restored_ = false;
    settleSince_ = now;
    settling_ = enabled_;
    state_ = enabled_ ? ExposureState::Adjusting : ExposureState::Off;
  }

  // Call only after a checked hardware write has actually completed. A decision
  // can be based on an older FIFO sample: the physical application timestamp,
  // not that sample's timestamp, anchors settling and the clipping cooldown.
  void applied(uint32_t now) {
    haveChange_ = true;
    lastChange_ = now;
    invalidate(now);
  }

  uint8_t red() const { return red_; }
  uint8_t ir() const { return ir_; }
  ExposureState state() const { return state_; }
  bool enabled() const { return enabled_; }

  ExposureDecision update(uint32_t irValue, uint32_t redValue, uint32_t now) {
    if (!enabled_) return decision(false, true);

    if (haveSample_ && uint32_t(now - lastSample_) > 80) invalidate(now);
    lastSample_ = now;
    haveSample_ = true;

    // Clipping wins even if the other channel looks like an absent finger.
    // Reduce only the affected LED, including during an existing settle hold.
    if (irValue >= 250000 || redValue >= 250000) {
      usable_ = false;
      state_ = ExposureState::Clipped;
      clearWindow();
      absent_ = restored_ = false;
      if (!haveChange_ || uint32_t(now - lastChange_) >= 500) {
        const uint8_t nextIr = irValue >= 250000 ? reduce(ir_) : ir_;
        const uint8_t nextRed = redValue >= 250000 ? reduce(red_) : red_;
        if (nextIr != ir_ || nextRed != red_) {
          change(nextRed, nextIr, now);
          return decision(true, false);
        }
      }
      return decision(false, false);
    }

    // Do not ramp to maximum power without an optical candidate for a finger.
    // Return to the known starting current once per continuous absence.
    if (irValue < 10000 || redValue < 1000) {
      usable_ = false;
      state_ = ExposureState::Searching;
      clearWindow();
      if (!absent_) { absent_ = true; absentSince_ = now; restored_ = false; }
      if (!restored_ && uint32_t(now - absentSince_) >= 1000) {
        restored_ = true;
        if (red_ != Default || ir_ != Default) {
          change(Default, Default, now);
          return decision(true, false);
        }
      }
      return decision(false, false);
    }
    absent_ = restored_ = false;

    if (settling_) {
      if (uint32_t(now - settleSince_) < 240) return decision(false, false);
      settling_ = false;
      clearWindow();
    }

    // Revoke a sudden drop below the RF processor's IR floor immediately.
    // The wider exposure bands apply to window means: using them as per-sample
    // gates would reject normal pulsation around an otherwise acceptable mean.
    const bool weakNow = irValue < 50000;
    if (weakNow) {
      usable_ = false;
      state_ = ExposureState::Adjusting;
      if (ir_ == Max) state_ = ExposureState::Weak;
    }

    if (!count_) {
      windowSince_ = now;
      minIr_ = maxIr_ = irValue;
      minRed_ = maxRed_ = redValue;
    }
    if (irValue < minIr_) minIr_ = irValue;
    if (irValue > maxIr_) maxIr_ = irValue;
    if (redValue < minRed_) minRed_ = redValue;
    if (redValue > maxRed_) maxRed_ = redValue;
    sumIr_ += irValue;
    sumRed_ += redValue;
    ++count_;

    const bool unstable =
        uint64_t(maxIr_ - minIr_) * 100 * count_ > sumIr_ * 35 ||
        uint64_t(maxRed_ - minRed_) * 100 * count_ > sumRed_ * 35;
    if (unstable) {
      usable_ = false;
      state_ = ExposureState::Unstable;
    }
    if (count_ < 25) return decision(false, usable_);

    // At the configured 25 pairs/s a window spans 960 ms. Too-fast timestamps
    // cannot pass by supplying repeated or buffered samples at one instant.
    if (unstable || uint32_t(now - windowSince_) < 900) {
      usable_ = false;
      state_ = ExposureState::Unstable;
      clearWindow();
      return decision(false, false);
    }

    const uint32_t meanIr = uint32_t(sumIr_ / count_);
    const uint32_t meanRed = uint32_t(sumRed_ / count_);
    clearWindow();
    const uint8_t nextIr = adjust(ir_, meanIr, 60000, 100000);
    const uint8_t nextRed = adjust(red_, meanRed, 10000, 60000);
    if (nextIr != ir_ || nextRed != red_) {
      state_ = ExposureState::Adjusting;
      change(nextRed, nextIr, now);
      return decision(true, false);
    }
    if (meanIr > 180000 || meanRed > 180000) {
      state_ = ExposureState::Clipped;
      usable_ = false;
    } else if (meanIr < 60000 || meanRed < 10000) {
      state_ = ExposureState::Weak;
      usable_ = false;
    } else if (weakNow) {
      state_ = ExposureState::Adjusting;
      usable_ = false;
    } else {
      state_ = ExposureState::Stable;
      usable_ = true;
    }
    return decision(false, usable_);
  }

 private:
  bool enabled_ = true, usable_ = false, settling_ = true;
  bool haveSample_ = false, haveChange_ = false, absent_ = false, restored_ = false;
  uint8_t red_ = Default, ir_ = Default, count_ = 0;
  ExposureState state_ = ExposureState::Adjusting;
  uint32_t lastSample_ = 0, lastChange_ = 0, settleSince_ = 0;
  uint32_t absentSince_ = 0, windowSince_ = 0;
  uint32_t minIr_ = 0, maxIr_ = 0, minRed_ = 0, maxRed_ = 0;
  uint64_t sumIr_ = 0, sumRed_ = 0;

  ExposureDecision decision(bool changed, bool usable) const {
    return {changed, red_, ir_, usable};
  }

  void clearWindow() {
    count_ = 0;
    sumIr_ = sumRed_ = 0;
  }

  static uint8_t step(uint8_t current) {
    return current >= 4 ? uint8_t(current / 4) : uint8_t(1);
  }

  static uint8_t reduce(uint8_t current) {
    const int next = int(current) - step(current);
    return next < Min ? Min : uint8_t(next);
  }

  static uint8_t adjust(uint8_t current, uint32_t mean, uint32_t low, uint32_t target) {
    if (mean >= low && mean <= 180000) return current;
    // Candidate thresholds ensure mean is positive. Keep uint64_t for arbitrary
    // input tests and avoid float rounding changes between host and MCU builds.
    uint64_t next = (uint64_t(current) * target + mean / 2) / mean;
    const uint8_t delta = step(current);
    if (next > unsigned(current) + delta) next = unsigned(current) + delta;
    if (next < unsigned(current) - delta) next = unsigned(current) - delta;
    if (next < Min) next = Min;
    if (next > Max) next = Max;
    return uint8_t(next);
  }

  void change(uint8_t red, uint8_t ir, uint32_t now) {
    red_ = red;
    ir_ = ir;
    usable_ = false;
    haveChange_ = true;
    lastChange_ = settleSince_ = now;
    settling_ = true;
    clearWindow();
  }
};

}  // namespace ppg
