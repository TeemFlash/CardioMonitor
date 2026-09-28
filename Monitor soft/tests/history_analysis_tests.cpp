#include "../MedicalMonitor/HistoryAnalysis.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using cardio::Finding;
using cardio::HistoryAnalysis;
using cardio::HistoryCsvStream;
using cardio::Metric;

static const char* PulseHeader = "millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm";
static const char* OxygenHeader = "millis_ms,ir,red,hr_valid,spo2_valid,correlation,bpm,spo2";
static const char* EcgHeader = "millis_ms,kind,adc_code,filtered_relative,signal_ok,ir,red,ppg_valid,spo2_valid,correlation,ppg_bpm,spo2,ecg_bpm";

static void optical(HistoryAnalysis& analysis, uint32_t time, float rate = 75, float oxygen = 98,
                    bool valid = true, int schema = 0) {
  char line[192], bpm[16] = "", spo2[16] = "";
  if (valid) { snprintf(bpm, sizeof(bpm), "%.1f", rate); snprintf(spo2, sizeof(spo2), "%.1f", oxygen); }
  if (schema == 2)
    snprintf(line, sizeof(line), "%u,ppg,,,,100000,90000,%d,%d,0.950,%s,%s,", time, valid, valid, bpm, spo2);
  else
    snprintf(line, sizeof(line), "%u,100000,90000,%d,%d,0.950,%s,%s", time, valid, valid,
             schema == 0 ? spo2 : bpm, schema == 0 ? bpm : spo2);
  analysis.feedLine(line);
}

static void ecg(HistoryAnalysis& analysis, uint32_t time, float rate = 75, bool valid = true) {
  char line[192], bpm[16] = "";
  if (valid) snprintf(bpm, sizeof(bpm), "%.1f", rate);
  snprintf(line, sizeof(line), "%u,ecg,13000,-20.50,%d,,,,,,,,%s", time, valid, bpm);
  analysis.feedLine(line);
}

static void series(HistoryAnalysis& analysis, uint32_t start, unsigned duration,
                   float rate = 75, float oxygen = 98, int schema = 0) {
  for (unsigned t = 0; t <= duration; t += 40) optical(analysis, start + t, rate, oxygen, true, schema);
}

static void basicAndHeaders() {
  HistoryAnalysis a;
  assert(sizeof(a) < 1024);  // RAM stays bounded independently of file length.
  assert(!a.supported() && !a.complete() && a.durationMs() == 0);
  assert(std::isnan(a.metric(Metric::Pulse).mean()));
  assert(std::isnan(a.metric(Metric::Pulse).minimum));
  a.feedLine("# firmware metadata"); a.feedLine(""); a.feedLine(PulseHeader);
  assert(a.supported());
  series(a, 0, 30000);
  assert(!a.metric(Metric::Pulse).sufficient());
  a.finish();
  const auto& pulse = a.metric(Metric::Pulse);
  assert(a.complete() && a.durationMs() == 30000 && pulse.validMs == 30000);
  assert(pulse.samples == 751 && pulse.mean() == 75 && pulse.coveragePercent() == 100);
  assert(pulse.sufficient() && pulse.finding(true) == Finding::WithinReference);
  assert(pulse.finding(false) == Finding::Insufficient);
  assert(a.metric(Metric::Oxygen).finding(false) == Finding::WithinReference);
  assert(!a.metric(Metric::Ecg).sufficient());
  // Once EOF is marked, neither later data nor errors can change the report.
  optical(a, 30040, 150, 80); a.markIoError(); a.markCorruptLine();
  assert(a.durationMs() == 30000 && !a.ioError());

  a.reset(); a.feedLine(OxygenHeader); series(a, 0, 30000, 80, 97, 1); a.finish();
  assert(a.metric(Metric::Pulse).mean() == 80 && a.metric(Metric::Oxygen).mean() == 97);
  a.reset(); a.feedLine("millis_ms,ir,red,bpm,spo2"); series(a, 0, 30000); a.finish();
  assert(!a.supported() && !a.metric(Metric::Pulse).sufficient());
  a.reset(); a.feedLine(PulseHeader); optical(a, 0); a.finish();
  assert(a.metric(Metric::Pulse).samples == 1 && a.metric(Metric::Pulse).validMs == 0);
  assert(std::isnan(a.metric(Metric::Pulse).mean()));
  assert(a.metric(Metric::Pulse).finding(true) == Finding::Insufficient);
}

static void thresholdsAndEpisodes() {
  HistoryAnalysis a;
  // Adult resting reference endpoints are included; oxygen 95 is included.
  for (float rate : {60.0f, 100.0f}) {
    a.reset(); a.feedLine(PulseHeader); series(a, 0, 30000, rate, 95); a.finish();
    assert(a.metric(Metric::Pulse).finding(true) == Finding::WithinReference);
    assert(a.metric(Metric::Oxygen).finding(false) == Finding::WithinReference);
  }
  for (float rate : {59.9f, 100.1f}) {
    a.reset(); a.feedLine(PulseHeader); series(a, 0, 10000, rate); a.finish();
    assert(!a.metric(Metric::Pulse).sufficient());
    assert(a.metric(Metric::Pulse).finding(true) == (rate < 60 ? Finding::LowRate : Finding::HighRate));
    assert(a.metric(Metric::Pulse).finding(false) == Finding::Insufficient);
  }
  for (float rate : {40.0f, 180.0f}) {
    a.reset(); a.feedLine(PulseHeader); series(a, 0, 10000, rate); a.finish();
    assert(a.malformedRows() == 0 && a.metric(Metric::Pulse).samples == 251);
  }
  // A normal average cannot hide a sustained high-rate episode.
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 10000, 120);
  series(a, 10040, 40000, 75); a.finish();
  assert(a.metric(Metric::Pulse).mean() < 100);
  assert(a.metric(Metric::Pulse).finding(true) == Finding::HighRate);
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 10000, 120);
  series(a, 10040, 10000, 50); a.finish();
  assert(a.metric(Metric::Pulse).finding(true) == Finding::MixedRate);

  for (float oxygen : {94.9f, 94.0f, 92.1f, 92.0f, 88.1f, 88.0f, 70.0f}) {
    a.reset(); a.feedLine(PulseHeader); series(a, 0, 1000, 75, oxygen); a.finish();
    const auto& o = a.metric(Metric::Oxygen);
    assert(!o.sufficient() && o.validMs == 1000);
    const Finding expected = oxygen <= 88 ? Finding::VeryLowOxygen :
                             oxygen <= 92 ? Finding::LowOxygen : Finding::BorderlineOxygen;
    assert(o.finding(false) == expected);
    assert(o.warningLowOxygen() == (oxygen <= 92));
    assert(o.warningVeryLowOxygen() == (oxygen <= 88));
  }
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 960, 75, 88); a.finish();
  assert(!a.metric(Metric::Oxygen).warningVeryLowOxygen());
  assert(a.metric(Metric::Oxygen).finding(false) == Finding::Insufficient);
  // An isolated accepted low value is not a sustained warning, but rules out a
  // reassuring summary even with high coverage and an otherwise normal mean.
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 30000);
  optical(a, 30040, 75, 88); optical(a, 30080); a.finish();
  assert(a.metric(Metric::Oxygen).sufficient());
  assert(a.metric(Metric::Oxygen).finding(false) == Finding::Insufficient);
  assert(a.metric(Metric::Oxygen).minimum == 88);
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 1000, 75, 88);
  series(a, 1040, 30000); a.finish();
  assert(a.metric(Metric::Oxygen).sufficient() && a.metric(Metric::Oxygen).mean() > 95);
  assert(a.metric(Metric::Oxygen).finding(false) == Finding::VeryLowOxygen);
}

static void evidenceAndContinuity() {
  HistoryAnalysis a; a.feedLine(PulseHeader);
  for (unsigned t = 0; t <= 40000; t += 40) optical(a, t, 75, 98, t >= 8000);
  a.finish();
  assert(a.metric(Metric::Pulse).validMs == 32000);
  assert(a.metric(Metric::Pulse).coveragePercent() == 80);
  assert(a.metric(Metric::Pulse).sufficient());
  a.reset(); a.feedLine(PulseHeader);
  for (unsigned t = 0; t <= 40000; t += 40) optical(a, t, 75, 98, t >= 8040);
  a.finish(); assert(!a.metric(Metric::Pulse).sufficient());

  // Repeated held estimates count elapsed signal time, not 751 independent
  // measurements or 751 seconds. The trailing hold never extends the record.
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 29960); a.finish();
  assert(!a.metric(Metric::Pulse).sufficient() && a.metric(Metric::Pulse).validMs == 29960);
  a.reset(); a.feedLine(PulseHeader); optical(a, 0, 75); optical(a, 40, 85); a.finish();
  assert(a.metric(Metric::Pulse).mean() == 80);  // elapsed-time trapezoidal mean

  a.reset(); a.feedLine(PulseHeader); series(a, 0, 9960, 120);
  optical(a, 10000, 120, 98, false); series(a, 10040, 9960, 120); a.finish();
  assert(a.metric(Metric::Pulse).highMs == 19920);
  assert(a.metric(Metric::Pulse).longestHighMs == 9960);
  assert(a.metric(Metric::Pulse).finding(true) == Finding::Insufficient);

  a.reset(); a.feedLine(PulseHeader); series(a, 0, 480, 75, 88);
  a.feedLine("# acquisition_gap"); series(a, 520, 480, 75, 88); a.finish();
  assert(a.gapCount() == 1 && a.metric(Metric::Oxygen).longestVeryLowOxygenMs() == 480);
  assert(!a.metric(Metric::Oxygen).warningVeryLowOxygen());
  // Missing samples beyond the supported interval never create evidence.
  a.reset(); a.feedLine(PulseHeader); optical(a, 0, 120, 88); optical(a, 60000, 120, 88); a.finish();
  assert(a.durationMs() == 60000 && a.metric(Metric::Pulse).validMs == 0 && a.gapCount() == 1);
  assert(a.metric(Metric::Pulse).finding(true) == Finding::Insufficient);
  a.reset(); a.feedLine(PulseHeader); optical(a, 0); optical(a, 60); optical(a, 121); a.finish();
  assert(a.metric(Metric::Pulse).validMs == 60 && a.gapCount() == 1);
  a.reset(); a.feedLine(EcgHeader); ecg(a, 0); ecg(a, 12); ecg(a, 25); a.finish();
  assert(a.metric(Metric::Ecg).validMs == 12 && a.gapCount() == 1);
}

static void mixedStreamsAndClock() {
  HistoryAnalysis a; a.feedLine(EcgHeader);
  // Both streams can have the same timestamp. PPG rows can also arrive later
  // than newer ECG rows due to their separate FIFO; both timelines stay valid.
  for (unsigned t = 0; t <= 30000; t += 4) {
    ecg(a, t, 72);
    if (t % 40 == 0) optical(a, t, 75, 98, true, 2);
  }
  a.finish();
  assert(a.malformedRows() == 0 && a.durationMs() == 30000);
  assert(a.metric(Metric::Ecg).validMs == 30000 && a.metric(Metric::Pulse).validMs == 30000);
  assert(a.metric(Metric::Ecg).mean() == 72 && a.metric(Metric::Pulse).mean() == 75);
  a.reset(); a.feedLine(EcgHeader); ecg(a, 44); optical(a, 40, 75, 98, true, 2);
  ecg(a, 48); ecg(a, 52); optical(a, 80, 75, 98, true, 2); a.finish();
  assert(a.durationMs() == 40 && a.metric(Metric::Pulse).validMs == 40 && a.metric(Metric::Ecg).validMs == 8);
  a.reset(); a.feedLine(EcgHeader);
  a.feedLine("0,ecg,13000,0.00,1,,,,,,,,");  // quality may precede a first beat estimate
  ecg(a, 4, 200); ecg(a, 8, 200); a.finish();
  assert(a.malformedRows() == 0 && a.metric(Metric::Ecg).validMs == 4);

  a.reset(); a.feedLine(PulseHeader); series(a, UINT32_MAX - 10000, 40000); a.finish();
  assert(a.durationMs() == 40000 && a.metric(Metric::Pulse).validMs == 40000);
  assert(a.metric(Metric::Pulse).sufficient());
  // Duplicate and reversed per-stream timestamps break continuity. Earlier
  // high-water marks are retained so replayed rows cannot double-count time.
  a.reset(); a.feedLine(PulseHeader); optical(a, 0); optical(a, 40);
  optical(a, 40, 120, 88); optical(a, 20, 120, 88); optical(a, 80); optical(a, 120); a.finish();
  assert(a.malformedRows() == 2 && a.metric(Metric::Pulse).validMs == 80);
  assert(a.metric(Metric::Oxygen).minimum == 98 && a.metric(Metric::Pulse).maximum == 75);
}

static void opticalEpochGaps() {
  HistoryAnalysis a;
  for (const char* marker : {"# ppg_gap", "# ppg_gap ", "# ppg_gap\tepoch change"}) {
    a.reset(); a.feedLine(EcgHeader);
    for (unsigned t = 0; t <= 19600; t += 4) {
      if (t == 9800) a.feedLine(marker);
      ecg(a, t, 120);
      if (t % 40 == 0) optical(a, t, 120, 88, true, 2);
    }
    a.finish();
    assert(a.gapCount() == 1 && a.malformedRows() == 0);
    assert(a.metric(Metric::Ecg).validMs == 19600 && a.metric(Metric::Ecg).longestHighMs == 19600);
    assert(a.metric(Metric::Ecg).finding(true) == Finding::HighRate);
    assert(a.metric(Metric::Pulse).validMs == 19560 && a.metric(Metric::Pulse).longestHighMs == 9800);
    assert(a.metric(Metric::Pulse).finding(true) == Finding::Insufficient);
    assert(a.metric(Metric::Oxygen).longestVeryLowOxygenMs() == 9800);
  }
  // Exposure details and lookalike comment names carry no continuity meaning.
  for (const char* marker : {"# ppg_epoch=7", "# ppg_led,60,8", "# ppg_gapless", "# ppg_gap=7"}) {
    a.reset(); a.feedLine(EcgHeader);
    ecg(a, 0); optical(a, 0, 75, 98, true, 2);
    a.feedLine(marker);
    ecg(a, 4); optical(a, 40, 75, 98, true, 2); a.finish();
    assert(a.gapCount() == 0 && a.metric(Metric::Ecg).validMs == 4 && a.metric(Metric::Pulse).validMs == 40);
  }
  // The older full-acquisition marker still splits both independent streams.
  a.reset(); a.feedLine(EcgHeader);
  ecg(a, 0); optical(a, 0, 75, 98, true, 2);
  a.feedLine("# acquisition_gap"); ecg(a, 4); optical(a, 40, 75, 98, true, 2); a.finish();
  assert(a.gapCount() == 1 && a.metric(Metric::Ecg).validMs == 0 && a.metric(Metric::Pulse).validMs == 0);
}

static void malformedAndIo() {
  const char* badRows[] = {
    "0,100000,90000,1,1,0.950,98,NaN", "0,100000,90000,1,1,0.950,inf,75",
    "0,100000,90000,1,1,0.950,101,75", "0,100000,90000,1,1,0.950,69.9,75",
    "0,100000,90000,1,1,0.950,98,180.1", "0,100000,90000,1,1,0.950,98,39.9",
    "0,100000,90000,2,1,0.950,98,75", "0,100000,90000,true,1,0.950,98,75",
    "0,100000,90000,1,1,0.950,98,", "0,100000,90000,0,1,0.950,98,75",
    "0,100000,90000,1,1,nan,98,75", "0,100000,90000,1,1,1.1,98,75",
    "-1,100000,90000,1,1,0.950,98,75", "4294967296,100000,90000,1,1,0.950,98,75",
    "0,262144,90000,1,1,0.950,98,75", "0,100000,90000,1,1,0.950,98,75,",
    "0,100000,90000,1,1,0.950,98,75junk", "0,100000,90000,1,1,0.950,98, 75",
    "0,100000,90000,1,1,0.950,98,0x4b", "0,100000,90000,1,1,0.950,98,7.5e1",
    "0,100000,90000,1,1,0.950,98", "0,100000,90000,0,0,0.950,,NaN",
  };
  HistoryAnalysis a;
  for (const char* row : badRows) {
    a.reset(); a.feedLine(PulseHeader); a.feedLine(row); a.finish();
    assert(a.malformedRows() == 1 && a.durationMs() == 0 && a.metric(Metric::Pulse).samples == 0);
  }
  a.reset(); a.feedLine(PulseHeader); a.feedLine("0,100000,90000,1,1,0.700,98,75"); a.finish();
  assert(a.malformedRows() == 0 && a.metric(Metric::Pulse).samples == 0);
  for (const char* row : {"0,49999,90000,1,1,0.950,98,75", "0,260000,90000,1,1,0.950,98,75",
                          "0,100000,999,1,1,0.950,98,75", "0,100000,260000,1,1,0.950,98,75"}) {
    a.reset(); a.feedLine(PulseHeader); a.feedLine(row); a.finish();
    assert(a.malformedRows() == 0 && a.metric(Metric::Pulse).samples == 0);
  }
  for (const char* row : {"0,ecg,0,0.00,1,,,,,,,,75", "0,ecg,26400,0.00,1,,,,,,,,75"}) {
    a.reset(); a.feedLine(EcgHeader); a.feedLine(row); a.finish();
    assert(a.malformedRows() == 0 && a.metric(Metric::Ecg).samples == 0);
  }
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 30000);
  char overlong[256]; memset(overlong, '1', sizeof(overlong) - 1); overlong[sizeof(overlong) - 1] = 0;
  a.feedLine(overlong); a.finish();
  assert(a.malformedRows() == 1 && !a.metric(Metric::Pulse).sufficient());
  assert(a.metric(Metric::Pulse).finding(true) == Finding::Insufficient);
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 30000); a.markIoError(); a.finish();
  assert(a.ioError() && !a.metric(Metric::Pulse).sufficient());
  // An error later in a file must not erase already observed concerning data.
  a.reset(); a.feedLine(PulseHeader); series(a, 0, 1000, 75, 88);
  a.markIoError(); a.markCorruptLine(); a.finish();
  assert(a.metric(Metric::Oxygen).finding(false) == Finding::VeryLowOxygen);
  a.reset(); a.feedLine(PulseHeader); a.feedLine(nullptr); a.finish(); assert(a.malformedRows() == 1);
  a.reset(); a.feedLine("millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm\r\n");
  a.feedLine("0,100000,90000,1,1,0.950,98,75\r\n");
  a.feedLine("40,100000,90000,1,1,0.950,98,75\r"); a.finish();
  assert(a.metric(Metric::Pulse).validMs == 40 && a.malformedRows() == 0);
}

static void boundedByteFraming() {
  const char* file = "# firmware metadata\r\n"
    "millis_ms,ir,red,hr_valid,spo2_valid,correlation,spo2,bpm\r\n"
    "0,100000,90000,1,1,0.950,98,75\r\n"
    "40,100000,90000,1,1,0.950,98,75\n"
    "80,100000,90000,1,1,0.950,98,75\n";
  const size_t length = strlen(file);
  HistoryAnalysis a; HistoryCsvStream stream(a);
  assert(sizeof(stream) <= 256);
  // All two-block splits cover boundaries in CRLF, headers, numeric fields and
  // LF. Tiny fixed chunks cover repeatedly split rows, not just one split.
  for (size_t split = 0; split <= length; ++split) {
    stream.reset();
    stream.feed(reinterpret_cast<const uint8_t*>(file), split);
    stream.feed(reinterpret_cast<const uint8_t*>(file + split), length - split);
    stream.finish();
    assert(a.supported() && a.complete() && a.malformedRows() == 0);
    assert(a.metric(Metric::Pulse).validMs == 80 && a.metric(Metric::Pulse).samples == 3);
  }
  for (size_t chunk = 1; chunk <= 17; ++chunk) {
    stream.reset();
    for (size_t offset = 0; offset < length; offset += chunk) {
      const size_t take = length - offset < chunk ? length - offset : chunk;
      stream.feed(reinterpret_cast<const uint8_t*>(file + offset), take);
    }
    stream.finish();
    assert(a.metric(Metric::Pulse).validMs == 80 && a.malformedRows() == 0);
  }
  const auto bytes = [&stream](const char* text) {
    stream.feed(reinterpret_cast<const uint8_t*>(text), strlen(text));
  };
  stream.reset(); bytes(PulseHeader); bytes("\n");
  // A valid-looking prefix followed by a long physical line must be discarded
  // as a whole, even when the prefix and suffix arrive in separate SD reads.
  bytes("0,100000,90000,1,1,0.950,98,75");
  char padding[512]; memset(padding, '0', sizeof(padding));
  stream.feed(reinterpret_cast<const uint8_t*>(padding), sizeof(padding));
  assert(a.malformedRows() == 1 && a.metric(Metric::Pulse).samples == 0);
  bytes("\n40,100000,90000,1,1,0.950,98,75\n80,100000,90000,1,1,0.950,98,75\n");
  stream.finish();
  assert(a.malformedRows() == 1 && a.metric(Metric::Pulse).samples == 2);
  assert(a.metric(Metric::Pulse).validMs == 40);

  stream.reset(); bytes(PulseHeader); bytes("\n");
  const char nulRow[] = "0,100000,90000,1,1,0.950,98,75\0hidden\0suffix\n";
  stream.feed(reinterpret_cast<const uint8_t*>(nulRow), sizeof(nulRow) - 1);
  bytes("40,100000,90000,1,1,0.950,98,75\n80,100000,90000,1,1,0.950,98,75\n");
  stream.finish();
  assert(a.malformedRows() == 1 && a.metric(Metric::Pulse).samples == 2);

  stream.reset(); bytes(PulseHeader); bytes("\n0,100000,90000,1,1,0.950,98,75\n");
  bytes("40,100000,90000,1,1,0.950,98,75"); stream.finish();
  assert(a.malformedRows() == 1 && a.metric(Metric::Pulse).samples == 1);
  assert(a.metric(Metric::Pulse).validMs == 0);
  bytes("\n80,100000,90000,1,1,0.950,98,75\n"); stream.finish();
  assert(a.malformedRows() == 1 && a.metric(Metric::Pulse).samples == 1);

  stream.reset(); bytes(PulseHeader); bytes("\n");
  stream.feed(reinterpret_cast<const uint8_t*>(padding), sizeof(padding)); stream.finish();
  assert(a.malformedRows() == 1);  // already rejected; EOF does not count twice
  stream.reset(); bytes(PulseHeader); stream.finish();
  assert(!a.supported() && a.malformedRows() == 1);
  stream.reset(); stream.finish();
  assert(!a.supported() && a.complete() && a.malformedRows() == 0);
  stream.reset(); bytes(file); a.markIoError(); stream.finish();
  assert(a.ioError() && a.metric(Metric::Pulse).validMs == 80);
}

int main() {
  basicAndHeaders(); thresholdsAndEpisodes(); evidenceAndContinuity();
  mixedStreamsAndClock(); opticalEpochGaps(); malformedAndIo(); boundedByteFraming();
  puts("History analysis: schemas, evidence, coverage, episodes, gaps, mixed streams, wrap, corruption and byte framing passed");
}
