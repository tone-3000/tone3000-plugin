#include "Bench.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include "Drive.h"
#include "Host.h"
#include "MockBackend.h"
#include "ProcessStats.h"
#include "Scenarios.h"
#include "views/PluginRoot.h"
#include "views/block/BlockEqView.h"
#include "views/browser/ToneCard.h"
#include "views/gallery/AddTile.h"
#include "views/gallery/ToneTile.h"
#include "widgets/DragScroller.h"
#include "widgets/Knob.h"

namespace t3k::ui::testbed {

namespace {

constexpr int kTickMs = 16;
constexpr double kStallMs = 50;
constexpr double kPi = 3.141592653589793;

struct Options {
  double seconds = 10;
  double warmup = 1.5;
  double zoom = 0;  // 0: the scenario's own
  juce::File json;
  juce::StringArray filters;
  bool list = false;
};

// Topmost child of the host that paints nothing and counts what the window
// repaints: it overlaps everything, so every paint pass reaches it with the
// dirty region as its clip.
class PaintProbe : public juce::Component {
public:
  PaintProbe() {
    setInterceptsMouseClicks(false, false);
    setAccessible(false);
    setWantsKeyboardFocus(false);
  }
  void paint(juce::Graphics& g) override {
    ++paints;
    const auto clip = g.getClipBounds();
    area += static_cast<double>(clip.getWidth()) * clip.getHeight();
  }
  void reset() {
    paints = 0;
    area = 0;
  }
  juce::int64 paints = 0;
  double area = 0;  // px², bounding box of each pass's dirty region
};

// Mouse and keys through the window's peer, as the OS delivers them: hit
// testing, hover, cursors and JUCE's drag/click plumbing all run.
struct Pointer {
  explicit Pointer(juce::ComponentPeer& p) : peer(p) {}

  juce::ComponentPeer& peer;
  juce::Point<float> pos;
  bool down = false;
  juce::int64 time = 0;

  juce::int64 stamp() { return time = std::max(time + 1, juce::Time::currentTimeMillis()); }
  void send() {
    peer.handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, pos,
                          down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f, 0.0f, stamp());
  }
  void moveTo(juce::Point<float> p) {
    pos = p;
    send();
  }
  void press() {
    down = true;
    send();
  }
  void release() {
    if (!down) return;
    down = false;
    send();
  }
  void click(juce::Point<float> p) {
    moveTo(p);
    press();
    release();
  }
  void wheel(float deltaY) {
    juce::MouseWheelDetails w{};
    w.deltaY = deltaY;
    w.isSmooth = true;  // a trackpad
    peer.handleMouseWheel(juce::MouseInputSource::InputSourceType::mouse, pos, stamp(), w);
  }
  void key(int code) { peer.handleKeyPress(juce::KeyPress(code)); }
  juce::Point<float> at(juce::Component& c, juce::Point<float> local) const {
    return peer.getComponent().getLocalPoint(&c, local);
  }
  juce::Point<float> centreOf(juce::Component& c) const { return at(c, c.getLocalBounds().getCentre().toFloat()); }
};

// What a phase's workload sees.
struct Ctx {
  Ctx(PluginRoot& r, MockBackend& b, Pointer& p, juce::StringArray& n) : root(r), backend(b), pointer(p), notes(n) {}

  PluginRoot& root;
  MockBackend& backend;
  Pointer& pointer;
  juce::StringArray& notes;
  double t = 0;  // seconds since the phase's ticks began
  juce::Component::SafePointer<juce::Component> target;
  juce::Point<float> anchor;
  double baseline = 0;
  double progress = 0;  // workload-defined: proof it did something
  int step = -1;
  double nextStepAt = 0;
};

struct Phase {
  Phase(const char* n, const char* w, const char* s, bool isLive = true, bool withAuth = false,
        std::function<void(Ctx&)> onSetup = {}, std::function<void(Ctx&)> onTick = {},
        std::function<juce::String(const Ctx&)> onCheck = {})
      : name(n), what(w), scenario(s), live(isLive), auth(withAuth), setup(std::move(onSetup)),
        tick(std::move(onTick)), check(std::move(onCheck)) {}

  const char* name;
  const char* what;
  const char* scenario;
  bool live;
  bool auth;
  std::function<void(Ctx&)> setup;  // after the scenario settles; may pump
  std::function<void(Ctx&)> tick;   // every 16 ms through warm-up and measurement
  std::function<juce::String(const Ctx&)> check;  // a note when the workload did not happen
};

juce::Component* withHelp(juce::Component& root, const juce::String& prefix) {
  return drive::find(root, [&](juce::Component& c) { return c.isShowing() && c.getHelpText().startsWith(prefix); });
}

template <typename T>
T* showing(juce::Component& root) {
  return dynamic_cast<T*>(
      drive::find(root, [](juce::Component& c) { return dynamic_cast<T*>(&c) != nullptr && c.isShowing(); }));
}

std::function<void(Ctx&)> scenarioDrive(const char* id) {
  return [id](Ctx& c) {
    if (const auto* drive = driveFor(id)) (*drive)(c.root, c.backend);
  };
}

// -1..1..-1 over `period` seconds.
double triangle(double t, double period) {
  const double x = std::fmod(t / period, 1.0);
  return x < 0.5 ? 4 * x - 1 : 3 - 4 * x;
}

std::vector<Phase> phases() {
  std::vector<Phase> list;

  list.push_back({"idle", "chain view, no signal: the floor (timers only, nothing moves)", "main-mono", false});
  list.push_back({"gallery", "chain view playing: meters, tile glows, clip LEDs, CPU readout", "main-mono"});
  list.push_back({"stereo", "stereo lanes playing: both chains, correlation meter", "main-stereo"});
  list.push_back({"detail", "block detail card playing", "main-detail", true, false, scenarioDrive("main-detail")});
  list.push_back({"eq-curve", "EQ curve view playing: the live output spectrum at 30 fps", "main-detail-eq-curve",
                  true, false, scenarioDrive("main-detail-eq-curve")});

  // Find a band dot by sweeping the pointer over the graph until the curve
  // reports one under it (its help text turns into the dot's), then drag it
  // round in circles.
  list.push_back({"eq-drag", "EQ curve playing, dragging a band dot in circles", "main-detail-eq-curve", true, false,
                  [](Ctx& c) {
                    scenarioDrive("main-detail-eq-curve")(c);
                    auto* eq = showing<BlockEqView>(c.root);
                    if (eq == nullptr) return;
                    auto& top = c.pointer.peer.getComponent();
                    const auto box = top.getLocalArea(eq, eq->getLocalBounds()).toFloat();
                    for (float y = box.getY(); y < box.getBottom(); y += 4) {
                      for (float x = box.getX(); x < box.getRight(); x += 4) {
                        c.pointer.moveTo({x, y});
                        auto* under = top.getComponentAt(juce::Point<float>(x, y).roundToInt());
                        if (under != nullptr && under->getHelpText().startsWith("Band Dot")) {
                          c.anchor = {x, y};
                          c.pointer.press();
                          return;
                        }
                      }
                    }
                  },
                  [](Ctx& c) {
                    if (!c.pointer.down) return;
                    const double a = 2 * kPi * c.t / 2.0;
                    c.pointer.moveTo(c.anchor + juce::Point<float>(static_cast<float>(40 * (std::cos(a) - 1)),
                                                                   static_cast<float>(25 * std::sin(a))));
                    c.progress += 1;
                  },
                  [](const Ctx& c) { return c.progress > 0 ? juce::String() : juce::String("no band dot found to drag"); }});

  list.push_back({"tuner", "tuner playing: the needle and note readout follow the pitch", "main-tuner-intune", true,
                  false, scenarioDrive("main-tuner-intune")});

  list.push_back({"knob-drag", "chain view playing, dragging the Input knob up and down", "main-mono", true, false,
                  [](Ctx& c) {
                    auto* knob = dynamic_cast<Knob*>(drive::find(c.root, [](juce::Component& k) {
                      return dynamic_cast<Knob*>(&k) != nullptr && k.isShowing() && k.getTitle() == "Input";
                    }));
                    if (knob == nullptr) return;
                    c.target = knob;
                    c.baseline = knob->value();
                    const float face = static_cast<float>(knob->getWidth()) / 2;  // the face is the square on top
                    c.anchor = c.pointer.at(*knob, {face, face});
                    c.pointer.moveTo(c.anchor);
                    c.pointer.press();
                  },
                  [](Ctx& c) {
                    auto* knob = dynamic_cast<Knob*>(c.target.getComponent());
                    if (knob == nullptr) return;
                    c.pointer.moveTo(c.anchor.translated(0, static_cast<float>(60 * triangle(c.t, 3.0))));
                    c.progress = std::max(c.progress, std::abs(knob->value() - c.baseline));
                  },
                  [](const Ctx& c) {
                    if (c.target == nullptr) return juce::String("Input knob not found");
                    return c.progress > 0 ? juce::String() : juce::String("the Input knob never turned");
                  }});

  list.push_back({"browser", "tone browser open on a grid of cards, playing", "browser-search"});

  list.push_back({"browser-scroll", "tone browser, trackpad-scrolling the grid up and down", "browser-search", true,
                  false,
                  [](Ctx& c) {
                    auto* card = showing<ToneCard>(c.root);
                    auto* scroller = card != nullptr ? card->findParentComponentOfClass<DragScroller>() : nullptr;
                    if (scroller == nullptr) return;
                    c.target = scroller;
                    c.pointer.moveTo(c.pointer.centreOf(*scroller));
                  },
                  [](Ctx& c) {
                    auto* scroller = dynamic_cast<DragScroller*>(c.target.getComponent());
                    if (scroller == nullptr) return;
                    c.pointer.wheel(static_cast<int>(c.t / 1.5) % 2 == 0 ? -0.05f : 0.05f);
                    c.progress = std::max(c.progress, static_cast<double>(scroller->getViewPositionY()));
                  },
                  [](const Ctx& c) { return c.progress > 0 ? juce::String() : juce::String("the grid never scrolled"); }});

  // A user clicking around, one click every 1.2 s: open a block, its EQ and
  // curve, back, the tuner on and off, the preset browser and away, the
  // tone browser and back.
  list.push_back({"tour", "clicking through screens: detail, EQ, tuner, presets, tone browser", "main-mono", true,
                  true, nullptr, [](Ctx& c) {
                    struct Step {
                      const char* label;
                      std::function<juce::Component*(PluginRoot&)> find;
                      int key = 0;
                    };
                    auto help = [](const char* prefix) {
                      return [prefix](PluginRoot& r) { return withHelp(r, prefix); };
                    };
                    static const std::vector<Step> steps = {
                        {"a tone tile", [](PluginRoot& r) -> juce::Component* { return showing<ToneTile>(r); }},
                        {"EQ", help("EQ:")},
                        {"Curve", help("Curve:")},
                        {"Back", help("Back:")},
                        {"Tuner (on)", help("Tuner:")},
                        {"Tuner (off)", help("Tuner:")},
                        {"Presets", help("Presets:")},
                        {"Escape", nullptr, juce::KeyPress::escapeKey},
                        {"Add Tone", [](PluginRoot& r) -> juce::Component* { return showing<AddTile>(r); }},
                        {"Close browser", help("Close: back")},
                    };
                    if (c.t < c.nextStepAt) return;
                    c.nextStepAt = c.t + 1.2;
                    c.step = (c.step + 1) % static_cast<int>(steps.size());
                    const auto& step = steps[static_cast<size_t>(c.step)];
                    if (step.key != 0) {
                      c.pointer.key(step.key);
                    } else if (auto* target = step.find(c.root)) {
                      c.pointer.click(c.pointer.centreOf(*target));
                      c.progress += 1;
                    } else {
                      c.notes.addIfNotAlreadyThere(juce::String("tour: ") + step.label + " not found");
                    }
                  }});
  return list;
}

struct Result {
  juce::String name, what, note;
  double seconds = 0, processCpu = 0, threadCpu = 0, paintsPerSec = 0, repaintWindows = 0, maxGapMs = 0;
  int stalls = 0;
  stats::Memory memory;
};

// One phase's window and everything under it. Members are torn down in
// reverse: the workload, the pointer, the window, then the UI and the mock.
struct Run {
  Scenario scenario;
  std::unique_ptr<MockBackend> backend;
  std::unique_ptr<ScaledHost> host;
  PaintProbe probe;
  std::unique_ptr<juce::DocumentWindow> window;
  std::unique_ptr<Pointer> pointer;
  std::unique_ptr<Ctx> ctx;
  juce::StringArray notes;

  ~Run() {
    if (pointer != nullptr) pointer->release();
  }
};

class BenchRun : public Bench, private juce::Timer {
public:
  BenchRun(Options options, std::vector<Phase> list, std::function<void(int)> done)
      : options_(std::move(options)), phases_(std::move(list)), done_(std::move(done)),
        fixtures_(Fixtures::load(fixturesDir().getChildFile("scenarios.json"))) {
    startMemory_ = stats::memory();
    std::cout << "UiTestbed bench: " << phases_.size() << " phase(s), " << options_.seconds << " s each after a "
              << options_.warmup << " s warm-up. Leave the mouse alone and the window unobscured." << std::endl;
    startTimer(kTickMs);
  }

  ~BenchRun() override { stopTimer(); }

private:
  enum class Stage { open, warmup, measure };

  static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

  void timerCallback() override {
    if (stage_ == Stage::open) {
      open();
      return;
    }
    const double now = nowMs();
    if (stage_ == Stage::measure) gaps_.push_back(now - lastTick_);
    lastTick_ = now;
    auto& ctx = *run_->ctx;
    ctx.t = (now - ticksFrom_) / 1000.0;
    if (run_->probe.getBounds() != run_->host->getLocalBounds()) run_->probe.setBounds(run_->host->getLocalBounds());
    if (const auto& tick = phases_[index_].tick) tick(ctx);

    const double elapsed = (now - stageFrom_) / 1000.0;
    if (stage_ == Stage::warmup && elapsed >= options_.warmup) {
      beginMeasure(now);
    } else if (stage_ == Stage::measure && elapsed >= options_.seconds) {
      endMeasure(now);
      close();
    }
  }

  // Build the phase's window and bring its scenario up (setup may pump the
  // loop, so this timer is off meanwhile).
  void open() {
    stopTimer();
    const auto& phase = phases_[index_];
    const auto* source = fixtures_.find(phase.scenario);
    if (source == nullptr) {
      std::cerr << "bench: scenario " << phase.scenario << " missing" << std::endl;
      finish(1);
      return;
    }
    run_ = std::make_unique<Run>();
    run_->scenario = *source;
    run_->scenario.data = juce::JSON::parse(juce::JSON::toString(source->data));
    if (auto* data = run_->scenario.data.getDynamicObject()) {
      if (phase.auth) data->setProperty("auth", true);
      if (options_.zoom > 0) data->setProperty("zoom", options_.zoom);
    }
    run_->backend = std::make_unique<MockBackend>(run_->scenario.data);
    run_->backend->setLive(phase.live);
    run_->host = std::make_unique<ScaledHost>(*run_->backend, run_->scenario, fixtures_.root);
    run_->host->addAndMakeVisible(run_->probe);
    run_->probe.setBounds(run_->host->getLocalBounds());
    run_->window = std::make_unique<juce::DocumentWindow>("UiTestbed bench: " + juce::String(phase.name),
                                                          juce::Colours::black, 0);
    run_->window->setUsingNativeTitleBar(true);
    run_->window->setContentNonOwned(run_->host.get(), true);
    run_->window->centreWithSize(run_->window->getWidth(), run_->window->getHeight());
    // Peer input is dropped at points where another app's window is on top
    // (JUCE hit-tests against the window server), and an app launched from a
    // shell is not necessarily brought to the front.
    run_->window->setAlwaysOnTop(true);
    run_->window->setVisible(true);
    juce::Process::makeForegroundProcess();
    run_->window->toFront(true);
    drive::wait(run_->scenario.settleMs());

    auto* peer = run_->window->getPeer();
    if (peer == nullptr) {
      std::cerr << "bench: no window peer (no display?)" << std::endl;
      finish(1);
      return;
    }
    run_->pointer = std::make_unique<Pointer>(*peer);
    run_->ctx = std::make_unique<Ctx>(run_->host->pluginRoot(), *run_->backend, *run_->pointer, run_->notes);
    if (phase.setup) phase.setup(*run_->ctx);

    stage_ = Stage::warmup;
    ticksFrom_ = stageFrom_ = lastTick_ = nowMs();
    startTimer(kTickMs);
  }

  void beginMeasure(double now) {
    stage_ = Stage::measure;
    stageFrom_ = now;
    gaps_.clear();
    run_->probe.reset();
    cpuFrom_ = stats::processCpuSeconds();
    threadFrom_ = stats::threadCpuSeconds();
  }

  void endMeasure(double now) {
    const auto& phase = phases_[index_];
    Result r;
    r.name = phase.name;
    r.what = phase.what;
    r.seconds = (now - stageFrom_) / 1000.0;
    r.processCpu = 100.0 * (stats::processCpuSeconds() - cpuFrom_) / r.seconds;
    r.threadCpu = 100.0 * (stats::threadCpuSeconds() - threadFrom_) / r.seconds;
    r.paintsPerSec = static_cast<double>(run_->probe.paints) / r.seconds;
    const double windowArea = static_cast<double>(run_->host->getWidth()) * run_->host->getHeight();
    r.repaintWindows = windowArea > 0 ? run_->probe.area / windowArea / r.seconds : 0;
    // A 16 ms timer's gaps sit around 16-25 ms even when idle (timer
    // granularity), so only long ones say anything: the message thread was
    // busy for most of a frame or more.
    for (const double gap : gaps_) {
      r.maxGapMs = std::max(r.maxGapMs, gap);
      if (gap > kStallMs) ++r.stalls;
    }
    r.memory = stats::memory();
    if (phase.check) run_->notes.addIfNotAlreadyThere(phase.check(*run_->ctx));
    run_->notes.removeEmptyStrings();
    r.note = run_->notes.joinIntoString("; ");
    windowSize_ = {run_->host->getWidth(), run_->host->getHeight()};
    printRow(r);
    results_.push_back(std::move(r));
  }

  void close() {
    stopTimer();
    run_.reset();
    drive::wait(300);  // async deletes and the last repaints of the closed window
    if (++index_ >= phases_.size()) {
      finish(report());
      return;
    }
    stage_ = Stage::open;
    startTimer(kTickMs);
  }

  void finish(int code) {
    stopTimer();
    run_.reset();
    juce::MessageManager::callAsync([done = done_, code] { done(code); });
  }

  // juce::String(double, 0) means "as many decimals as it takes".
  static juce::String number(double v, int decimals) {
    return decimals > 0 ? juce::String(v, decimals) : juce::String(juce::roundToInt(v));
  }
  static juce::String cell(const juce::String& text, int width) { return text.paddedLeft(' ', width); }

  void printRow(const Result& r) {
    if (!headerPrinted_) {
      headerPrinted_ = true;
      std::cout << juce::String("phase").paddedRight(' ', 16) << cell("proc CPU", 9) << cell("UI thread", 11)
                << cell("paints/s", 10) << cell("repaint", 9) << cell("stalls", 8) << cell("max gap", 10) << "   "
                << stats::memoryKind() << " MB" << std::endl;
    }
    std::cout << r.name.paddedRight(' ', 16) << cell(number(r.processCpu, 1) + "%", 9)
              << cell(number(r.threadCpu, 1) + "%", 11) << cell(number(r.paintsPerSec, 0), 10)
              << cell(number(r.repaintWindows, 1), 9) << cell(juce::String(r.stalls), 8)
              << cell(number(r.maxGapMs, 0) + " ms", 10) << "   " << number(r.memory.currentMb, 0) << " (peak "
              << number(r.memory.peakMb, 0) << ")" << (r.note.isNotEmpty() ? "   ! " + r.note : juce::String())
              << std::endl;
  }

  int report() {
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    std::cout << "window " << windowSize_.x << "x" << windowSize_.y << " px at " << (display ? display->scale : 1.0)
              << "x display scale; CPU is % of one core; stalls: ticks more than " << number(kStallMs, 0)
              << " ms apart (ideal 16); memory at start " << number(startMemory_.currentMb, 0)
              << " MB" << std::endl;
    if (options_.json != juce::File()) {
      juce::Array<juce::var> rows;
      for (const auto& r : results_) {
        auto* o = new juce::DynamicObject();
        o->setProperty("phase", r.name);
        o->setProperty("what", r.what);
        o->setProperty("seconds", r.seconds);
        o->setProperty("processCpuPercent", r.processCpu);
        o->setProperty("uiThreadCpuPercent", r.threadCpu);
        o->setProperty("paintsPerSecond", r.paintsPerSec);
        o->setProperty("repaintWindowsPerSecond", r.repaintWindows);
        o->setProperty("stalls", r.stalls);
        o->setProperty("maxGapMs", r.maxGapMs);
        o->setProperty("memoryMb", r.memory.currentMb);
        o->setProperty("memoryPeakMb", r.memory.peakMb);
        o->setProperty("note", r.note);
        rows.add(juce::var(o));
      }
      auto* root = new juce::DynamicObject();
      root->setProperty("platform", juce::SystemStats::getOperatingSystemName());
      root->setProperty("cpu", juce::SystemStats::getCpuModel());
      root->setProperty("memoryKind", juce::String(stats::memoryKind()));
      root->setProperty("windowWidth", windowSize_.x);
      root->setProperty("windowHeight", windowSize_.y);
      root->setProperty("displayScale", display ? display->scale : 1.0);
      root->setProperty("phases", rows);
      options_.json.replaceWithText(juce::JSON::toString(juce::var(root)));
      std::cout << "wrote " << options_.json.getFullPathName() << std::endl;
    }
    for (const auto& r : results_)
      if (r.note.isNotEmpty()) return 1;  // a workload that did not run measured nothing
    return 0;
  }

  Options options_;
  std::vector<Phase> phases_;
  std::function<void(int)> done_;
  Fixtures fixtures_;
  size_t index_ = 0;
  Stage stage_ = Stage::open;
  std::unique_ptr<Run> run_;
  double ticksFrom_ = 0, stageFrom_ = 0, lastTick_ = 0, cpuFrom_ = 0, threadFrom_ = 0;
  std::vector<double> gaps_;
  std::vector<Result> results_;
  stats::Memory startMemory_;
  juce::Point<int> windowSize_;
  bool headerPrinted_ = false;
};

void usage() {
  std::cout << "UiTestbed --bench [--seconds N] [--warmup N] [--zoom Z] [--json out.json] [--list] [phase...]\n"
               "  phase: substrings of phase names to run (default: all)" << std::endl;
}

}  // namespace

std::unique_ptr<Bench> Bench::start(const juce::StringArray& args, std::function<void(int)> done) {
  Options options;
  for (int i = 1; i < args.size(); ++i) {
    const auto& a = args[i];
    const bool hasValue = i + 1 < args.size();
    if (a == "--seconds" && hasValue) options.seconds = args[++i].getDoubleValue();
    else if (a == "--warmup" && hasValue) options.warmup = args[++i].getDoubleValue();
    else if (a == "--zoom" && hasValue) options.zoom = args[++i].getDoubleValue();
    else if (a == "--json" && hasValue) options.json = juce::File::getCurrentWorkingDirectory().getChildFile(args[++i]);
    else if (a == "--list") options.list = true;
    else if (a.startsWith("--")) {
      usage();
      done(2);
      return nullptr;
    } else options.filters.add(a);
  }

  std::vector<Phase> chosen;
  for (auto& phase : phases()) {
    bool match = options.filters.isEmpty();
    for (const auto& f : options.filters) match = match || juce::String(phase.name).contains(f);
    if (match) chosen.push_back(std::move(phase));
  }
  if (options.list || chosen.empty()) {
    for (const auto& p : options.list ? phases() : chosen)
      std::cout << juce::String(p.name).paddedRight(' ', 16) << p.what << "  [" << p.scenario << "]" << std::endl;
    if (chosen.empty()) std::cerr << "no phase matches" << std::endl;
    done(chosen.empty() ? 2 : 0);
    return nullptr;
  }
  if (options.seconds <= 0) options.seconds = 10;
  return std::make_unique<BenchRun>(std::move(options), std::move(chosen), std::move(done));
}

}  // namespace t3k::ui::testbed
