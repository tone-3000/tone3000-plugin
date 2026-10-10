// Native UI testbed. Renders PluginRoot over the fixture-driven MockBackend:
//
//   UiTestbed --capture <outDir> [--ref <refDir>] [filter...] PNGs (+ diff table)
//   UiTestbed --compare <ref.png> <candidate.png> [diff.png]
//   UiTestbed --selftest                                     unit tests (pure logic)
//   UiTestbed --bench [--seconds N] [--json out] [phase...]  CPU / memory under load (Bench.h)
//   UiTestbed [--scenario <id>] [--live]                     interactive window (--live: moving signal)
//
// Captures are 2x (retina density), so a capture before a rendering change
// and one after diff pixel for pixel (--ref points at the earlier run).

#include <juce_gui_extra/juce_gui_extra.h>

#include "Bench.h"
#include "Compare.h"
#include "Drive.h"
#include "Host.h"
#include "MockBackend.h"
#include "MockSession.h"
#include "Scenarios.h"
#include "SelfTests.h"
#include "core/Design.h"
#include "views/PluginRoot.h"

namespace t3k::ui::testbed {

namespace {

class TestbedWindow : public juce::DocumentWindow {
public:
  TestbedWindow(const Scenario& scenario, const juce::var& fixtures, bool live)
      : DocumentWindow("TONE3000 UI Testbed", juce::Colours::black, allButtons),
        backend(std::make_unique<MockBackend>(scenario.data)),
        host(*backend, scenario, fixtures) {
    backend->setLive(live);
    setUsingNativeTitleBar(true);
    setUsingWindowsMultiTouch(true);  // touch arrives live, as in the plugin (see NativeEditor.h)
    setContentNonOwned(&host, true);
    setResizable(true, false);
    getConstrainer()->setFixedAspectRatio(design::kWidth / double(host.getHeight()));
    centreWithSize(host.getWidth(), host.getHeight());
    setVisible(true);
  }
  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
  std::unique_ptr<MockBackend> backend;
  ScaledHost host;
};

// Renders one scenario offscreen and returns the 2x snapshot, plus the
// screen's unnamed Tab stops (the accessibility audit rides along with the
// pixel capture: every screen the suite reaches gets checked).
juce::Image captureScenario(const Scenario& scenario, const juce::var& fixtures, bool& driveMissing,
                            juce::StringArray& unnamed) {
  MockBackend backend(scenario.data);
  ScaledHost host(backend, scenario, fixtures);
  auto& root = host.pluginRoot();

  auto* mm = juce::MessageManager::getInstance();
  mm->runDispatchLoopUntil(500);  // let the first async loads (images, chain) land
  if (const auto* drive = driveFor(scenario.id)) {
    (*drive)(root, backend);
  } else if (scenario.hasDrive) {
    driveMissing = true;
  }
  mm->runDispatchLoopUntil(scenario.settleMs());
  unnamed = drive::unnamedFocusables(root);
  return root.createComponentSnapshot(root.getLocalBounds(), true, 2.0f);
}

int runCapture(const juce::StringArray& args) {
  const juce::File outDir(juce::File::getCurrentWorkingDirectory().getChildFile(args[1]));
  outDir.createDirectory();
  juce::File refDir;
  juce::StringArray filters;  // substrings; a scenario runs if it matches any
  for (int i = 2; i < args.size(); ++i) {
    if (args[i] == "--ref" && i + 1 < args.size())
      refDir = juce::File::getCurrentWorkingDirectory().getChildFile(args[++i]);
    else
      filters.add(args[i]);
  }
  const auto selected = [&](const juce::String& id) {
    if (filters.isEmpty()) return true;
    for (const auto& f : filters)
      if (id.contains(f)) return true;
    return false;
  };

  const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
  juce::PNGImageFormat png;
  int captured = 0, unnamedTotal = 0;
  for (const auto& scenario : fixtures.scenarios) {
    if (!selected(scenario.id)) continue;
    bool driveMissing = false;
    juce::StringArray unnamed;
    const auto image = captureScenario(scenario, fixtures.root, driveMissing, unnamed);
    const auto file = outDir.getChildFile(scenario.id + ".png");
    file.deleteFile();
    if (juce::FileOutputStream out(file); out.openedOk())
      png.writeImageToStream(image, out);
    ++captured;

    juce::String line = scenario.id.paddedRight(' ', 34);
    if (driveMissing)
      line += " (drive not mirrored)";
    if (refDir != juce::File()) {
      const auto ref = refDir.getChildFile(scenario.id + ".png");
      const auto result = comparePngFiles(ref, file, outDir.getChildFile(scenario.id + ".diff.png"));
      line += result.ok ? juce::String::formatted(" %6.2f%% mismatch, worst tile %5.1f%% at %d,%d",
                                                  result.mismatchPercent(), result.worstTilePercent,
                                                  result.worstTile.x, result.worstTile.y)
                        : " " + result.error;
    }
    if (!unnamed.isEmpty()) {
      line += " a11y: " + juce::String(unnamed.size()) + " unnamed Tab stop(s)";
      for (const auto& u : unnamed) line += "\n    " + u;
      unnamedTotal += unnamed.size();
    }
    std::cout << line << std::endl;
  }
  std::cout << captured << " scenario(s) captured to " << outDir.getFullPathName() << std::endl;
  if (unnamedTotal > 0) {
    std::cout << unnamedTotal << " Tab stop(s) without a screen-reader name (see a11y lines above)" << std::endl;
    return 1;
  }
  return 0;
}

int runCompare(const juce::StringArray& args) {
  const auto cwd = juce::File::getCurrentWorkingDirectory();
  const auto result = comparePngFiles(cwd.getChildFile(args[1]), cwd.getChildFile(args[2]),
                                      args.size() > 3 ? cwd.getChildFile(args[3]) : juce::File());
  if (!result.ok) {
    std::cerr << result.error << std::endl;
    return 2;
  }
  std::cout << juce::String::formatted("%lld px mismatched of %dx%d (%.3f%%), worst tile %.1f%% at %d,%d",
                                       static_cast<long long>(result.mismatched), result.width,
                                       result.height, result.mismatchPercent(), result.worstTilePercent,
                                       result.worstTile.x, result.worstTile.y)
            << std::endl;
  return 0;
}

}  // namespace

class TestbedApp : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override { return "UiTestbed"; }
  const juce::String getApplicationVersion() override { return "1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String& commandLine) override {
    const auto args = juce::StringArray::fromTokens(commandLine, true);
    // Never the real Library (library state files are written into it).
    LibraryStore::defaultRootOverride = libraryRoot.getFile();
    if (args.size() >= 2 && args[0] == "--capture") {
      setApplicationReturnValue(runCapture(args));
      quit();
      return;
    }
    if (args.size() >= 3 && args[0] == "--compare") {
      setApplicationReturnValue(runCompare(args));
      quit();
      return;
    }
    if (args.size() >= 1 && args[0] == "--bench") {
      bench = Bench::start(args, [this](int code) {
        setApplicationReturnValue(code);
        quit();
      });
      return;
    }
    if (args.size() >= 1 && args[0] == "--selftest") {
      setApplicationReturnValue(runSelfTests(args));
      quit();
      return;
    }
    const auto fixtures = Fixtures::load(fixturesDir().getChildFile("scenarios.json"));
    juce::String id = "main-mono";
    for (int i = 0; i + 1 < args.size(); ++i)
      if (args[i] == "--scenario")
        id = args[i + 1];
    const auto* scenario = fixtures.find(id);
    if (scenario == nullptr) {
      std::cerr << "unknown scenario: " << id << std::endl;
      setApplicationReturnValue(1);
      quit();
      return;
    }
    window = std::make_unique<TestbedWindow>(*scenario, fixtures.root, args.contains("--live"));
  }

  void shutdown() override {
    bench.reset();
    window.reset();
    libraryRoot.getFile().deleteRecursively();
  }

private:
  const juce::TemporaryFile libraryRoot;  // deleted on exit (a folder: TemporaryFile removes only a file)
  std::unique_ptr<TestbedWindow> window;
  std::unique_ptr<Bench> bench;
};

}  // namespace t3k::ui::testbed

START_JUCE_APPLICATION(t3k::ui::testbed::TestbedApp)
