# Ardour e2e

Loads the built LV2 and VST3 into a real Ardour session, headless through
`luasession` on Ardour's Dummy backend. Linux only, needs Ardour 9 from the
distro (`luasession` and `ardour-vst3-scanner` under `/usr/lib/ardour9`).

```sh
test/e2e/run.py                                  # every scenario
test/e2e/run.py --runs 3 state_roundtrip         # one scenario, three times
test/e2e/run.py --plugin-dir build/plugin/TONE3000_artefacts/Release
```

- `smoke`: the plugin inserts and passes audio.
- `state_roundtrip`: a parameter survives session save and reload (VST3).

luasession cannot import audio, so a Lua DSP processor (`lua/player.lua`)
plays a sine into the plugin.
