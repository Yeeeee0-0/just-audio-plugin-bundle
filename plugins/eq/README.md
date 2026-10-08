# JUST EQ

A twelve-band minimum-phase equalizer with stereo, mid and side band targets.

- Bell, low shelf, high shelf, high-pass, low-pass and notch shapes.
- Band frequency, gain, Q and slope controls; dynamic controls with Peak/RMS detection and internal/external sources.
- Native spectrum editor with selected-band controls and band audition.
- Input/output gain and bypass.

The engine reports zero latency. The default right-hand spectrum range is 0 to -120 dBFS. Explicit saved current-format ranges remain unchanged. Removing the main graph's explanatory tooltip in the final baseline does not remove the selected-band editor.

Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.hpp` and `EditorMac.mm`. Parameter IDs and appended slope mappings must stay stable.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_eq
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
