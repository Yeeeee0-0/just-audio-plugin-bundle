# JUST Tremolo

A level-modulation effect with Sine, Triangle, Square, Saw Up and Saw Down waveforms.

- Depth, mix, free rate and host-synced rhythmic divisions.
- Phase and stereo phase offset.
- Square-wave duty and edge controls for square/saw shapes.
- Free, Transport and On Play timing modes.
- Input/output gain and bypass.

Transport timing requires Sync; the implementation reports a Free fallback when Transport is selected without Sync.

Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_tremolo
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
