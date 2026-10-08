# JUST Wider

A stereo-field processor with Width, Asymmetry and Rotation controls, plus generated-side processing.

- L/R or M/S input interpretation, channel inversion and L/R swap.
- Generated Width and Existing Side controls.
- Low Protect, optional generated-side High Cut, Tight/Diffuse character and mix.
- Mono Check and input/output gain.

The product display name is **JUST Wider**. Its internal slug remains `fake_stereo`, its target is `Just_fake_stereo`, and its bundle is `Just_fake_stereo.vst3`. Preserve these identities for session compatibility. The module accepts mono/stereo configurations defined by its bus policy and reports zero latency.

Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_fake_stereo
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
