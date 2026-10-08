# JUST Gate

A dynamics effect with Gate, Expand and Duck modes.

- Threshold and attenuation range; ratio/knee for expansion.
- Attack, hold, release and hysteresis where applicable to the selected mode.
- Peak/RMS detection, internal/external sidechain and optional sidechain high-pass/low-pass filtering.
- Input/output gain and bypass.

**Lookahead is pending in this release.** Nonzero lookahead/configuration requests are recorded as pending. The current engine reports zero latency and does not provide an active lookahead delay.

Implementation: `Module.cpp`, `Parameters.hpp`, `GateEngine.cpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_gate
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
