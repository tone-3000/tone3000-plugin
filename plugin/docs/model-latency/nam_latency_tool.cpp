// nam_latency_tool.cpp
//
// Command-line harness for nam_latency.h, built on NeuralAmpModelerCore.
// Loads .nam models (and .wav IRs, convolved directly) and prints the
// latency / polarity each analyzer measures, plus how long each step took.
//
//   ./plugin/docs/model-latency/build.sh
//   build/nam_latency_tool [options] file.nam [file2.nam ... ir.wav ...]
//
// Options:
//   --delay N        insert N samples of delay after the processor (self-test:
//                    the measurement should move by exactly N)
//   --invert         flip output polarity (self-test: `inverted` should flip)
//   --di file.wav    render this real DI through the processor and write
//                    <model>-<di>-aligned.wav: left = processor output,
//                    right = the DI delayed by the measured latency
//                    (polarity corrected). Zoom in on any pick attack in a
//                    DAW to see the alignment. (Correlating real guitar
//                    against the output is *not* a measurement: the signal
//                    is periodic and LF-heavy, the peak lands on pitch
//                    periods.)
//   --di-seconds X   only render the first X seconds of the DI (default: all)
//   --di-gain-db X   gain applied to the DI before the processor (default 0)
//   --probe lin|exp  sweep shape (default lin; exp is what the plugin uses)
//   --probe-ms X     sweep length (default 100)
//   --max-lag-ms X   lag search window (default 10)
//   --amp X          probe amplitude, linear (default 0.1 = -20 dBFS)
//   --click-amp X    click amplitude for the impulse method (default 0.1 = -20 dBFS)
//   --block N        process block size (default 256)
//   --taps N         print the first N taps of the correlation (default 24)
//   --csv            one machine-readable line per file instead of the report
//                    (header first; load failures get an "error" row)
//
// See build.sh and ../model-latency.md.

#include "nam_latency.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// --------------------------------------------------------------------------
// Tiny WAV reader: PCM 16/24/32 and float32, first channel only.
// --------------------------------------------------------------------------
bool readWav(const std::string& path, std::vector<double>& out, double& sampleRate) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::vector<char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    return false;

  auto u16 = [&](size_t p) { return static_cast<uint16_t>(static_cast<uint8_t>(bytes[p]) | (static_cast<uint8_t>(bytes[p + 1]) << 8)); };
  auto u32 = [&](size_t p) { return static_cast<uint32_t>(u16(p)) | (static_cast<uint32_t>(u16(p + 2)) << 16); };

  uint16_t format = 0, channels = 0, bits = 0;
  size_t dataPos = 0, dataLen = 0;
  for (size_t p = 12; p + 8 <= bytes.size();) {
    const uint32_t len = u32(p + 4);
    if (std::memcmp(bytes.data() + p, "fmt ", 4) == 0) {
      format = u16(p + 8);
      channels = u16(p + 10);
      sampleRate = u32(p + 12);
      bits = u16(p + 22);
      if (format == 0xFFFE && len >= 26) format = u16(p + 32);  // WAVE_FORMAT_EXTENSIBLE
    } else if (std::memcmp(bytes.data() + p, "data", 4) == 0) {
      dataPos = p + 8;
      dataLen = std::min<size_t>(len, bytes.size() - dataPos);
    }
    p += 8 + len + (len & 1);
  }
  if (!dataPos || !channels || !bits) return false;

  const size_t frameBytes = static_cast<size_t>(channels) * (bits / 8);
  const size_t frames = dataLen / frameBytes;
  out.resize(frames);
  for (size_t i = 0; i < frames; ++i) {
    const size_t p = dataPos + i * frameBytes;  // channel 0
    double v = 0.0;
    if (format == 3 && bits == 32) {
      float fv; std::memcpy(&fv, bytes.data() + p, 4); v = fv;
    } else if (bits == 16) {
      v = static_cast<int16_t>(u16(p)) / 32768.0;
    } else if (bits == 24) {
      int32_t s = (static_cast<uint8_t>(bytes[p]) | (static_cast<uint8_t>(bytes[p + 1]) << 8) |
                   (static_cast<uint8_t>(bytes[p + 2]) << 16));
      if (s & 0x800000) s |= ~0xFFFFFF;
      v = s / 8388608.0;
    } else if (bits == 32) {
      v = static_cast<int32_t>(u32(p)) / 2147483648.0;
    } else {
      return false;
    }
    out[i] = v;
  }
  return true;
}

// --------------------------------------------------------------------------
// Processors under test
// --------------------------------------------------------------------------

struct Processor {
  virtual ~Processor() = default;
  virtual void reset(double sampleRate, int blockSize) = 0;
  virtual void process(const double* in, double* out, int n) = 0;
  // Natural rate of the thing (NAM: model rate; IR: file rate).
  double sampleRate = 48000.0;
};

struct NamProcessor : Processor {
  std::unique_ptr<nam::DSP> dsp;
  explicit NamProcessor(const std::string& path) {
    try {
      dsp = nam::get_dsp(std::filesystem::path(path));
    } catch (const std::exception& e) {
      std::fprintf(stderr, "  %s\n", e.what());
    }
    if (dsp && dsp->GetExpectedSampleRate() > 0) sampleRate = dsp->GetExpectedSampleRate();
  }
  void reset(double sr, int blockSize) override { dsp->Reset(sr, blockSize); }  // prewarms
  void process(const double* in, double* out, int n) override {
    NAM_SAMPLE* ip = const_cast<NAM_SAMPLE*>(in);
    NAM_SAMPLE* op = out;
    dsp->process(&ip, &op, n);
  }
};

// Direct-form convolution with the first `maxSeconds` of an IR. Plenty for
// a latency measurement: the onset lives at the front of the kernel.
struct IrProcessor : Processor {
  std::vector<double> h;
  std::vector<double> hist;
  size_t pos = 0;
  bool ok = false;
  explicit IrProcessor(const std::string& path, double maxSeconds = 0.25) {
    ok = readWav(path, h, sampleRate);
    if (ok) h.resize(std::min(h.size(), static_cast<size_t>(maxSeconds * sampleRate)));
  }
  void reset(double, int) override { hist.assign(h.size(), 0.0); pos = 0; }
  void process(const double* in, double* out, int n) override {
    const size_t L = h.size();
    for (int i = 0; i < n; ++i) {
      hist[pos] = in[i];
      double acc = 0.0;
      size_t j = pos;
      for (size_t k = 0; k < L; ++k) {
        acc += h[k] * hist[j];
        j = j == 0 ? L - 1 : j - 1;
      }
      out[i] = acc;
      pos = (pos + 1) % L;
    }
  }
};

// Self-test wrapper: extra integer delay and/or polarity flip.
struct Wrapped {
  Processor& p;
  int delay;
  bool invert;
  std::vector<double> ring;
  size_t pos = 0;
  Wrapped(Processor& proc, int d, bool inv) : p(proc), delay(d), invert(inv), ring(static_cast<size_t>(d) + 1, 0.0) {}
  void operator()(const double* in, double* out, int n) {
    p.process(in, out, n);
    for (int i = 0; i < n; ++i) {
      ring[pos] = out[i];
      const size_t rd = (pos + ring.size() - static_cast<size_t>(delay)) % ring.size();
      out[i] = (invert ? -1.0 : 1.0) * ring[rd];
      pos = (pos + 1) % ring.size();
    }
  }
};

void csvHeader() {
  std::printf("file,load_ms,loudness_db,probe_ms");
  for (const char* m : {"td", "gp", "im"})
    std::printf(",%s_peak,%s_fine,%s_onset,%s_inv,%s_conf,%s_ratio,%s_lobe,%s_early,%s_early_inv", m, m, m, m, m, m, m, m, m);
  std::printf(",im_ms\n");
}

void csvResult(const namlat::Result& r) {
  std::printf(",%d,%.3f,%d,%d,%.4f,%.3f,%.3f,%d,%d", r.latencySamples, r.latencyFine, r.onsetSamples, r.inverted ? 1 : 0,
              r.confidence, r.peakRatio, r.lobeRatio, r.earlySamples, r.earlyInverted ? 1 : 0);
}

// CSV counterpart of runBoth: same measurements, one line.
void runCsv(Processor& proc, const namlat::Config& cfg, int delay, bool invert, double clickAmp,
            const std::string& path, double loadMs, double loudness) {
  Wrapped w(proc, delay, invert);
  proc.reset(cfg.sampleRate, cfg.blockSize);
  auto t0 = Clock::now();
  const namlat::Capture cap = namlat::runProbe(w, cfg);
  const double probeMs = msSince(t0);
  const namlat::Result td = namlat::analyzeTimeDomain(cap, cfg);
  const namlat::Result gp = namlat::analyzeGccPhat(cap, cfg);
  namlat::Config ccfg = cfg;
  ccfg.amplitude = clickAmp;
  proc.reset(cfg.sampleRate, cfg.blockSize);
  t0 = Clock::now();
  const namlat::Result im = namlat::measureImpulse(w, ccfg);
  const double imMs = msSince(t0);
  std::printf("%s,%.2f,%.2f,%.2f", path.c_str(), loadMs, loudness, probeMs);
  csvResult(td);
  csvResult(gp);
  csvResult(im);
  std::printf(",%.2f\n", imMs);
}

void printRow(const char* method, const namlat::Result& r, double analyzeMs) {
  std::printf("  %-12s peak %4d smp (%7.3f smp = %6.3f ms)  onset %4d smp  %-8s conf %.3f  peak-ratio %6.1f  lobe-ratio %5.2f%s  analyze %5.2f ms\n",
              method, r.latencySamples, r.latencyFine, r.latencyMs, r.onsetSamples,
              r.inverted ? "INVERTED" : "normal", r.confidence, r.peakRatio, r.lobeRatio,
              r.ambiguous ? " AMBIGUOUS" : "", analyzeMs);
  if (r.earlySamples != r.latencySamples)
    std::printf("  %-12s   early lobe %4d smp %s\n", "", r.earlySamples, r.earlyInverted ? "INVERTED" : "normal");
}

// Runs the sweep once and both analyzers on the capture, then the click
// on a fresh reset. Returns the
// GCC-PHAT result (used for the DI render).
namlat::Result runBoth(Processor& proc, const namlat::Config& cfg, int delay, bool invert, int taps, double clickAmp) {
  Wrapped w(proc, delay, invert);
  proc.reset(cfg.sampleRate, cfg.blockSize);

  auto t0 = Clock::now();
  const namlat::Capture cap = namlat::runProbe(w, cfg);
  const double probeMs = msSince(t0);

  t0 = Clock::now();
  const namlat::Result td = namlat::analyzeTimeDomain(cap, cfg);
  const double tdMs = msSince(t0);

  t0 = Clock::now();
  const namlat::Result gp = namlat::analyzeGccPhat(cap, cfg);
  const double gpMs = msSince(t0);

  std::printf("  probe: %s sweep, %zu samples + %d tail @ %.0f Hz, ran through the processor in %.1f ms\n",
              cfg.probe == namlat::Probe::LinearSweep ? "linear" : "exponential", cap.x.size(), cap.maxLag,
              cfg.sampleRate, probeMs);
  printRow("time-domain", td, tdMs);
  printRow("gcc-phat", gp, gpMs);

  // The click, on a freshly reset model. Its amplitude is independent of
  // the sweep's (clickAmp), everything else from cfg.
  namlat::Config ccfg = cfg;
  ccfg.amplitude = clickAmp;
  proc.reset(cfg.sampleRate, cfg.blockSize);
  t0 = Clock::now();
  const namlat::Result im = namlat::measureImpulse(w, ccfg);
  const double imMs = msSince(t0);
  std::printf("  click: %.3f (%.0f dBFS), %.0f ms pre-roll, ran + analyzed in %.2f ms\n", clickAmp,
              20.0 * std::log10(clickAmp), ccfg.impulsePreSeconds * 1000.0, imMs);
  printRow("impulse", im, 0.0);

  if (taps > 0) {
    std::printf("  time-domain correlation = est. impulse response, lags 0..%d (normalized):\n   ", taps - 1);
    for (int k = 0; k < taps && k < static_cast<int>(td.correlation.size()); ++k)
      std::printf(" %+.2f", td.correlation[static_cast<size_t>(k)]);
    std::printf("\n  click response, lags 0..%d (normalized):\n   ", taps - 1);
    for (int k = 0; k < taps && k < static_cast<int>(im.correlation.size()); ++k)
      std::printf(" %+.2f", im.correlation[static_cast<size_t>(k)]);
    std::printf("\n");
  }
  return gp;
}

// Stereo 24-bit WAV writer for the DI render.
bool writeWav24(const std::string& path, const std::vector<double>& l, const std::vector<double>& r, double sr) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  const uint32_t frames = static_cast<uint32_t>(std::min(l.size(), r.size()));
  const uint32_t dataLen = frames * 6;
  auto put16 = [&](uint16_t v) { f.put(static_cast<char>(v & 0xFF)); f.put(static_cast<char>(v >> 8)); };
  auto put32 = [&](uint32_t v) { put16(v & 0xFFFF); put16(v >> 16); };
  f.write("RIFF", 4); put32(36 + dataLen); f.write("WAVE", 4);
  f.write("fmt ", 4); put32(16); put16(1); put16(2); put32(static_cast<uint32_t>(sr));
  put32(static_cast<uint32_t>(sr) * 6); put16(6); put16(24);
  f.write("data", 4); put32(dataLen);
  auto put24 = [&](double v) {
    const int32_t s = static_cast<int32_t>(std::lround(std::clamp(v, -1.0, 1.0) * 8388607.0));
    f.put(static_cast<char>(s & 0xFF)); f.put(static_cast<char>((s >> 8) & 0xFF)); f.put(static_cast<char>((s >> 16) & 0xFF));
  };
  for (uint32_t i = 0; i < frames; ++i) { put24(l[i]); put24(r[i]); }
  return true;
}

// Render the DI through the processor; write L = output, R = DI delayed by
// the measured latency and polarity-corrected, both peak-normalized.
void renderDi(Processor& proc, const namlat::Config& cfg, int delay, bool invert,
              const std::vector<double>& di, const namlat::Result& r, const std::string& outPath) {
  Wrapped w(proc, delay, invert);
  proc.reset(cfg.sampleRate, cfg.blockSize);
  std::vector<double> out(di.size(), 0.0), in(static_cast<size_t>(cfg.blockSize)), tmp(static_cast<size_t>(cfg.blockSize));
  for (size_t pos = 0; pos < di.size(); pos += static_cast<size_t>(cfg.blockSize)) {
    const size_t take = std::min(static_cast<size_t>(cfg.blockSize), di.size() - pos);
    std::fill(in.begin(), in.end(), 0.0);
    std::copy(di.begin() + static_cast<long>(pos), di.begin() + static_cast<long>(pos + take), in.begin());
    w(in.data(), tmp.data(), cfg.blockSize);
    std::copy(tmp.begin(), tmp.begin() + static_cast<long>(take), out.begin() + static_cast<long>(pos));
  }
  std::vector<double> ref(di.size(), 0.0);
  const double sgn = r.inverted ? -1.0 : 1.0;
  for (size_t i = static_cast<size_t>(r.latencySamples); i < di.size(); ++i) ref[i] = sgn * di[i - static_cast<size_t>(r.latencySamples)];
  auto normalize = [](std::vector<double>& v) {
    double pk = 1e-9; for (double s : v) pk = std::max(pk, std::fabs(s));
    for (double& s : v) s *= 0.5 / pk;
  };
  normalize(out); normalize(ref);
  if (writeWav24(outPath, out, ref, cfg.sampleRate))
    std::printf("  DI render: %s (L = processor output, R = DI delayed %d smp%s)\n", outPath.c_str(),
                r.latencySamples, r.inverted ? ", inverted" : "");
  else
    std::printf("  couldn't write %s\n", outPath.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  namlat::Config cfg;
  int delay = 0;
  bool invert = false;
  int taps = 24;
  std::string diPath;
  double diSeconds = 0.0;
  double diGainDb = 0.0;
  double clickAmp = 0.1;
  bool csv = false;
  std::vector<std::string> files;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--delay") delay = std::stoi(next());
    else if (a == "--invert") invert = true;
    else if (a == "--di") diPath = next();
    else if (a == "--di-seconds") diSeconds = std::stod(next());
    else if (a == "--di-gain-db") diGainDb = std::stod(next());
    else if (a == "--taps") taps = std::stoi(next());
    else if (a == "--probe") cfg.probe = next() == "exp" ? namlat::Probe::ExponentialSweep : namlat::Probe::LinearSweep;
    else if (a == "--probe-ms") cfg.probeSeconds = std::stod(next()) * 0.001;
    else if (a == "--max-lag-ms") cfg.maxLagMs = std::stod(next());
    else if (a == "--amp") cfg.amplitude = std::stod(next());
    else if (a == "--click-amp") clickAmp = std::stod(next());
    else if (a == "--csv") csv = true;
    else if (a == "--block") cfg.blockSize = std::stoi(next());
    else files.push_back(a);
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: nam_latency_tool [--delay N] [--invert] [--di file.wav] files...\n");
    return 1;
  }

  std::vector<double> di;
  double diRate = 0.0;
  std::string diStem;
  if (!diPath.empty()) {
    if (!readWav(diPath, di, diRate)) { std::fprintf(stderr, "couldn't read DI %s\n", diPath.c_str()); return 1; }
    if (diSeconds > 0.0) di.resize(std::min(di.size(), static_cast<size_t>(diSeconds * diRate)));
    if (diGainDb != 0.0) {
      const double g = std::pow(10.0, diGainDb / 20.0);
      for (double& s : di) s *= g;
    }
    std::printf("DI: %s (%.0f Hz, %.2f s%s, %+.0f dB)\n", diPath.c_str(), diRate, di.size() / diRate,
                diSeconds > 0.0 ? " used" : "", diGainDb);
    // "Power - Guitar.wav" -> "power-guitar", for the render file name.
    diStem = diPath.substr(diPath.find_last_of("/\\") + 1);
    diStem = diStem.substr(0, diStem.find_last_of('.'));
    std::string clean;
    for (char ch : diStem) {
      if (std::isalnum(static_cast<unsigned char>(ch))) clean += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      else if (!clean.empty() && clean.back() != '-') clean += '-';
    }
    while (!clean.empty() && clean.back() == '-') clean.pop_back();
    diStem = clean;
  }
  if (delay || invert)
    std::printf(csv ? "# self-test wrapper: +%d samples delay, polarity %s\n" : "Self-test wrapper: +%d samples delay, polarity %s\n",
                delay, invert ? "flipped" : "unchanged");
  if (csv) csvHeader();

  for (const auto& path : files) {
    if (!csv) std::printf("\n%s\n", path.c_str());
    std::unique_ptr<Processor> proc;
    const bool isNam = path.size() > 4 && path.compare(path.size() - 4, 4, ".nam") == 0;
    auto t0 = Clock::now();
    double loadMs = 0.0, loudness = 0.0;
    if (isNam) {
      auto p = std::make_unique<NamProcessor>(path);
      loadMs = msSince(t0);
      if (!p->dsp) { std::printf(csv ? "%s,error\n" : "  failed to load\n", path.c_str()); continue; }
      if (p->dsp->HasLoudness()) loudness = p->dsp->GetLoudness();
      if (!csv) {
        std::printf("  loaded in %.1f ms", loadMs);
        if (p->dsp->HasLoudness()) std::printf(", loudness %.2f dB", loudness);
        std::printf("\n");
      }
      proc = std::move(p);
    } else {
      auto p = std::make_unique<IrProcessor>(path);
      if (!p->ok) { std::printf(csv ? "%s,error\n" : "  failed to read wav\n", path.c_str()); continue; }
      if (!csv) std::printf("  IR: %zu taps used @ %.0f Hz\n", p->h.size(), p->sampleRate);
      proc = std::move(p);
    }

    namlat::Config c = cfg;
    c.sampleRate = proc->sampleRate;
    if (csv) {
      runCsv(*proc, c, delay, invert, clickAmp, path, loadMs, loudness);
      std::fflush(stdout);
      continue;
    }
    const namlat::Result gp = runBoth(*proc, c, delay, invert, taps, clickAmp);

    if (!di.empty() && gp.ok) {
      if (std::fabs(diRate - c.sampleRate) > 0.5) {
        std::printf("  (DI rate %.0f != processor rate %.0f, skipping DI render)\n", diRate, c.sampleRate);
      } else {
        std::string stem = path.substr(path.find_last_of("/\\") + 1);
        stem = stem.substr(0, stem.find_last_of('.'));
        renderDi(*proc, c, delay, invert, di, gp, stem + "-" + diStem + "-aligned.wav");
      }
    }
  }
  return 0;
}
