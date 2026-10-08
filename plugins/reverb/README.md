# JUST Reverb

An algorithmic reverb with Room, Hall and Plate styles.

- Decay, size, pre-delay, early/late balance and diffusion.
- Separate low/mid/high decay controls with adjustable crossover frequencies.
- Wet high-pass/high-cut filtering, two bell bands, modulation, stereo width and wet level.
- Ducking, freeze and host-synced pre-delay.
- Simple macro controls and detailed advanced controls.

The 14 factory presets in `Presets.hpp` use original JUST tuning. Third-party manuals informed categories and use cases; the bank does not copy vendor preset data, branded preset names, proprietary algorithms or impulse responses, and does not claim to reproduce another product's sound. Complete preset states include hidden targets and the seed.

Freeze can sustain the reverberant signal. Saved state contains parameter targets and the seed, not the currently sounding delay buffers; render a frozen tail if it must be preserved as audio.

Implementation: `Module.cpp`, `Parameters.hpp`, `ReverbEngine.cpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_reverb
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
