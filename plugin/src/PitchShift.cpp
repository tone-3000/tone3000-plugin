#include "PitchShift.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Tuning, all in ms so every rate behaves the same. The values are the ones
// the bench in plugin/docs/pitch-shift.md settled on; none is exposed.
namespace {
// A drift splice crossfades over kFadeMs when its lag matches well and
// stretches toward kFadeMaxMs as the match worsens (ncc from kFadeNccHi
// down to kFadeNccLo). On a dissonant chord no lag lines every partial up;
// a short fade turns each mismatched partial's phase step into a click,
// a long one spreads it below the chord. kFadeMinMs is the shortest fade
// the geometry plans for: a splice landing at the far end of its range
// has only that much buffer left to fade in.
constexpr double kFadeMs = 30.0;
constexpr double kFadeMaxMs = 120.0;
constexpr double kFadeMinMs = 6.0;
constexpr double kFadeNccHi = 0.95;
constexpr double kFadeNccLo = 0.6;
constexpr double kOnsetFadeMs = 2.0;     // crossfade of an onset re-sync
constexpr double kMaxCorrMs = 25.0;      // correlation window (capped at the buffer)
constexpr double kOnsetSpanMs = 4.0;     // an onset re-sync lands within this of the floor
constexpr double kRefractoryMs = 40.0;   // one re-sync per attack
constexpr double kDetectorHpfHz = 600.0; // attacks are HF-rich, low fundamentals never ripple it
constexpr double kDetectorSmoothMs = 2.0;
constexpr int kHistoryCells = 50;        // 1 ms energy cells kept for the detector
constexpr int kHistorySkipCells = 5;     // the attack itself is not its own reference
constexpr double kOnsetOverMin = 7.94;   // 9 dB over the recent floor ...
constexpr double kOnsetOverMax = 3.98;   // ... and 6 dB over the recent ceiling (a new peak;
                                         // a beating dyad swings its HF energy by less)
// Lag search granularity: 4 samples at 48 kHz, refined to the sample around
// the coarse best. Free in quality on the bench, 3x cheaper than exhaustive.
constexpr double kCoarseStepRate = 12000.0;
// A drift splice is predictable (the tap approaches the buffer end at a
// known rate), so its lag search is spread over the samples of this lead
// instead of running as one burst: at a 16-sample host buffer the burst
// alone (400-800 us at 48 kHz) blew the callback budget and clicked.
constexpr double kSearchLeadMs = 4.0;
// An upshift tap gains on the write head, so every fade and every search
// lead spends buffer at the drift rate (three samples a sample at +24).
// Whatever they leave is the range of jumps a splice may choose from, and
// a splice is only clean at a whole number of periods: the range has to be
// at least a period long for every note to have one. Half the buffer is
// kept for it; where the fades and the lead don't fit beside that (above
// +8 on the 20 ms buffer, +11 on 30 ms) they shrink to make room. Smaller
// shares keep longer fades but leave notes whose period the range misses
// off-pitch (plugin/docs/pitch-shift.md, Two octaves).
constexpr double kUpshiftLandShare = 0.5;
// A pitch change smaller than this (in ratio; ~1.7 st around unity) keeps a
// pending search: the landing moves by at most the change times the lead,
// ~20 samples, which the landing range's margins cover.
constexpr double kSearchKeepRatio = 0.1;
// Power and tonality blends, the same length as the image decks'
// (kDeckFadeSeconds): every transition passes through a mix, never a jump.
constexpr double kBlendSeconds = 0.025;
}  // namespace

struct PitchShift::Impl {
  // Power-of-two ring indexed by absolute sample number; sample i lives at
  // buf[i & mask]. Reads are 4-point Hermite so a fractional tap is smooth.
  struct Ring {
    std::vector<float> buf;
    uint32_t mask = 0;
    void init(int minSize) {
      int size = 1;
      while (size < minSize) size <<= 1;
      buf.assign(static_cast<size_t>(size), 0.0f);
      mask = static_cast<uint32_t>(size - 1);
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    void write(int64_t i, float x) { buf[static_cast<uint32_t>(i) & mask] = x; }
    float at(int64_t i) const { return buf[static_cast<uint32_t>(i) & mask]; }
    float read(double pos) const {
      const auto i = static_cast<int64_t>(std::floor(pos));
      const float t = static_cast<float>(pos - static_cast<double>(i));
      const float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
      const float c = 0.5f * (x1 - xm1);
      const float v = x0 - x1;
      const float w = c + v;
      const float a = w + v + 0.5f * (x2 - x0);
      const float b = w + a;
      return ((a * t - b) * t + c) * t + x0;
    }
  };

  // The control ring is mirrored (every sample written twice, `size`
  // apart) so any window ending at sample i is contiguous memory: the
  // correlation is then a plain dot product the compiler vectorises.
  struct MirroredRing {
    std::vector<float> buf;
    uint32_t mask = 0;
    int size = 0;
    void init(int minSize) {
      size = 1;
      while (size < minSize) size <<= 1;
      buf.assign(static_cast<size_t>(size) * 2, 0.0f);
      mask = static_cast<uint32_t>(size - 1);
    }
    void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
    void write(int64_t i, float x) {
      const auto k = static_cast<uint32_t>(i) & mask;
      buf[k] = x;
      buf[k + static_cast<uint32_t>(size)] = x;
    }
    // The n samples before sample i (excluding it), oldest first.
    const float* windowEndingAt(int64_t i, int n) const {
      const auto k = static_cast<uint32_t>(i) & mask;
      return buf.data() + (static_cast<int>(k) + size - n);
    }
  };

  double sampleRate = 0.0;
  Params params;
  double ratio = 1.0;

  // Power: the shifted signal blends against the dry; the engine keeps
  // running until a fade-out lands so the processor can stop calling it.
  // After a reset the tap reads a cleared ring for one floor delay, so the
  // blend-in waits for `primeLeft` samples (rather than fading up onto the
  // edge where the audio arrives).
  bool enabled = false, running = false;
  juce::LinearSmoothedValue<float> wetMix;
  // Dry/wet balance (the deck's Mix): the shifted signal's share of the
  // output, 0 = dry, 1 = wet (the default). Blended inside the power fade,
  // which anchors on the bypassed input, so a power-off still lands on the
  // untouched input whatever Mix is set to.
  juce::LinearSmoothedValue<float> mixSmoother;
  int primeLeft = 0;

  // Rings: one per channel for the audio, one for the control signal (the
  // channel mean) the detector and the lag search run on, and one per
  // channel delaying the dry for the tonality bypass band.
  std::array<Ring, kMaxChannels> rings;
  MirroredRing control;
  std::array<Ring, kMaxChannels> dryRings;
  int64_t written = 0;  // samples written so far; the newest is written - 1

  // Geometry at the current rate / window, in samples. fadeLo..fadeHi is
  // the drift fade's range and searchLeadNow the drift search's lead here
  // (the constants above, cut down by the buffer and the ratio, see
  // updateFadeRange).
  int dMin = 0, dMax = 0, corrLen = 0;
  int fadeLen = 0, fadeMaxLen = 0, fadeMinLen = 0, fadeLo = 0, fadeHi = 0, onsetFadeLen = 0;
  int onsetSpan = 0, refractory = 0, cellLen = 1, coarseStep = 1, searchLead = 1, searchLeadNow = 1;

  // Read taps (absolute positions) and the crossfade between them. fadeNcc
  // is how well the two correlate, which sets the fade's gain law.
  double rA = 0.0, rB = 0.0;
  bool fading = false;
  double fade = 0.0, fadeInc = 0.0, fadeNcc = 1.0;

  // Lag search, incremental. The reference is the tap's recent waveform,
  // copied when the search starts; candidates are buffer positions scored
  // as damage per splice (1 - ncc) over the seconds the jump buys, so a
  // long jump with a good match beats a short perfect one (max-ncc alone
  // picks one-period hops and splices constantly on low dyads). The result
  // is a tap jump J (new position = tap + J); a jump found a few ms early
  // stays valid because both segments move together on a sustained note.
  struct Search {
    bool active = false, ready = false;
    int lo = 0, hi = 0, next = 0, perSample = 1;
    double bestScore = -1e9, bestNcc = 0.0;
    int bestDelay = 0;
    int64_t now0 = 0, a0 = 0;
    int64_t resultJump = 0;
  } search;
  std::vector<float> ref;
  double refXX = 0.0;

  // Onset detector: first-order HPF, 2 ms energy smoother, 1 ms min/max
  // history cells.
  double hpA = 0.0, hpZ1 = 0.0, hpZ2 = 0.0;
  double smoothA = 0.0, eHf = 0.0;
  std::array<float, kHistoryCells> minHist{}, maxHist{};
  double minAcc = 1e9, maxAcc = 0.0;
  int64_t lastOnset = 0;

  // Tonality: LR4 crossover after the shifter, the low band from the
  // shifted signal and the high band from the dry (delayed by the floor so
  // it lands with the re-synced attacks), blended against the plain shift.
  // The filters run whether or not the limit is engaged (four biquads a
  // channel), so engaging it never starts them cold.
  juce::dsp::LinkwitzRileyFilter<float> wetLowpass, dryHighpass;
  juce::LinearSmoothedValue<float> tonalityMix;

  void applyWindow() {
    dMin = minDelaySamples(sampleRate);
    dMax = windowSamples(params.window, sampleRate);
    corrLen = std::min(dMax, static_cast<int>(sampleRate * kMaxCorrMs * 0.001));
    updateFadeRange();
  }

  // The longest drift fade and the search lead this ratio and window
  // allow. An upshift tap gains on the write head, so one cycle spends
  // buffer on the fade that lands the tap, the next search's lead and the
  // fade after it, all at the drift rate (`landLo` in process()). The
  // longest fade is held to a sixth of the range, and where the cycle
  // would leave less than kUpshiftLandShare of the buffer to land in, the
  // fade and the lead scale down together until it does. A downshift tap
  // only runs deeper, which costs ring memory, not range, so it keeps the
  // full span and the full lead.
  void updateFadeRange() {
    const double drift = std::abs(1.0 - ratio);
    double hi = fadeMaxLen;
    double lead = searchLead;
    int floor = fadeMinLen;
    if (ratio > 1.0) {
      hi = std::max<double>(fadeMinLen, std::min(hi, static_cast<double>(dMax - dMin) / (6.0 * drift)));
      const double cost = drift * (2.0 * hi + lead + 4.0) + 6.0;
      const double budget = dMax * (1.0 - kUpshiftLandShare);
      if (cost > budget) {
        const double scale = budget / cost;
        hi *= scale;
        lead *= scale;
        floor = 8;
      }
    }
    fadeHi = std::max(floor, static_cast<int>(hi));
    fadeLo = std::min(fadeLen, fadeHi);
    searchLeadNow = std::max(1, static_cast<int>(lead));
  }

  // Delay floor the tap may approach at the current ratio: an upshift tap
  // gains on the write head during a fade, so the floor moves out to keep
  // the interpolator behind the newest sample.
  int lowGuard() const {
    return std::max(dMin, static_cast<int>(std::max(0.0, ratio - 1.0) * fadeHi) + 4);
  }

  // Fade length for a drift splice whose best lag matched with `ncc`,
  // within `room`, the samples the destination tap can fade before it runs
  // out of buffer.
  int fadeFor(double ncc, double room) const {
    const double t = juce::jlimit(0.0, 1.0, (kFadeNccHi - ncc) / (kFadeNccHi - kFadeNccLo));
    const double len = fadeLo + t * (fadeHi - fadeLo);
    return std::max(8, static_cast<int>(std::min(len, room)));
  }

  // 0 means off: the crossover keeps its last cutoff and blends out.
  void setTonality(float hz) {
    const bool on = hz > 0.0f;
    if (on) {
      const float cutoff = juce::jlimit(kTonalityMinHz, kTonalityOffHz, hz);
      wetLowpass.setCutoffFrequency(cutoff);
      dryHighpass.setCutoffFrequency(cutoff);
    }
    tonalityMix.setTargetValue(on ? 1.0f : 0.0f);
  }

  void reset() {
    for (auto& r : rings) r.clear();
    control.clear();
    for (auto& r : dryRings) r.clear();
    rA = rB = static_cast<double>(written - dMin);  // start at the floor
    primeLeft = dMin + 8;
    fading = false;
    search = Search{};
    hpZ1 = hpZ2 = 0.0;
    eHf = 0.0;
    minHist.fill(1e9f);
    maxHist.fill(0.0f);
    minAcc = 1e9;
    maxAcc = 0.0;
    lastOnset = -(1 << 30);
  }

  // Normalised cross-correlation of the reference with the corrLen control
  // samples ending at `b`.
  double nccAt(int64_t b) const {
    const float* y = control.windowEndingAt(b, corrLen);
    const float* x = ref.data();
    // Eight independent accumulators: float sums can't be reassociated by
    // the compiler, so this is what lets the loop run as SIMD lanes.
    float xy[8] = {}, yy[8] = {};
    int i = 0;
    for (; i + 8 <= corrLen; i += 8)
      for (int k = 0; k < 8; ++k) {
        xy[k] += x[i + k] * y[i + k];
        yy[k] += y[i + k] * y[i + k];
      }
    for (; i < corrLen; ++i) {
      xy[0] += x[i] * y[i];
      yy[0] += y[i] * y[i];
    }
    double sxy = 0.0, syy = 0.0;
    for (int k = 0; k < 8; ++k) {
      sxy += xy[k];
      syy += yy[k];
    }
    return sxy / (std::sqrt(refXX * syy) + 1e-9);
  }

  // Start scoring candidate delays [lo, hi] against the tap's waveform now,
  // spread over `lead` samples (0: all at once, for the onset re-sync).
  void beginSearch(int lo, int hi, int lead) {
    search.active = true;
    search.ready = false;
    search.now0 = written - 1;
    search.a0 = static_cast<int64_t>(std::floor(rA));
    search.lo = lo;
    search.hi = std::max(lo, hi);
    search.next = lo;
    search.bestScore = -1e9;
    search.bestDelay = lo;
    const float* x = control.windowEndingAt(search.a0, corrLen);
    std::copy(x, x + corrLen, ref.begin());
    refXX = 0.0;
    for (int i = 0; i < corrLen; ++i) refXX += static_cast<double>(x[i]) * x[i];
    const int candidates = (search.hi - search.lo) / coarseStep + 1;
    search.perSample = lead > 0 ? (candidates + lead - 1) / lead : candidates;
  }

  void consider(int d) {
    const double curDelay = static_cast<double>(search.now0 - search.a0);
    const double jump = std::max(1.0, std::abs(curDelay - static_cast<double>(d)));
    const double ncc = nccAt(search.now0 - d);
    const double s = -(1.0 - ncc) / jump;
    if (s > search.bestScore) {
      search.bestScore = s;
      search.bestDelay = d;
      search.bestNcc = ncc;
    }
  }

  // Scores up to `count` coarse candidates; on the last one refines to the
  // sample around the best and publishes the jump.
  void stepSearch(int count) {
    while (count-- > 0 && search.next <= search.hi) {
      consider(search.next);
      search.next += coarseStep;
    }
    if (search.next > search.hi) {
      if (coarseStep > 1) {
        const int c = search.bestDelay;
        for (int d = std::max(search.lo, c - coarseStep + 1); d <= std::min(search.hi, c + coarseStep - 1); ++d)
          if (d != c) consider(d);
      }
      search.resultJump = (search.now0 - search.bestDelay) - search.a0;
      search.active = false;
      search.ready = true;
    }
  }

  // Crossfade to the tap position rA + jump, the one the search just found.
  // A plan kept across a pitch change (see setParams) is checked here: a
  // destination ahead of the write head or off the ring is dropped and the
  // next sample plans afresh.
  void startFade(int64_t jump, int fadeSamples) {
    search.ready = false;
    const double destDelay = static_cast<double>(written - 1) - (rA + static_cast<double>(jump));
    if (destDelay < 2.0 || destDelay > static_cast<double>(rings[0].buf.size()) - 8.0) return;
    // Keep rA's fraction so the splice is a pure integer lag.
    rB = rA + static_cast<double>(jump);
    fading = true;
    fade = 0.0;
    fadeInc = 1.0 / fadeSamples;
    fadeNcc = juce::jlimit(0.0, 1.0, search.bestNcc);
  }

  // Runs the detector on one control sample; true on a pick attack.
  bool detectOnset(double x) {
    const int64_t now = written - 1;
    const double hp = x - hpZ1 + hpA * hpZ2;  // y = x - x[-1] + a * y[-1]
    hpZ1 = x;
    hpZ2 = hp;
    eHf = smoothA * eHf + (1.0 - smoothA) * hp * hp;
    const int64_t cellNow = now / cellLen;
    if (now % cellLen == 0 && now > 0) {
      // Commit the finished cell.
      const auto prev = static_cast<size_t>((cellNow - 1) % kHistoryCells);
      minHist[prev] = static_cast<float>(minAcc);
      maxHist[prev] = static_cast<float>(maxAcc);
      minAcc = 1e9;
      maxAcc = 0.0;
    }
    minAcc = std::min(minAcc, eHf);
    maxAcc = std::max(maxAcc, eHf);
    double recentMin = 1e9, recentMax = 0.0;
    for (int back = kHistorySkipCells; back < kHistoryCells; ++back) {
      const auto c = static_cast<size_t>((cellNow - back + 4 * kHistoryCells) % kHistoryCells);
      recentMin = std::min(recentMin, static_cast<double>(minHist[c]));
      recentMax = std::max(recentMax, static_cast<double>(maxHist[c]));
    }
    return eHf > 1e-7 && eHf > recentMin * kOnsetOverMin && eHf > recentMax * kOnsetOverMax;
  }
};

PitchShift::PitchShift() : impl_(std::make_unique<Impl>()) {}
PitchShift::~PitchShift() = default;

void PitchShift::prepare(double sampleRate, int maxBlockSamples) {
  auto& s = *impl_;
  s.sampleRate = sampleRate;
  s.fadeLen = std::max(8, static_cast<int>(sampleRate * kFadeMs * 0.001));
  s.fadeMaxLen = std::max(s.fadeLen, static_cast<int>(sampleRate * kFadeMaxMs * 0.001));
  s.fadeMinLen = std::max(8, static_cast<int>(sampleRate * kFadeMinMs * 0.001));
  s.onsetFadeLen = std::max(8, static_cast<int>(sampleRate * kOnsetFadeMs * 0.001));
  s.onsetSpan = static_cast<int>(sampleRate * kOnsetSpanMs * 0.001);
  s.refractory = static_cast<int>(sampleRate * kRefractoryMs * 0.001);
  s.searchLead = std::max(1, static_cast<int>(sampleRate * kSearchLeadMs * 0.001));
  s.cellLen = std::max(1, static_cast<int>(sampleRate * 0.001));
  s.coarseStep = std::max(1, juce::roundToInt(sampleRate / kCoarseStepRate));
  s.hpA = std::exp(-2.0 * juce::MathConstants<double>::pi * kDetectorHpfHz / sampleRate);
  s.smoothA = std::exp(-1.0 / (sampleRate * kDetectorSmoothMs * 0.001));
  // Rings hold the largest window plus the correlation reach behind the
  // farthest candidate and the overshoot of a late search and of a
  // downshift tap running deeper through the longest fade.
  const int maxCorr = static_cast<int>(sampleRate * kMaxCorrMs * 0.001);
  const int reach = windowSamples(Window::ms60, sampleRate) + maxCorr + s.fadeMaxLen + 2 * s.searchLead + 64;
  for (auto& r : s.rings) r.init(reach);
  s.control.init(reach);
  s.ref.assign(static_cast<size_t>(maxCorr), 0.0f);
  for (auto& r : s.dryRings) r.init(minDelaySamples(sampleRate) + 8);
  const juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(std::max(1, maxBlockSamples)),
                                    static_cast<juce::uint32>(kMaxChannels)};
  s.wetLowpass.prepare(spec);
  s.wetLowpass.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
  s.dryHighpass.prepare(spec);
  s.dryHighpass.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
  s.tonalityMix.reset(sampleRate, kBlendSeconds);  // lands on its target
  s.setTonality(s.params.tonalityHz);
  // The rings are empty again, so even a running engine blends back in
  // once the tap has audio under it (see primeLeft).
  s.wetMix.reset(sampleRate, kBlendSeconds);
  s.wetMix.setCurrentAndTargetValue(0.0f);
  s.mixSmoother.reset(sampleRate, kBlendSeconds);
  s.mixSmoother.setCurrentAndTargetValue(juce::jlimit(0.0f, 1.0f, s.params.mix));
  s.running = s.enabled;
  s.applyWindow();
  s.reset();
}

void PitchShift::setEnabled(bool on) {
  auto& s = *impl_;
  if (on == s.enabled) return;
  s.enabled = on;
  if (!on) {
    s.wetMix.setTargetValue(0.0f);
  } else if (s.running) {
    // Caught inside the fade-out: the rings are live, just turn around.
    s.wetMix.setTargetValue(1.0f);
  } else {
    // Fresh start: the rings hold whatever played before the last
    // fade-out, and the tap belongs at the floor. The blend-in follows
    // once the ring is primed.
    s.reset();
    s.running = true;
  }
}

bool PitchShift::isRunning() const { return impl_->running; }

void PitchShift::setParams(const Params& p) {
  auto& s = *impl_;
  if (p.window != s.params.window) {
    // The rings are sized for the largest window and stay valid; a tap now
    // outside the new range gets an ordinary splice back in (the drift
    // logic sees its delay past the end). Only a pending search, planned
    // for the old range, is dropped.
    s.params.window = p.window;
    s.applyWindow();
    s.search = Impl::Search{};
  }
  // Exact compare on purpose: this is a parameter value, and a change of
  // any size must reach the engine.
  if (!juce::exactlyEqual(p.semitones, s.params.semitones)) {
    s.params.semitones = p.semitones;
    const double ratio = std::pow(2.0, static_cast<double>(p.semitones) / 12.0);
    // A pending search was planned for the old drift: its candidates were
    // shifted by the distance the tap covers during the lead. A small
    // same-direction change (a knob sweep with STEP off, a block at a time)
    // moves the landing by a few samples, inside the margins the landing
    // range keeps, so the plan stands; a sweep never arrives at the buffer
    // end without one. A direction flip or a big jump would land the tap
    // off the buffer, so those start over.
    const auto direction = [](double r) { return r > 1.0 ? 1 : r < 1.0 ? -1 : 0; };
    if (direction(ratio) != direction(s.ratio) || std::abs(ratio - s.ratio) > kSearchKeepRatio)
      s.search = Impl::Search{};
    s.ratio = ratio;
    s.updateFadeRange();
  }
  if (!juce::exactlyEqual(p.tonalityHz, s.params.tonalityHz)) {
    s.params.tonalityHz = p.tonalityHz;
    s.setTonality(p.tonalityHz);
  }
  if (!juce::exactlyEqual(p.mix, s.params.mix)) {
    s.params.mix = p.mix;
    s.mixSmoother.setTargetValue(juce::jlimit(0.0f, 1.0f, p.mix));
  }
}

void PitchShift::process(juce::AudioBuffer<float>& buffer) {
  auto& s = *impl_;
  const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
  const int numSamples = buffer.getNumSamples();
  if (s.sampleRate <= 0.0 || numChannels <= 0 || numSamples <= 0 || !s.running) return;

  float* out[kMaxChannels];
  for (int ch = 0; ch < kMaxChannels; ++ch) out[ch] = buffer.getWritePointer(juce::jmin(ch, numChannels - 1));
  const float channelScale = 1.0f / static_cast<float>(numChannels);
  const double ratio = s.ratio;
  // How far the tap's delay moves over a search lead / a fade. The lead is
  // padded by a few samples so a planned search always completes before
  // the tap reaches the trigger.
  const double drift = std::abs(1.0 - ratio);
  const double leadDrift = drift * (s.searchLeadNow + 4);
  const int leadDriftI = static_cast<int>(std::ceil(leadDrift));
  // An upshift fade always runs fadeHi (fadeLo meets it there); a downshift
  // one is planned for the shortest and gets whatever room the landing has.
  const int fadeDrift = static_cast<int>(std::ceil(drift * (ratio > 1.0 ? s.fadeHi : s.fadeMinLen)));

  for (int i = 0; i < numSamples; ++i) {
    const int64_t now = s.written++;
    float dry[kMaxChannels];
    float control = 0.0f;
    for (int ch = 0; ch < kMaxChannels; ++ch) {
      dry[ch] = out[ch][i];
      s.rings[static_cast<size_t>(ch)].write(now, dry[ch]);
      if (ch < numChannels) control += dry[ch];
    }
    control *= channelScale;
    s.control.write(now, control);

    const double delay = static_cast<double>(now) - s.rA;
    const int guard = s.lowGuard();
    const bool onset = s.detectOnset(control);
    // Where a splice may land (delay at splice time). Downshift: anywhere
    // from the floor up to where the shortest fade and the next search's
    // lead still fit before dMax. Upshift: the mirror image above the
    // guard. The onset re-sync targets the front of that range.
    const int landLo = ratio > 1.0 ? std::min(guard + fadeDrift + leadDriftI + 2, s.dMax) : s.dMin;
    const int landHi = ratio < 1.0 ? std::max(s.dMin, s.dMax - fadeDrift - leadDriftI - 2) : s.dMax;
    if (!s.fading) {
      if (onset && now - s.lastOnset > s.refractory && delay > landLo + s.onsetSpan) {
        // Onset re-sync: a pick attack brings the tap to the front of the
        // buffer, so attacks arrive with the floor delay wherever the tap
        // was. A small search, run at once.
        s.lastOnset = now;
        s.beginSearch(landLo, std::min(landLo + s.onsetSpan, s.dMax), 0);
        s.stepSearch(s.search.perSample);
        s.startFade(s.search.resultJump, s.onsetFadeLen);
        // The attack becomes the reference: a re-trigger needs a fresh dip.
        s.minHist.fill(static_cast<float>(s.eHf));
        s.maxHist.fill(static_cast<float>(s.eHf));
        s.minAcc = s.maxAcc = s.eHf;
      } else if (ratio < 1.0) {
        // Drift splice, downshift: the tap falls back toward dMax. The
        // search is planned a lead early; a candidate that will have delay
        // dL when the splice comes has delay dL - leadDrift now, so the
        // candidate range is the landing range shifted toward the head.
        // If the plan is late (the ratio or window just changed) the tap
        // simply overshoots dMax by the lead; the rings have the room.
        if (!s.search.active && !s.search.ready && delay >= s.dMax - leadDrift) {
          const int lo = std::max(4, landLo - leadDriftI);
          s.beginSearch(lo, std::max(lo, landHi - leadDriftI), s.searchLeadNow);
        }
        if (s.search.ready && delay >= s.dMax) {
          // The destination keeps drifting deeper while it fades in, so
          // it may fade for as long as it takes to reach the end itself
          // (less the lead the next search needs).
          const double dest = delay - static_cast<double>(s.search.resultJump);
          const double room = (static_cast<double>(s.dMax) - dest - leadDrift - 2.0) / drift;
          s.startFade(s.search.resultJump, s.fadeFor(s.search.bestNcc, room));
        }
      } else if (ratio > 1.0) {
        // Upshift: the tap gains on the write head toward the guard; the
        // candidates sit deeper than where they will land. The guard
        // already leaves room for the longest fade.
        if (!s.search.active && !s.search.ready && delay <= guard + leadDrift)
          s.beginSearch(landLo + leadDriftI, landHi + leadDriftI, s.searchLeadNow);
        if (s.search.ready && delay <= guard) s.startFade(s.search.resultJump, s.fadeFor(s.search.bestNcc, s.fadeHi));
      }
    }
    if (s.search.active) s.stepSearch(s.search.perSample);

    // Read. The fade is a raised cosine; taps that don't correlate add in
    // power rather than amplitude, so the gains are normalised by the taps'
    // correlation (r = 1 leaves the plain complementary fade, r = 0 is the
    // equal-power one), and the level holds through the fade either way.
    float gainA = 1.0f, gainB = 0.0f;
    if (s.fading) {
      gainB = static_cast<float>(0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * s.fade));
      gainA = 1.0f - gainB;
      const float r = static_cast<float>(s.fadeNcc);
      const float norm = std::sqrt(gainA * gainA + gainB * gainB + 2.0f * gainA * gainB * r);
      gainA /= norm;
      gainB /= norm;
      s.fade += s.fadeInc;
    }
    if (s.primeLeft > 0 && --s.primeLeft == 0 && s.enabled) s.wetMix.setTargetValue(1.0f);
    // Power fades the whole engine against the bypassed (zero-latency) input,
    // so a power-on/off is seamless whatever Mix is set to. Mix is the
    // dry/wet blend inside that: the dry it crossfades against is the input
    // held by the floor, the same alignment the tonality band uses, because
    // attacks re-sync the tap to the floor. Without it the dry would lead
    // the shift by the tap's drift and comb against it.
    const float powerFade = s.wetMix.getNextValue();
    const float mix = s.mixSmoother.getNextValue();
    const float tonalityMix = s.tonalityMix.getNextValue();
    for (int ch = 0; ch < numChannels; ++ch) {
      auto& ring = s.rings[static_cast<size_t>(ch)];
      float wet = ring.read(s.rA);
      if (s.fading) wet = wet * gainA + gainB * ring.read(s.rB);
      const float dryRef = ring.at(now - s.dMin);
      auto& dryRing = s.dryRings[static_cast<size_t>(ch)];
      dryRing.write(now, s.dryHighpass.processSample(ch, dry[ch]));
      const float split = s.wetLowpass.processSample(ch, wet) + dryRing.at(now - s.dMin);
      wet += tonalityMix * (split - wet);
      const float mixOut = dryRef + mix * (wet - dryRef);
      out[ch][i] = (1.0f - powerFade) * dry[ch] + powerFade * mixOut;
    }
    if (s.fading && s.fade >= 1.0) {
      s.rA = s.rB;
      s.fading = false;
    }
    s.rA += ratio;
    s.rB += ratio;
  }
  if (!s.enabled && !s.wetMix.isSmoothing()) s.running = false;
}

int PitchShift::latencySamples() const { return (impl_->dMin + impl_->dMax) / 2; }
