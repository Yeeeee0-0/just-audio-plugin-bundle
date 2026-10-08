# JUST Compressor

A compressor with Clean and Punch styles and Peak, RMS or blended detection.

- Threshold, ratio, attack, release, knee and gain-reduction range.
- Input gain, makeup gain, parallel mix and stereo link.
- Internal/external sidechain selection, sidechain gain and optional high-pass/low-pass filters.

**Lookahead is pending in this release.** The parameter and maximum-lookahead configuration can retain saved requests, but the engine uses zero actual lookahead and reports zero latency. A stored nonzero request must not be presented as an active pre-delay.

Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.cpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_compressor
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
