# JUST Limiter

<img src="../../common/ui/resources/icons/limiter.png" alt="JUST Limiter icon" width="64" height="64">

A limiter with sample-peak and reconstructed-peak processing and an explicit algorithm-version compatibility path.

## Features

- Input/output gain, ceiling, release and transient controls.
- Lookahead and comparison trim.
- A loudness guide used as a display reference.
- A current Transparent insurance algorithm and a Legacy compatibility path for older saved states.

## Latency and limitations

The saved parameter labels include Live Sample Peak and Mix TP prototype. Reconstructed-peak processing is not a claim of ITU/EBU compliance. Read the host-reported latency instead of assuming that a selected lookahead value equals total processing latency. The loudness guide does not normalize the audio automatically.

## Implementation

Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp`, `TransparentDsp.hpp`, `LegacyDsp.hpp` and `EditorMac.mm`.

## Build this plugin

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_limiter
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.

[← Back to JUST Audio Plugins](../../README.md)
