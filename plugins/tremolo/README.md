# JUST Tremolo

<img src="../../common/ui/resources/icons/tremolo.png" alt="JUST Tremolo icon" width="64" height="64">

A level-modulation effect with Sine, Triangle, Square, Saw Up and Saw Down waveforms.

## Features

- Depth, mix, free rate and host-synced rhythmic divisions.
- Phase and stereo phase offset.
- Square-wave duty and edge controls for square/saw shapes.
- Free, Transport and On Play timing modes.
- Input/output gain and bypass.

## Latency and limitations

Transport timing requires Sync; the implementation reports a Free fallback when Transport is selected without Sync.

## Implementation

Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp` and `EditorMac.mm`.

## Build this plugin

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_tremolo
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.

[← Back to JUST Audio Plugins](../../README.md)
