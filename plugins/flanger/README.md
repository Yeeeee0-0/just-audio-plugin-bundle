# JUST Flanger

A short-delay modulation effect with LFO and Manual modes.

- Base delay, modulation depth and rate.
- Sine/Triangle waveforms, rhythmic sync divisions and Free/Transport/On Play timing.
- Phase and stereo phase controls.
- Positive or negative feedback with a feedback low-pass filter.
- Wet-polarity inversion, mix, input/output gain and bypass.

Manual mode uses the phase control directly; modulation depth and rate are not editable in that mode.

Implementation: `Module.cpp`, `Parameters.hpp`, `FlangerEngine.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_flanger
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
