# JUST Limiter

A limiter with sample-peak and reconstructed-peak processing and an explicit algorithm-version compatibility path.

- Input/output gain, ceiling, release and transient controls.
- Lookahead and comparison trim.
- A loudness guide used as a display reference.
- A current Transparent insurance algorithm and a Legacy compatibility path for older saved states.

The saved parameter labels include Live Sample Peak and Mix TP prototype. Reconstructed-peak processing is not a claim of ITU/EBU compliance. Read the host-reported latency instead of assuming that a selected lookahead value equals total processing latency. The loudness guide does not normalize the audio automatically.

Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp`, `TransparentDsp.hpp`, `LegacyDsp.hpp` and `EditorMac.mm`.

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_limiter
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
