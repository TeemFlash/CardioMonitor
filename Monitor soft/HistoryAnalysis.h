#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Streaming review of this firmware's CSV files. No waveform, row list, or heap
// allocation is retained. Thresholds describe accepted device estimates; they
// are not diagnoses or a validation of sensor accuracy.
namespace cardio {

enum class Metric : uint8_t { Ecg, Pulse, Oxygen };
enum class Finding : uint8_t {
  Insufficient, WithinReference, HighRate, LowRate, MixedRate,
  BorderlineOxygen, LowOxygen, VeryLowOxygen
};

struct MetricSummary {
  uint64_t validMs = 0, highMs = 0, lowMs = 0;
  uint64_t longestHighMs = 0, longestLowMs = 0;
  float minimum = NAN, maximum = NAN;
  // Number of accepted logged rows, NOT independent measurements: the firmware
  // holds an estimate across many rows between algorithm updates.
  uint32_t samples = 0;

  double mean() const { return validMs ? weightedSum_ / double(validMs) : NAN; }
  float coveragePercent() const {
    return spanMs_ ? float(100.0 * double(validMs) / double(spanMs_)) : 0.0f;
  }
  // 30 s and 80% are conservative engineering review gates, not clinical
  // performance claims. No time after the final row is inferred.
  bool sufficient() const {
    return finished_ && trustworthy_ && validMs >= 30000 && spanMs_ &&
           double(validMs) >= double(spanMs_) * 0.80;
  }
  uint64_t longestLowOxygenMs() const { return longest92_; }
  uint64_t longestVeryLowOxygenMs() const { return longest88_; }
  bool warningLowOxygen() const { return oxygen_ && longest92_ >= 1000; }
  bool warningVeryLowOxygen() const { return oxygen_ && longest88_ >= 1000; }

  Finding finding(bool adultRest) const {
    if (oxygen_) {
      // Surface sustained concerning readings even in an otherwise short or
      // poor recording. The 1 s continuity gate is engineering evidence only;
      // it must not be presented as a medical waiting period or confirmation.
      if (warningVeryLowOxygen()) return Finding::VeryLowOxygen;
      if (warningLowOxygen()) return Finding::LowOxygen;
      if (longestLowMs >= 1000) return Finding::BorderlineOxygen;
    } else {
      if (!adultRest) return Finding::Insufficient;
      const bool high = longestHighMs >= 10000;
      const bool low = longestLowMs >= 10000;
      if (high && low) return Finding::MixedRate;
      if (high) return Finding::HighRate;
      if (low) return Finding::LowRate;
    }
    // Even one accepted out-of-reference reading prevents a reassuring result.
    return sufficient() && !abnormalSeen_ ? Finding::WithinReference : Finding::Insufficient;
  }

 private:
  friend class HistoryAnalysis;
  double weightedSum_ = 0;
  uint64_t spanMs_ = 0, highRun_ = 0, lowRun_ = 0;
  uint64_t run92_ = 0, run88_ = 0, longest92_ = 0, longest88_ = 0;
  int64_t lastMs_ = 0;
  float lastValue_ = 0;
  bool hasTime_ = false, lastValid_ = false, oxygen_ = false;
  bool abnormalSeen_ = false, finished_ = false, trustworthy_ = true;

  void breakContinuity() {
    lastValid_ = false;
    highRun_ = lowRun_ = run92_ = run88_ = 0;
  }
  bool isHigh(float v) const { return !oxygen_ && v > 100; }
  bool isLow(float v) const { return oxygen_ ? v < 95 : v < 60; }
  static void extend(bool condition, uint64_t dt, uint64_t& run, uint64_t& longest) {
    run = condition ? run + dt : 0;
    if (run > longest) longest = run;
  }
  // Caller has already rejected duplicate/reversed timestamps. A bad value is
  // still a timeline endpoint and must break every continuous abnormal run.
  bool add(int64_t ms, bool valid, float value, uint32_t maxGap) {
    const uint64_t dt = hasTime_ ? uint64_t(ms - lastMs_) : 0;
    const bool missing = hasTime_ && dt > maxGap;
    if (valid) {
      if (!samples) minimum = maximum = value;
      else { if (value < minimum) minimum = value; if (value > maximum) maximum = value; }
      if (samples < UINT32_MAX) ++samples;
      abnormalSeen_ = abnormalSeen_ || isHigh(value) || isLow(value);
    }
    if (valid && lastValid_ && dt && !missing) {
      validMs += dt;
      weightedSum_ += (double(lastValue_) + double(value)) * 0.5 * double(dt);
      const bool high = isHigh(lastValue_) && isHigh(value);
      const bool low = isLow(lastValue_) && isLow(value);
      if (high) highMs += dt;
      if (low) lowMs += dt;
      extend(high, dt, highRun_, longestHighMs);
      extend(low, dt, lowRun_, longestLowMs);
      if (oxygen_) {
        extend(lastValue_ <= 92 && value <= 92, dt, run92_, longest92_);
        extend(lastValue_ <= 88 && value <= 88, dt, run88_, longest88_);
      }
    } else breakContinuity();
    lastMs_ = ms; hasTime_ = true; lastValue_ = value; lastValid_ = valid;
    return missing;
  }
};

class HistoryAnalysis {
 public:
  // The SD reader must drain and reject longer physical lines, not feed their
  // prefix or remaining suffix as new CSV rows.
  static constexpr size_t MaxLineLength = 191;

  HistoryAnalysis() { reset(); }
  void reset() {
    for (unsigned i = 0; i < 3; ++i) metrics_[i] = MetricSummary{};
    metrics_[2].oxygen_ = true;
    schema_ = Schema::None;
    malformed_ = gaps_ = 0;
    haveSpan_ = finished_ = headerSeen_ = corrupt_ = ioError_ = false;
    firstMs_ = lastMs_ = 0;
  }
  const MetricSummary& metric(Metric which) const { return metrics_[unsigned(which)]; }
  uint64_t durationMs() const { return haveSpan_ ? uint64_t(lastMs_ - firstMs_) : 0; }
  uint32_t malformedRows() const { return malformed_; }
  uint32_t gapCount() const { return gaps_; }
  bool supported() const { return schema_ != Schema::None && schema_ != Schema::Unsupported; }
  bool complete() const { return finished_; }
  bool ioError() const { return ioError_; }
  void markCorruptLine() {
    if (finished_) return;
    if (malformed_ < UINT32_MAX) ++malformed_;
    corrupt_ = true;
    breakAll();
    updateSummaries();
  }
  void markIoError() {
    if (finished_) return;
    ioError_ = true;
    breakAll();
    updateSummaries();
  }
  void finish() {
    finished_ = true;
    updateSummaries();
  }

  void feedLine(const char* input) {
    if (finished_) return;
    if (!input) { markCorruptLine(); return; }
    size_t length = 0;
    while (length <= MaxLineLength && input[length]) ++length;
    if (length > MaxLineLength) { markCorruptLine(); return; }
    while (length && (input[length - 1] == '\r' || input[length - 1] == '\n')) --length;
    if (!length) return;
    char line[MaxLineLength + 1];
    memcpy(line, input, length); line[length] = 0;
    if (line[0] == '#') {
      if (!strncmp(line, "# acquisition_gap", 17) &&
          (!line[17] || line[17] == ' ' || line[17] == '\t')) {
        noteGap(); breakAll();
      } else if (!strncmp(line, "# ppg_gap", 9) &&
                 (!line[9] || line[9] == ' ' || line[9] == '\t')) {
        // A MAX30102 exposure/FIFO epoch reset invalidates only optical
        // continuity. Concurrent ADS1115 ECG samples remain independent.
        noteGap();
        metrics_[1].breakContinuity();
        metrics_[2].breakContinuity();
      }
      return;
    }
    if (!headerSeen_) {
      headerSeen_ = true;
      if (!strcmp(line, EcgHeader)) schema_ = Schema::Ecg;
      else if (!strcmp(line, PulseHeader)) schema_ = Schema::Pulse;
      else if (!strcmp(line, OxygenHeader)) schema_ = Schema::Oxygen;
      else schema_ = Schema::Unsupported;
      updateSummaries();
      return;
    }
    if (!supported()) return;
    char* fields[13];
    const size_t count = split(line, fields, 13);
    if (count != (schema_ == Schema::Ecg ? 13u : 8u)) { markCorruptLine(); return; }
    uint32_t time = 0;
    if (!unsignedNumber(fields[0], UINT32_MAX, time)) { markCorruptLine(); return; }
    if (schema_ == Schema::Ecg && !strcmp(fields[1], "ecg")) {
      uint32_t adc = 0;
      bool signalOk = false;
      float filtered = 0, rate = 0;
      bool haveRate = false;
      if (!unsignedNumber(fields[2], 65535, adc) || !number(fields[3], filtered) ||
          !flag(fields[4], signalOk) || !emptyRange(fields, 5, 12) ||
          !optionalRange(fields[12], 40, 200, rate, haveRate)) {
        markCorruptLine(); return;
      }
      const int64_t ms = unwrap(time);
      if (!newTime(metrics_[0], ms)) { markCorruptLine(); return; }
      observe(ms);
      const float normalizedAdc = float(adc) * 4095.0f / 26400.0f;
      const bool usableAdc = normalizedAdc >= 150 && normalizedAdc <= 3950;
      if (metrics_[0].add(ms, signalOk && haveRate && usableAdc, rate, 12)) noteGap();
    } else {
      const bool combined = schema_ == Schema::Ecg;
      if (combined && (strcmp(fields[1], "ppg") || !emptyRange(fields, 2, 5) || *fields[12])) {
        markCorruptLine(); return;
      }
      const unsigned irIndex = combined ? 5 : 1;
      const unsigned flagIndex = combined ? 7 : 3;
      const unsigned corrIndex = combined ? 9 : 5;
      const unsigned rateIndex = combined ? 10 : (schema_ == Schema::Pulse ? 7 : 6);
      const unsigned oxygenIndex = combined ? 11 : (schema_ == Schema::Pulse ? 6 : 7);
      uint32_t ir = 0, red = 0;
      bool hrValid = false, oxygenValid = false, haveRate = false, haveOxygen = false;
      float correlation = 0, rate = 0, oxygen = 0;
      if (!unsignedNumber(fields[irIndex], 262143, ir) ||
          !unsignedNumber(fields[irIndex + 1], 262143, red) ||
          !flag(fields[flagIndex], hrValid) || !flag(fields[flagIndex + 1], oxygenValid) ||
          !number(fields[corrIndex], correlation) || correlation < -1 || correlation > 1 ||
          !optionalRange(fields[rateIndex], 40, 180, rate, haveRate) ||
          !optionalRange(fields[oxygenIndex], 70, 100, oxygen, haveOxygen) ||
          (hrValid && !haveRate) || (oxygenValid && (!haveOxygen || !hrValid))) {
        markCorruptLine(); return;
      }
      const int64_t ms = unwrap(time);
      if (!newTime(metrics_[1], ms) || !newTime(metrics_[2], ms)) { markCorruptLine(); return; }
      observe(ms);
      // Correlation is saved by the producing algorithm. A claimed valid flag
      // must also agree with its documented red/IR coherence gate.
      const bool coherent = correlation >= 0.8f && ir >= 50000 && red >= 1000 &&
                            ir < 260000 && red < 260000;
      const bool gap = metrics_[1].add(ms, hrValid && coherent, rate, 60);
      metrics_[2].add(ms, oxygenValid && coherent, oxygen, 60);
      if (gap) noteGap();
    }
    updateSummaries();
  }

 private:
  enum class Schema : uint8_t { None, Unsupported, Ecg, Pulse, Oxygen };
  static constexpr const char* EcgHeader =
    "millis_ms,kind,adc_code,filtered_relative,signal_ok,ir,red,ppg_valid,spo2_valid,correlation,ppg_bpm,spo2,ecg_bpm";
  static constexpr const char* PulseHeader =
    "millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm";
  static constexpr const char* OxygenHeader =
    "millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2";
  MetricSummary metrics_[3];
  Schema schema_ = Schema::None;
  uint32_t malformed_ = 0, gaps_ = 0;
  bool haveSpan_ = false, finished_ = false, headerSeen_ = false, corrupt_ = false, ioError_ = false;
  int64_t firstMs_ = 0, lastMs_ = 0;

  void noteGap() { if (gaps_ < UINT32_MAX) ++gaps_; }
  void breakAll() { for (auto& summary : metrics_) summary.breakContinuity(); }
  void updateSummaries() {
    for (auto& summary : metrics_) {
      summary.spanMs_ = durationMs();
      summary.finished_ = finished_;
      summary.trustworthy_ = supported() && !corrupt_ && !ioError_;
    }
  }
  // Map a uint32 millis timestamp onto the nearest epoch of the latest row.
  // Streams may interleave out of timestamp order; each stream has its own
  // strict watermark. A continuous session must not have gaps >= 2^31 ms.
  int64_t unwrap(uint32_t ms) const {
    if (!haveSpan_) return ms;
    const uint32_t difference = ms - uint32_t(lastMs_);
    const int64_t delta = difference <= INT32_MAX ? int64_t(difference) :
      -int64_t(uint64_t(UINT32_MAX) - difference + 1);
    return lastMs_ + delta;
  }
  static bool newTime(const MetricSummary& summary, int64_t ms) {
    return !summary.hasTime_ || ms > summary.lastMs_;
  }
  void observe(int64_t ms) {
    if (!haveSpan_) { firstMs_ = lastMs_ = ms; haveSpan_ = true; }
    else { if (ms < firstMs_) firstMs_ = ms; if (ms > lastMs_) lastMs_ = ms; }
  }
  static size_t split(char* line, char** fields, size_t capacity) {
    size_t count = 1; fields[0] = line;
    for (char* cursor = line; *cursor; ++cursor) {
      if (*cursor != ',') continue;
      if (count == capacity) return capacity + 1;
      *cursor = 0; fields[count++] = cursor + 1;
    }
    return count;
  }
  static bool emptyRange(char** fields, unsigned begin, unsigned end) {
    for (unsigned i = begin; i < end; ++i) if (*fields[i]) return false;
    return true;
  }
  static bool flag(const char* text, bool& value) {
    if ((text[0] != '0' && text[0] != '1') || text[1]) return false;
    value = text[0] == '1'; return true;
  }
  static bool unsignedNumber(const char* text, uint32_t maximum, uint32_t& value) {
    if (!*text) return false;
    uint64_t accumulator = 0;
    for (const char* c = text; *c; ++c) {
      if (*c < '0' || *c > '9') return false;
      accumulator = accumulator * 10 + unsigned(*c - '0');
      if (accumulator > maximum) return false;
    }
    value = uint32_t(accumulator); return true;
  }
  static bool number(const char* text, float& value) {
    // CSV writer emits decimal fixed-point values only. Disallow permissive
    // strtof forms (whitespace, hex, inf/nan, exponent) and trailing junk.
    const char* c = text;
    if (*c == '-') ++c;
    if (*c < '0' || *c > '9') return false;
    while (*c >= '0' && *c <= '9') ++c;
    if (*c == '.') {
      ++c;
      if (*c < '0' || *c > '9') return false;
      while (*c >= '0' && *c <= '9') ++c;
    }
    if (*c) return false;
    value = strtof(text, nullptr);
    return isfinite(value);
  }
  static bool optionalRange(const char* text, float low, float high, float& value, bool& present) {
    present = *text != 0;
    return !present || (number(text, value) && value >= low && value <= high);
  }
};

// Physical CSV framing is separate from field parsing so an SD block reader
// cannot accidentally turn a long/truncated row into valid-looking fragments.
// Files written by the firmware terminate every physical line with LF.
class HistoryCsvStream {
 public:
  explicit HistoryCsvStream(HistoryAnalysis& target) : target_(target) { reset(); }
  void reset() {
    used_ = 0; dropping_ = finished_ = false;
    target_.reset();
  }
  void feed(const uint8_t* bytes, size_t count) {
    if (finished_) return;
    if (!bytes && count) {
      if (!dropping_) target_.markCorruptLine();
      used_ = 0; dropping_ = true;
      return;
    }
    for (size_t i = 0; i < count; ++i) {
      const uint8_t byte = bytes[i];
      if (byte == '\n') {
        if (!dropping_) {
          line_[used_] = 0;
          target_.feedLine(line_);
        }
        used_ = 0; dropping_ = false;
      } else if (!dropping_) {
        if (byte == 0 || used_ == HistoryAnalysis::MaxLineLength) {
          // Mark once for the whole physical line, even if it contains several
          // NULs or spans many read blocks. Never parse its prefix or suffix.
          target_.markCorruptLine();
          used_ = 0; dropping_ = true;
        } else line_[used_++] = char(byte);
      }
    }
  }
  void finish() {
    if (finished_) return;
    // A fully formed numeric prefix at unexpected EOF could be a partially
    // written longer value. It cannot safely serve as an accepted endpoint.
    if (used_ && !dropping_) target_.markCorruptLine();
    used_ = 0; dropping_ = false; finished_ = true;
    target_.finish();
  }

 private:
  HistoryAnalysis& target_;
  char line_[HistoryAnalysis::MaxLineLength + 1] = {};
  size_t used_ = 0;
  bool dropping_ = false, finished_ = false;
};

}  // namespace cardio
