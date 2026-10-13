# JUST Flanger

<img src="../../common/ui/resources/icons/flanger.png" alt="JUST Flanger icon" width="64" height="64">

A short-delay modulation effect with LFO and Manual modes.

## Features

- Base delay, modulation depth and rate.
- Sine/Triangle waveforms, rhythmic sync divisions and Free/Transport/On Play timing.
- Phase and stereo phase controls.
- Positive or negative feedback with a feedback low-pass filter.
- Wet-polarity inversion, mix, input/output gain and bypass.

## Latency and limitations

Manual mode uses the phase control directly; modulation depth and rate are not editable in that mode.

## Implementation

Implementation: `Module.cpp`, `Parameters.hpp`, `FlangerEngine.hpp` and `EditorMac.mm`.

## Build this plugin

Build from the repository root after configuration:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_flanger
```

See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.

[← Back to JUST Audio Plugins](../../README.md)
