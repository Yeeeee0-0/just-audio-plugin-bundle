# JUST Distortion

A distortion effect with Clean, Soft, Hard, Asym, Fold and Crush models.

- Drive, boost, bias and model-specific shape controls.
- Pre high-pass/tilt and post bass/treble/low-pass shaping.
- Crush bit depth, hold rate, optional anti-aliasing and TPDF dither.
- Makeup, drive compensation, match gain and wet/dry mix.
- Fixed 4x nonlinear processing with 32 samples of reported latency.

New instances select Soft. Clean remains the same saved enumeration value, so old states retain their model selection. The saved Quality enumeration contains 1x/2x/4x/8x requests, but the shipping factory prepares only 4x. Other host-state quality requests remain pending, and the preset validator rejects unsupported quality requests. Dynamic quality/PDC switching is not implemented.

Hard processing uses first-order antiderivative antialiasing (ADAA) from the exact integral of this module's cubic shoulder, with a half-sample average and a project-specific five-tap correction. The method references are [Bilbao et al. (2017)](https://doi.org/10.1109/LSP.2017.2675541) and [Holters (2019), section 2](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_4.pdf). The project records its implementation as original; these papers are references, not bundled source or assets.

Clean skips wet tone shaping and nonlinearity. Dry, Clean, Crush and settled bypass paths are aligned to the reported 32-sample latency. Crush uses a base-rate quantizer and hold-rate process. Drive Compensation is a fixed approximation, not adaptive loudness matching. No all-input alias-free or subjective sound-quality guarantee is made.

Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_distortion
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
