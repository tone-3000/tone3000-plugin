## Summary

<!-- Why this exists. Keep it small: no speculative fallbacks, no dead code. -->

## Test plan

CI does not run on pull requests. A maintainer can dispatch **Build Plugin** from the Actions tab when they want a full signed build. Run the local checks this PR can break:

- [ ] DSP: `./script/test-dsp.sh` (or N/A: no audio/chain/state change)
- [ ] UI: `UiTestbed --selftest`, and a `--capture` before/after diff for visual changes (or N/A: no `plugin/ui/` change; see `plugin/ui/README.md`)
- [ ] Host validators, if you touched the processor, editor, or plugin wrappers: `./script/validate-plugin.sh` ([pluginval](https://github.com/Tracktion/pluginval) strictness 10 for VST3/AU, clap-validator, lv2lint). AAX and Standalone are skipped by that script.
- [ ] Host smoke (DAW + format + sample rate), if this is user-visible

If you changed DSP behavior on purpose, update the GoogleTest in the same commit and say why.

## Compatibility

These are contracts from the first public release. Leave unchecked only if this PR does not touch that surface.

- [ ] AU parameter version hints in `Processor.cpp` are not reused or renumbered
- [ ] LV2 URI and CLAP ID in `plugin/CMakeLists.txt` are unchanged
- [ ] State format is unchanged, or `kStateSchemaVersion` is bumped and the old tree is handled explicitly
- [ ] README / `plugin/docs/` updated if the public build or behavior changed
