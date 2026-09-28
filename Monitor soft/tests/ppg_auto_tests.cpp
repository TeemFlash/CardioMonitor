#include "../MedicalMonitor/PpgAutoExposure.h"
#include <cassert>
#include <cstdio>
#include <limits>

using ppg::AutoExposure;
using ppg::ExposureDecision;
using ppg::ExposureState;

static ExposureDecision feed(AutoExposure& p, uint32_t& now, int count,
                             uint32_t ir = 100000, uint32_t red = 60000) {
  ExposureDecision d = {false, p.red(), p.ir(), false};
  for (int i = 0; i < count; ++i) { d = p.update(ir, red, now); now += 40; }
  return d;
}

static void stableAndIndependent() {
  AutoExposure p;
  uint32_t now = 0;
  // Initial settle 240 ms, then 25 samples spanning 960 ms.
  assert(!feed(p, now, 30).usable);
  assert(feed(p, now, 1).usable);
  assert(p.state() == ExposureState::Stable);
  assert(p.ir() == 60 && p.red() == 60);
  for (int i = 0; i < 500; ++i) {
    auto d = feed(p, now, 1);
    assert(d.usable && !d.changed);
  }

  // The same exposure change must not drag the healthy RED channel with IR.
  p.reset(true, now);
  auto d = feed(p, now, 31, 40000, 60000);
  assert(d.changed && !d.usable && d.ir == 75 && d.red == 60);
  assert(p.state() == ExposureState::Adjusting);
  assert(!feed(p, now, 29, 100000, 60000).usable);
  assert(feed(p, now, 1, 100000, 60000).usable);

  p.reset(true, now);
  d = feed(p, now, 31, 100000, 5000);
  assert(d.changed && d.red == 75 && d.ir == 60);

  // Normal modulation crossing an exposure target edge must not repeatedly
  // reset a previously accepted RF window. The exposure target uses the mean.
  p.reset(true, now);
  assert(feed(p, now, 31, 61000, 11000).usable);
  for (int i = 0; i < 150; ++i) {
    d = feed(p, now, 1, i % 2 ? 58000 : 64000, i % 2 ? 9500 : 12500);
    assert(d.usable && !d.changed);
  }

  // A sample below the RF IR floor is gated even on the final window sample.
  p.reset(true, now);
  assert(feed(p, now, 31, 61000, 60000).usable);
  feed(p, now, 24, 61000, 60000);
  assert(!feed(p, now, 1, 49000, 60000).usable);
}

static void convergenceAndLimits() {
  AutoExposure p;
  uint32_t now = 0;
  unsigned changes = 0;
  for (int i = 0; i < 600; ++i) {
    const auto oldIr = p.ir(), oldRed = p.red();
    // Healthy amplitude with lower transmission: output grows with LED current.
    const auto d = p.update(uint32_t(oldIr) * 800, uint32_t(oldRed) * 1100, now);
    now += 40;
    if (d.changed) {
      ++changes;
      assert(d.ir <= unsigned(oldIr) + oldIr / 4 && d.red == oldRed);
    }
  }
  assert(changes == 1 && p.ir() == 75 && p.red() == 60);
  assert(p.state() == ExposureState::Stable);

  p.reset(true, now);
  feed(p, now, 500, 20000, 5000);
  assert(p.ir() == AutoExposure::Max && p.red() == AutoExposure::Max);
  assert(p.state() == ExposureState::Weak);
  assert(!feed(p, now, 1, 20000, 5000).usable);

  p.reset(true, now);
  feed(p, now, 600, 200000, 60000);
  assert(p.ir() == AutoExposure::Min && p.red() == AutoExposure::Default);
  assert(p.state() == ExposureState::Clipped);
}

static void clipping() {
  AutoExposure p;
  uint32_t now = 0;
  auto d = p.update(250000, 0, now);
  assert(d.changed && d.ir == 45 && d.red == 60 && !d.usable);
  assert(p.state() == ExposureState::Clipped);
  for (now = 40; now < 500; now += 40) {
    d = p.update(262143, 0, now);
    assert(!d.changed && d.ir == 45 && !d.usable);
  }
  d = p.update(262143, 262143, now);
  assert(d.changed && d.ir == 34 && d.red == 45);
  uint32_t lastChange = now;
  for (int i = 0; i < 600; ++i) {
    now += 40;
    d = p.update(std::numeric_limits<uint32_t>::max(), 262143, now);
    if (d.changed) {
      assert(uint32_t(now - lastChange) >= 500);
      lastChange = now;
    }
    assert(!d.usable && d.ir >= AutoExposure::Min && d.red >= AutoExposure::Min);
  }
  assert(p.ir() == AutoExposure::Min && p.red() == AutoExposure::Min);
  assert(p.state() == ExposureState::Clipped);

  p.reset(true, now);
  assert(feed(p, now, 31).usable);
  d = feed(p, now, 1, 100000, 250000);
  assert(d.changed && !d.usable && d.ir == 60 && d.red == 45);
}

static void absentAndMotion() {
  AutoExposure p;
  uint32_t now = 0;
  feed(p, now, 200, 0, 0);
  assert(p.ir() == 60 && p.red() == 60 && p.state() == ExposureState::Searching);
  feed(p, now, 31, 40000, 60000);
  assert(p.ir() == 75);
  const uint32_t absentAt = now;
  unsigned restores = 0;
  for (int i = 0; i < 100; ++i) {
    auto d = p.update(0, 0, now);
    if (d.changed) { ++restores; assert(uint32_t(now - absentAt) >= 1000); }
    assert(!d.usable);
    now += 40;
  }
  assert(restores == 1 && p.ir() == 60 && p.red() == 60);
  assert(p.state() == ExposureState::Searching);

  p.reset(true, now);
  // Large excursions with a low mean never authorize raising the current.
  for (int i = 0; i < 400; ++i) {
    auto d = p.update(i % 2 ? 20000 : 50000, 60000, now);
    assert(!d.changed && !d.usable);
    now += 40;
  }
  assert(p.ir() == 60 && p.red() == 60 && p.state() == ExposureState::Unstable);
  feed(p, now, 50);
  assert(p.state() == ExposureState::Stable);
  assert(!feed(p, now, 1, 9999, 60000).usable);
  assert(p.state() == ExposureState::Searching);

  p.reset(true, now);
  assert(feed(p, now, 31).usable);
  feed(p, now, 1);
  assert(!feed(p, now, 1, 170000, 60000).usable);
  assert(p.state() == ExposureState::Unstable);
}

static void actualApplicationTime() {
  AutoExposure p;
  uint32_t now = 0;
  auto d = feed(p, now, 31, 40000, 60000);
  assert(d.changed && d.ir == 75);
  const uint32_t decisionAt = now - 40;
  const uint32_t appliedAt = decisionAt + 100;
  p.applied(appliedAt);
  assert(p.ir() == 75 && p.red() == 60);
  // The FIFO timestamp was 100 ms behind the completed physical write.
  // Clipping must not permit another change 500 ms after that old timestamp.
  for (now = appliedAt + 40; now < appliedAt + 500; now += 40) {
    d = p.update(250000, 60000, now);
    assert(!d.changed && !d.usable && d.ir == 75);
  }
  d = p.update(250000, 60000, appliedAt + 500);
  assert(d.changed && !d.usable && d.ir == 57 && d.red == 60);

  // The same guard applies to initialization, not just changed decisions.
  p.reset(true, 0);
  p.applied(100);
  assert(!p.update(250000, 60000, 140).changed);
  assert(p.update(250000, 60000, 600).changed);
}

static void timestampsResetAndOff() {
  AutoExposure p;
  uint32_t now = 0;
  feed(p, now, 31, 40000, 60000);
  assert(p.ir() == 75);
  p.invalidate(now);
  assert(p.ir() == 75 && p.red() == 60);
  assert(!feed(p, now, 30).usable);
  assert(feed(p, now, 1).usable);
  now += 81;
  assert(!feed(p, now, 1).usable);
  assert(!feed(p, now, 29).usable);
  assert(feed(p, now, 1).usable);

  now = std::numeric_limits<uint32_t>::max() - 400;
  p.reset(true, now);
  assert(!feed(p, now, 30).usable);
  assert(feed(p, now, 1).usable);
  for (int i = 0; i < 200; ++i) assert(feed(p, now, 1).usable);

  // A burst with fictitious repeated timestamps cannot establish exposure.
  p.reset(true, 0);
  for (int i = 0; i < 100; ++i) assert(!p.update(100000, 60000, 240).usable);
  assert(p.state() == ExposureState::Unstable);

  p.reset(false, now);
  assert(!p.enabled() && p.state() == ExposureState::Off);
  const uint32_t inputs[] = {0, 100000, 262143};
  for (auto ir : inputs) {
    auto d = feed(p, now, 500, ir, 0);
    assert(!d.changed && d.usable && d.ir == 60 && d.red == 60);
  }
  p.invalidate(now);
  assert(p.state() == ExposureState::Off && p.update(0, 0, now).usable);
}

int main() {
  stableAndIndependent();
  convergenceAndLimits();
  clipping();
  absentAndMotion();
  actualApplicationTime();
  timestampsResetAndOff();
  puts("PPG auto exposure: stability, independent channels, bounds, clipping, absence, motion, timing and off mode passed");
}
