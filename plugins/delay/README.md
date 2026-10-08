# JUST Delay

A delay with Stereo, Dual and Ping-Pong routing.

- Independent left/right delay times and sync divisions, plus a fallback tempo.
- Four tap controls with level, pan and enable state; additional time controls for taps 3 and 4.
- Feedback, crossfeed, feedback high-pass/high-cut filtering and freeze.
- Wet drive/tilt, modulation, ducking, wet width and wet level.
- Repitch and Smooth time-change modes.

The Simple Time control can represent a custom state when independent timing settings differ. Advanced values remain part of the saved sound state. Freeze can sustain repeats.

Implementation: `Module.cpp`, `Parameters.hpp`, `DelayEngine.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_delay
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
