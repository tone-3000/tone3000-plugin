// CPU benchmark: real WaveNet model at 1x vs 8x phase-interleaved.
// Approximates NamEngine's phase configuration: factor N = N instances, each
// processing every Nth sample (per-call block = hostBlock/N per phase... in
// chain terms: chain block = 512*N frames -> each phase gets 512).
//
// Needs the built static NAM lib (its config parsers self-register from
// static initializers, hence -force_load on macOS). Build the plugin first
// (cmake --build build), then from the repo root:
//
//   clang++ -std=c++17 -O2 \
//     -I plugin/NeuralAmpModelerCore \
//     -I plugin/NeuralAmpModelerCore/Dependencies/eigen \
//     -I plugin/NeuralAmpModelerCore/Dependencies/nlohmann \
//     test/src/os_bench.cpp -Wl,-force_load,build/plugin/libNAM.a -o /tmp/os_bench
//   /tmp/os_bench                            # defaults to test/files/a2-amp-cab-test.nam
//   /tmp/os_bench test/files/a2-amp-test.nam # or any other .nam
#include "NAM/get_dsp.h"
#include "NAM/dsp.h"
#include "NAM/slimmable.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>
#include "json.hpp"

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "test/files/a2-amp-cab-test.nam";
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  const nlohmann::json config = nlohmann::json::parse(ss.str());

  const double seconds = 5.0;
  const int baseRate = 48000;
  const int baseBlock = 512;

  for (double slim : {0.0, 1.0})
  for (int factor : {1, 8}) {
    // Phase-safe config: `factor` instances, each at 48k. Chain frames per
    // callback = baseBlock*factor, so each phase still gets baseBlock frames.
    std::vector<std::unique_ptr<nam::DSP>> instances;
    for (int i = 0; i < factor; ++i) {
      auto dsp = nam::get_dsp(config);
      dsp->ResetAndPrewarm(baseRate, baseBlock);
      if (auto* s = dynamic_cast<nam::SlimmableModel*>(dsp.get()))
        s->SetSlimmableSize(slim);
      instances.push_back(std::move(dsp));
    }

    std::vector<double> in(static_cast<size_t>(baseBlock), 0.1);
    std::vector<double> out(static_cast<size_t>(baseBlock), 0.0);

    const int callbacks = static_cast<int>(seconds * baseRate / baseBlock);
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int c = 0; c < callbacks; ++c) {
      for (auto& inst : instances) {
        double* ip[] = {in.data()};
        double* op[] = {out.data()};
        inst->process(ip, op, baseBlock);
      }
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::printf("A2-%s  factor %d: %.3f s CPU for %.1f s audio -> %.1f%% of one core (%.1fx realtime)\n",
                slim < 0.5 ? "Lite" : "Full", factor, elapsed, seconds,
                100.0 * elapsed / seconds, seconds / elapsed);
  }
  return 0;
}
