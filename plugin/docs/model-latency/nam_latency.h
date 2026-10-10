// nam_latency.h
//
// Measure the latency and polarity of a black-box audio processor (a NAM
// model, an IR, a whole block) by running a short, known probe through it
// and cross-correlating what comes out against what went in.
//
// This is a stripped-down, dependency-free version of what the TONE3000
// plugin does when it auto-aligns two parallel chains. There, both chains
// eat the same sweep and we correlate chain A against chain B (relative
// lag). Here the "reference" is simply the dry probe itself, so the result
// is the absolute latency of one processor. Same math, one fewer unknown.
//
// Two analyzers over the same sweep capture, pick whichever fits your platform:
//
//   measureTimeDomain()  Plain cross-correlation over the lag window. No
//                        FFT, no allocation beyond the capture, ~N * maxLag
//                        multiply-adds (2.3 M at the defaults, well under
//                        the cost of running the model over the probe).
//                        With the default white (linear) sweep the
//                        correlation is the processor's small-signal
//                        impulse response, so the peak is unambiguous.
//
//   measureGccPhat()     Generalized cross-correlation with PHAT weighting
//                        (Knapp & Carter 1976), the method the plugin uses.
//                        Whitening the cross-spectrum removes the model's
//                        voicing and the probe's spectral tilt, so it also
//                        works with the gentler exponential sweep and gives
//                        a slightly sharper, bias-free sub-sample estimate.
//                        Needs a power-of-two complex FFT (a small one is
//                        included below; swap in your own).
//
// On the test models both agree to within a sample. If you only want one,
// take the time-domain analyzer with the linear sweep.
//
// A third, measureImpulse(), feeds a single click instead of a sweep and
// reads the response directly. Cheapest by far, exact on linear things
// (IRs), unreliable on nonlinear amp models; it is here for the comparison
// and documented as such in ../model-latency.md.
//
// Only <cmath>, <vector>, <complex>, <algorithm>. Sample type is `double`;
// search/replace to `float` if that's what your engine speaks (keep the
// accumulators double).
//
// Usage:
//
//   namlat::Config cfg;                 // 48k, 100 ms sweep, 10 ms window
//   auto result = namlat::measureTimeDomain(
//       [&](const double* in, double* out, int n) { model.process(in, out, n); },
//       cfg);
//   // result.latencySamples, result.latencyMs, result.inverted, result.confidence
//
// The processor callback is called in cfg.blockSize chunks, in order, with
// a mono input block and must fill the mono output block. It is stateful:
// warm it up first (nam::DSP::Reset() prewarms; or set cfg.warmupSeconds).

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace namlat {

enum class Probe {
  // Linear chirp: white spectrum, so its autocorrelation is ~a delta and
  // the plain cross-correlation with the output *is* the processor's
  // small-signal impulse response. Works with both analyzers. Default.
  LinearSweep,
  // Exponential (log) sweep, what the plugin uses: pink spectrum, gentler on
  // the model, but its broad low-frequency-heavy autocorrelation smears a
  // plain cross-correlation. Only meaningful paired with GCC-PHAT, whose
  // whitening undoes the spectral tilt.
  ExponentialSweep,
};

struct Config {
  double sampleRate = 48000.0;
  Probe probe = Probe::LinearSweep;
  // Sweep length. 100 ms is plenty for a ±10 ms window; the plugin uses
  // 280 ms for its ±24 ms window.
  double probeSeconds = 0.10;
  // Lag search window [0, maxLagMs]. A causal model can't produce negative
  // lag; misaligned training data only ever bakes in *positive* delay.
  double maxLagMs = 10.0;
  // Probe peak amplitude. -20 dBFS keeps most models in their quasi-linear
  // region (less harmonic smear on the correlation) while staying well
  // above any noise floor the model learned. The answers are insensitive to
  // this (see the research doc), so there is no need to calibrate it per
  // model from `input_level_dbu`.
  double amplitude = 0.1;
  double lowHz = 40.0;
  // Up to 20 kHz (clamped to 0.45 * sampleRate). The sweep's bandwidth sets
  // the correlation's time resolution: at 16 kHz the identity's own
  // correlation reads 0.42 at lag 1 and has -20 dB sidelobes; at 20 kHz,
  // 0.20 and -13 dB, below the onset threshold.
  double highHz = 20000.0;
  // Raised-cosine fades on the sweep edges: no spectral splatter.
  double edgeFadeSeconds = 0.005;
  // Chunk size the processor callback is fed with.
  int blockSize = 256;
  // Silence run through the processor before the probe, for engines that
  // don't prewarm themselves (NAM core's Reset() does, so 0 for it).
  double warmupSeconds = 0.0;
  // GCC-PHAT only: whitening exponent, 1 = full PHAT, 0 = plain xcorr.
  // 0.8 keeps near-empty bins from being amplified into noise.
  double phatRho = 0.8;
  // Onset: first lag whose |correlation| reaches this fraction of the peak.
  // 0.3 (-10 dB) sits above the probe's own correlation sidelobes (see
  // highHz), so for a pure delay onset == peak; anything earlier is the
  // model's response rising before its main lobe.
  double onsetFraction = 0.3;
  // Lobes within this ratio of the biggest count as ties. Sets both
  // Result::ambiguous (lobeRatio below it) and Result::earlySamples (the
  // earliest tied lobe). 1.1 is ~0.8 dB; see the research doc for why.
  double lobeTolerance = 1.1;
  // measureImpulse only: silence fed before the click, to read the model's
  // idle output.
  double impulsePreSeconds = 0.01;
};

struct Result {
  bool ok = false;
  int latencySamples = 0;      // integer lag at the correlation peak
  double latencyFine = 0.0;    // sub-sample refined lag (samples)
  double latencyMs = 0.0;      // latencyFine in ms
  // First lag where energy arrives (see Config::onsetFraction). Equal to
  // latencySamples for a pure delay; earlier for a smeared (cab-like)
  // response. The peak is what you align parallel paths on; the onset tells
  // you whether the gap is baked-in delay or just the response's rise time.
  int onsetSamples = 0;
  bool inverted = false;       // output polarity is flipped vs. input
  // Normalized cross-correlation magnitude at the peak, 0..1. 1 = output is
  // the input up to gain/shift/polarity. Nonlinear amp models legitimately
  // read well below 1; it's a diagnostic, not a gate.
  double confidence = 0.0;
  // |peak| over the largest |peak| more than 1 ms away: how unambiguous the
  // lag is. Gate on this (the plugin rejects < 2).
  double peakRatio = 0.0;
  // |peak| over the largest lobe 2 samples to 1 ms away, the competitors
  // peakRatio deliberately ignores. A cab response has lobes of alternating
  // sign a few samples apart; when two are within ~1 dB of each other
  // (lobeRatio < 1.1) which one wins depends on probe level and analyzer,
  // and the two answers differ by a few samples *and* in polarity.
  double lobeRatio = 0.0;
  // lobeRatio < Config::lobeTolerance: the peak lag and polarity are a
  // coin flip between two lobes. Store the onset, not the peak.
  bool ambiguous = false;
  // The earliest lobe within lobeTolerance of the biggest (== latencySamples
  // when the peak is unambiguous). When two lobes are near-equal, "which is
  // 0.1 dB bigger" flips with probe level and analyzer; "the earliest of
  // the tied ones" does not. earlyInverted is that lobe's polarity.
  int earlySamples = 0;
  bool earlyInverted = false;
  // The correlation function over [0, maxLag], normalized to the peak. For
  // the time-domain analyzer with a linear sweep this is the estimated
  // impulse response; handy for logging. Drop it on a memory-tight target.
  std::vector<double> correlation;
};

// ---------------------------------------------------------------------------
// Probe + capture (shared by both analyzers)
// ---------------------------------------------------------------------------

struct Capture {
  std::vector<double> x;  // the dry sweep, N samples
  std::vector<double> y;  // processor output, N + maxLag samples, DC removed
  int maxLag = 0;
};

// Sine sweep from lowHz to highHz over probeSeconds, raised-cosine edges.
//   linear:      phase(t) = 2π (f1 t + (f2 - f1) t² / 2T)
//   exponential: phase(t) = K (exp(t/T ln(f2/f1)) - 1),  K = 2π f1 T / ln(f2/f1)   (Farina 2000)
inline std::vector<double> makeSweep(const Config& cfg) {
  const double sr = cfg.sampleRate;
  const int n = static_cast<int>(std::lround(cfg.probeSeconds * sr));
  const double f1 = cfg.lowHz;
  const double f2 = std::min(cfg.highHz, 0.45 * sr);
  const double T = n / sr;
  const double logRatio = std::log(f2 / f1);
  const double K = 2.0 * M_PI * f1 * T / logRatio;
  const int fade = std::min(n / 4, static_cast<int>(std::lround(cfg.edgeFadeSeconds * sr)));

  std::vector<double> x(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double t = i / sr;
    const double phase = cfg.probe == Probe::LinearSweep
                             ? 2.0 * M_PI * (f1 * t + 0.5 * (f2 - f1) / T * t * t)
                             : K * (std::exp(t / T * logRatio) - 1.0);
    double s = cfg.amplitude * std::sin(phase);
    if (i < fade)
      s *= 0.5 - 0.5 * std::cos(M_PI * i / fade);
    else if (i >= n - fade)
      s *= 0.5 - 0.5 * std::cos(M_PI * (n - 1 - i) / fade);
    x[static_cast<size_t>(i)] = s;
  }
  return x;
}

// Runs warm-up silence, then `signal` followed by maxLag samples of
// silence, through `process` in blockSize chunks. Captures the output.
// `signal` is normally the sweep (runProbe), but any mono clip works as the
// reference: feed a real guitar DI to check the sweep's verdict against
// actual playing.
template <class ProcessFn>
inline Capture runSignal(ProcessFn&& process, const Config& cfg, std::vector<double> signal) {
  Capture cap;
  cap.x = std::move(signal);
  cap.maxLag = static_cast<int>(std::ceil(cfg.maxLagMs * 0.001 * cfg.sampleRate));

  const int n = static_cast<int>(cap.x.size());
  const int total = n + cap.maxLag;
  const int block = std::max(1, cfg.blockSize);

  std::vector<double> in(static_cast<size_t>(block), 0.0);
  std::vector<double> out(static_cast<size_t>(block), 0.0);

  const int warmup = static_cast<int>(std::lround(cfg.warmupSeconds * cfg.sampleRate));
  for (int done = 0; done < warmup; done += block)
    process(in.data(), out.data(), block);

  cap.y.assign(static_cast<size_t>(total), 0.0);
  for (int pos = 0; pos < total; pos += block) {
    const int take = std::min(block, total - pos);
    for (int i = 0; i < take; ++i) {
      const int src = pos + i;
      in[static_cast<size_t>(i)] = src < n ? cap.x[static_cast<size_t>(src)] : 0.0;
    }
    // Always feed a full block (engines are prepared for blockSize); the
    // extra samples past `total` are just discarded.
    for (int i = take; i < block; ++i)
      in[static_cast<size_t>(i)] = 0.0;
    process(in.data(), out.data(), block);
    for (int i = 0; i < take; ++i)
      cap.y[static_cast<size_t>(pos + i)] = out[static_cast<size_t>(i)];
  }

  // Remove DC: some models sit on a small offset, which would bias the
  // correlation toward lag 0.
  double mean = 0.0;
  for (double v : cap.y) mean += v;
  mean /= std::max<size_t>(1, cap.y.size());
  for (double& v : cap.y) v -= mean;
  return cap;
}

template <class ProcessFn>
inline Capture runProbe(ProcessFn&& process, const Config& cfg) {
  return runSignal(process, cfg, makeSweep(cfg));
}

// Onset: first lag whose |c| reaches onsetFraction of c's own peak.
inline int onsetOf(const std::vector<double>& c, const Config& cfg) {
  double peak = 0.0;
  for (double v : c) peak = std::max(peak, std::fabs(v));
  for (size_t k = 0; k < c.size(); ++k)
    if (std::fabs(c[k]) >= cfg.onsetFraction * peak) return static_cast<int>(k);
  return 0;
}

// Shared post-processing: given the correlation function c[lag] for
// lag in [0, maxLag], find the peak on magnitude, its sign, sharpness,
// and a parabolic sub-sample refinement.
inline Result finishFromCorrelation(const std::vector<double>& c, const Capture& cap,
                                    const Config& cfg) {
  Result r;
  const int maxLag = cap.maxLag;
  if (static_cast<int>(c.size()) < maxLag + 1 || cap.x.empty())
    return r;

  int best = 0;
  double bestAbs = std::fabs(c[0]);
  for (int k = 1; k <= maxLag; ++k) {
    const double a = std::fabs(c[static_cast<size_t>(k)]);
    if (a > bestAbs) { bestAbs = a; best = k; }
  }
  r.inverted = c[static_cast<size_t>(best)] < 0.0;

  r.onsetSamples = onsetOf(c, cfg);

  // Sharpness: winner vs. the best competitor more than 1 ms away (or a
  // quarter of the window, when the window is tighter than that).
  const int guard = std::min(static_cast<int>(std::lround(0.001 * cfg.sampleRate)), std::max(1, maxLag / 4));
  double second = 0.0, lobe = 0.0;
  for (int k = 0; k <= maxLag; ++k) {
    const int dist = std::abs(k - best);
    if (dist > guard) second = std::max(second, std::fabs(c[static_cast<size_t>(k)]));
    else if (dist >= 2) lobe = std::max(lobe, std::fabs(c[static_cast<size_t>(k)]));
  }
  r.peakRatio = bestAbs / std::max(second, 1e-12);
  r.lobeRatio = bestAbs / std::max(lobe, 1e-12);
  r.ambiguous = r.lobeRatio < cfg.lobeTolerance;

  // Earliest local maximum of |c| within lobeTolerance of the peak.
  r.earlySamples = best;
  r.earlyInverted = r.inverted;
  for (int k = 0; k < best; ++k) {
    const double a = std::fabs(c[static_cast<size_t>(k)]);
    const bool localMax = (k == 0 || a >= std::fabs(c[static_cast<size_t>(k - 1)])) &&
                          a >= std::fabs(c[static_cast<size_t>(k + 1)]);
    if (localMax && a * cfg.lobeTolerance >= bestAbs) {
      r.earlySamples = k;
      r.earlyInverted = c[static_cast<size_t>(k)] < 0.0;
      break;
    }
  }

  r.correlation.resize(c.size());
  for (size_t k = 0; k < c.size(); ++k) r.correlation[k] = c[k] / std::max(bestAbs, 1e-12);

  // Parabolic interpolation on the three samples around the peak. Carries
  // a small position-dependent bias (the plugin's band-limited version in
  // measureGccPhat avoids it); fine for "which sample" purposes.
  double fine = best;
  if (best > 0 && best < maxLag) {
    const double sgn = r.inverted ? -1.0 : 1.0;
    const double ym = sgn * c[static_cast<size_t>(best - 1)];
    const double y0 = sgn * c[static_cast<size_t>(best)];
    const double yp = sgn * c[static_cast<size_t>(best + 1)];
    const double denom = ym - 2.0 * y0 + yp;
    if (std::fabs(denom) > 1e-20)
      fine += std::clamp(0.5 * (ym - yp) / denom, -0.5, 0.5);
  }

  // Confidence: normalized correlation at the integer peak, on the raw
  // capture (independent of any whitening the analyzer applied).
  const int n = static_cast<int>(cap.x.size());
  double dot = 0.0, ex = 0.0, ey = 0.0;
  for (int i = 0; i < n; ++i) {
    const double xi = cap.x[static_cast<size_t>(i)];
    const double yi = cap.y[static_cast<size_t>(i + best)];
    dot += xi * yi; ex += xi * xi; ey += yi * yi;
  }
  r.confidence = std::fabs(dot) / std::sqrt(std::max(ex * ey, 1e-24));

  r.latencySamples = best;
  r.latencyFine = fine;
  r.latencyMs = fine * 1000.0 / cfg.sampleRate;
  r.ok = true;
  return r;
}

// ---------------------------------------------------------------------------
// Analyzer 1: time-domain cross-correlation (no FFT)
// ---------------------------------------------------------------------------
//
// c[k] = sum_n x[n] * y[n + k]   for k in [0, maxLag]
//
// Because the sweep's autocorrelation is close to a delta, c[k] is very
// nearly the processor's impulse response (for the linear part of it).
// The peak of |c| is where most of the energy arrives. For an amp model
// that's the onset; for a model with a cab baked in it can sit a sample or
// two past the first arrival (the cab's own peak), which is also what you
// want to line up when the goal is "sum in phase with a parallel path".
inline Result analyzeTimeDomain(const Capture& cap, const Config& cfg) {
  const int n = static_cast<int>(cap.x.size());
  std::vector<double> c(static_cast<size_t>(cap.maxLag + 1), 0.0);
  for (int k = 0; k <= cap.maxLag; ++k) {
    double acc = 0.0;
    const double* x = cap.x.data();
    const double* y = cap.y.data() + k;
    for (int i = 0; i < n; ++i) acc += x[i] * y[i];
    c[static_cast<size_t>(k)] = acc;
  }
  return finishFromCorrelation(c, cap, cfg);
}

template <class ProcessFn>
inline Result measureTimeDomain(ProcessFn&& process, const Config& cfg = Config()) {
  return analyzeTimeDomain(runProbe(process, cfg), cfg);
}

// ---------------------------------------------------------------------------
// Analyzer 2: GCC-PHAT via FFT (what the plugin does)
// ---------------------------------------------------------------------------

// Minimal in-place iterative radix-2 complex FFT. Replace with your
// platform's FFT; only forward/inverse of a power-of-two complex buffer is
// needed.
inline void fft(std::vector<std::complex<double>>& a, bool inverse) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = 2.0 * M_PI / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
    const std::complex<double> wlen(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1.0, 0.0);
      for (size_t j = 0; j < len / 2; ++j) {
        const std::complex<double> u = a[i + j];
        const std::complex<double> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
  if (inverse)
    for (auto& v : a) v /= static_cast<double>(n);
}

inline Result analyzeGccPhat(const Capture& cap, const Config& cfg) {
  const int n = static_cast<int>(cap.x.size());
  const int m = static_cast<int>(cap.y.size());
  // Zero-pad past m + maxLag so circular wrap-around stays out of the
  // searched window.
  size_t fftSize = 1;
  while (fftSize < static_cast<size_t>(m + cap.maxLag)) fftSize <<= 1;

  std::vector<std::complex<double>> X(fftSize), Y(fftSize);
  for (int i = 0; i < n; ++i) X[static_cast<size_t>(i)] = cap.x[static_cast<size_t>(i)];
  for (int i = 0; i < m; ++i) Y[static_cast<size_t>(i)] = cap.y[static_cast<size_t>(i)];
  fft(X, false);
  fft(Y, false);

  // Cross-spectrum Y * conj(X), whitened by |.|^rho. IFFT of this is
  // c[k] = sum_n y[n + k] x[n]: peak at k = lag of the output. Negative
  // lags would live at fftSize - k; we only search 0..maxLag.
  std::vector<std::complex<double>> cross(fftSize), plain(fftSize);
  for (size_t b = 0; b < fftSize; ++b) {
    const std::complex<double> v = Y[b] * std::conj(X[b]);
    const double mag = std::abs(v);
    plain[b] = v;
    cross[b] = v / std::max(std::pow(mag, cfg.phatRho), 1e-12);
  }
  std::vector<std::complex<double>> corr(cross);
  fft(corr, true);

  std::vector<double> c(static_cast<size_t>(cap.maxLag + 1));
  for (int k = 0; k <= cap.maxLag; ++k) c[static_cast<size_t>(k)] = corr[static_cast<size_t>(k)].real();

  Result r = finishFromCorrelation(c, cap, cfg);
  if (!r.ok) return r;

  // Onset from the *un-whitened* correlation (one more inverse FFT; it is
  // the time-domain analyzer's c[k]). Whitening pre-rings, which put the
  // whitened onset a sample or two early on ~10% of catalog models.
  fft(plain, true);
  std::vector<double> cp(static_cast<size_t>(cap.maxLag + 1));
  for (int k = 0; k <= cap.maxLag; ++k) cp[static_cast<size_t>(k)] = plain[static_cast<size_t>(k)].real();
  r.onsetSamples = onsetOf(cp, cfg);

  // Band-limited sub-sample refinement (replaces the parabolic estimate
  // from finishFromCorrelation): evaluate the whitened cross-spectrum's
  // inverse DFT at fractional lags around the integer peak. Exact
  // interpolation of the correlation, no position-dependent bias.
  const int best = r.latencySamples;
  const double sgn = r.inverted ? -1.0 : 1.0;
  const int half = static_cast<int>(fftSize / 2);
  auto corrAt = [&](double tau) {
    double acc = 0.0;
    const double w0 = 2.0 * M_PI * tau / static_cast<double>(fftSize);
    for (size_t b = 0; b < fftSize; ++b) {
      const int sb = static_cast<int>(b) <= half ? static_cast<int>(b) : static_cast<int>(b) - static_cast<int>(fftSize);
      acc += cross[b].real() * std::cos(w0 * sb) - cross[b].imag() * std::sin(w0 * sb);
    }
    return sgn * acc;
  };
  const int steps = 8;
  const double step = 1.0 / steps;
  double fineTau = best, fineVal = corrAt(fineTau);
  for (int s = -steps; s <= steps; ++s) {
    const double tau = best + s * step;
    if (tau < 0.0 || tau > cap.maxLag) continue;
    const double v = corrAt(tau);
    if (v > fineVal) { fineVal = v; fineTau = tau; }
  }
  {
    const double ym = corrAt(fineTau - step), yp = corrAt(fineTau + step);
    const double denom = ym - 2.0 * fineVal + yp;
    if (std::fabs(denom) > 1e-20)
      fineTau += std::clamp(0.5 * (ym - yp) / denom, -0.5, 0.5) * step;
  }
  r.latencyFine = fineTau;
  r.latencyMs = fineTau * 1000.0 / cfg.sampleRate;
  return r;
}

template <class ProcessFn>
inline Result measureGccPhat(ProcessFn&& process, const Config& cfg = Config()) {
  return analyzeGccPhat(runProbe(process, cfg), cfg);
}

// ---------------------------------------------------------------------------
// Analyzer 3: a single click (no sweep, no correlation)
// ---------------------------------------------------------------------------
//
// Feed impulsePreSeconds of silence, one sample at cfg.amplitude, then
// maxLag more samples of silence. The output after the click, minus the
// model's idle output, *is* its impulse response; peak/onset/polarity read
// straight off it. Inference covers ~20 ms of audio instead of 110 ms and
// the analysis is a few hundred compares, so this is the cheapest possible
// measurement. What it gives up is the sweep's processing gain: the sweep
// spreads the probe energy over 4800 samples at a sane level, a click puts
// it all in one sample, so to drive the model the same distance it would
// need to be ~37 dB hotter. Whether that matters depends on the model (see
// the research doc); the sweep methods' time-domain correlation is the same
// impulse response with that gain, so compare the two when in doubt.
template <class ProcessFn>
inline Result measureImpulse(ProcessFn&& process, const Config& cfg = Config()) {
  const int pre = std::max(1, static_cast<int>(std::lround(cfg.impulsePreSeconds * cfg.sampleRate)));
  std::vector<double> click(static_cast<size_t>(pre) + 1, 0.0);
  click.back() = cfg.amplitude;
  const Capture cap = runSignal(process, cfg, std::move(click));
  const int maxLag = cap.maxLag;

  // Idle output: mean of the second half of the pre-roll (the first half
  // lets any Reset() transient settle). Subtracting it cancels whatever DC
  // offset the model sits on; runSignal's global DC removal is then moot.
  double idle = 0.0;
  for (int i = pre / 2; i < pre; ++i) idle += cap.y[static_cast<size_t>(i)];
  idle /= std::max(1, pre - pre / 2);

  std::vector<double> h(static_cast<size_t>(maxLag) + 1);
  for (int k = 0; k <= maxLag; ++k) h[static_cast<size_t>(k)] = cap.y[static_cast<size_t>(pre + k)] - idle;

  // Reuse the shared peak/onset/polarity/sharpness/refinement on h as if
  // it were a correlation function. The Capture handed in is the response
  // itself, so `confidence` degenerates to 1 and is zeroed: with no
  // reference to correlate against there is nothing to be confident about.
  // peakRatio keeps its meaning (peak vs. the best lobe > 1 ms away).
  Capture hc;
  hc.x.assign(1, cfg.amplitude);
  hc.y = h;
  hc.maxLag = maxLag;
  Result r = finishFromCorrelation(h, hc, cfg);
  r.confidence = 0.0;
  return r;
}

}  // namespace namlat
