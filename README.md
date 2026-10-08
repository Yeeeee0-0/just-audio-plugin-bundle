# JUST Audio Plugins

> **Windows preview branch:** native Windows CI builds and tests ten x64 VST3 plugins before publishing a prerelease. Check the [workflow result](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/actions/workflows/windows-preview.yml) and the exact asset manifest; a source branch alone is not a usable plugin. Real REAPER visual/audio acceptance is still pending. Start with [Windows setup and validation](docs/windows/START-HERE-zh.md) and [known gaps](docs/windows/KNOWN-DIFFERENCES-zh.md). Stable macOS 0.1.0 remains on [main](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/tree/main) and the [v0.1.0 release](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0).

JUST is a suite of ten audio effects in VST3 format, written in C++17 with native platform editors. Version 0.1.0 derives from frozen source snapshot `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. This hash records source provenance; it is not a commit identifier in the public repository's new history.

**Download:** [JUST 0.1.0 for macOS Apple silicon](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0). The installer lets you choose each of the ten plugins independently; all ten are selected by default. See [installation instructions](INSTALLING.md) and [release notes](RELEASE_NOTES_0.1.0.md).

| Effect | What it does | Source module |
| --- | --- | --- |
| EQ | Twelve-band minimum-phase EQ with dynamic bands, stereo/mid/side processing and a spectrum display | [eq](plugins/eq/README.md) |
| Reverb | Room, Hall and Plate spaces with frequency-dependent decay, wet EQ, modulation, ducking and freeze | [reverb](plugins/reverb/README.md) |
| Delay | Stereo, Dual and Ping-Pong delay with tempo sync, four taps, feedback filtering, modulation, ducking and freeze | [delay](plugins/delay/README.md) |
| Tremolo | Rhythmic level modulation with five waveforms, tempo sync and stereo phase control | [tremolo](plugins/tremolo/README.md) |
| Compressor | Clean and Punch compression with Peak/RMS/Blend detection and sidechain filtering | [compressor](plugins/compressor/README.md) |
| Limiter | Sample-peak and reconstructed-peak limiting, with a compatibility path for saved legacy states | [limiter](plugins/limiter/README.md) |
| Gate | Gate, Expand and Duck modes with envelope and sidechain controls | [gate](plugins/gate/README.md) |
| Flanger | Modulated short delay with feedback, manual phase, tempo sync and stereo phase control | [flanger](plugins/flanger/README.md) |
| Wider | Stereo field width, asymmetry and rotation, plus generated-side processing and mono checking | [fake_stereo](plugins/fake_stereo/README.md) |
| Distortion | Clean, Soft, Hard, Asym, Fold and Crush models with tone shaping and fixed 4x processing | [distortion](plugins/distortion/README.md) |

The shared framework supplies the VST3 processor/controller, parameter and state handling, presets, analysis and editor shell. Existing processor/controller identifiers and parameter IDs are compatibility contracts. Wider retains the module slug `fake_stereo` and bundle name `Just_fake_stereo.vst3` so that existing sessions can identify it.

## Platform status

| Platform | 0.1.0 status |
| --- | --- |
| macOS on Apple silicon (`arm64`) | Ten Release bundles; selective installer GUI checked. Only Apple silicon binaries are included. |
| Intel macOS / Universal | Build preset exists; no accepted binary or runtime validation in this release. |
| Windows x64 | Separate unsigned preview workflow and selectable installer; see each prerelease's actual evidence. User REAPER acceptance remains pending. |
| Linux | Core tests can be configured without VST3; no plugin release. |

The configured macOS deployment target is 11.0. That setting alone does not establish that the plugins have been tested on macOS 11.0. The frozen build uses development ad-hoc signatures; it does not establish Developer ID signing or notarization. Host compatibility and actual native input checks must be reported separately from offscreen or automated fixture results.

## Build and use

See [BUILDING.md](BUILDING.md) for pinned dependencies, macOS build commands and tests that do not start a GUI. Build outputs stay in the build directory; automatic links into plugin installation folders are disabled.

Use an arm64-capable VST3 host with the macOS arm64 build. The selectable installer installs for the current user at `~/Library/Audio/Plug-Ins/VST3`. The source tree does not include third-party commercial plugins or user audio projects.

Compressor and Gate currently preserve lookahead requests but process with zero actual lookahead/latency. Distortion uses fixed 4x processing and reports 32 samples of latency; other saved quality requests remain pending. Limiter's reconstructed-peak mode is not a claim of ITU/EBU compliance. See the module notes for these and other behavior details.

## Source layout

- `common/`: shared DSP contracts, parameters, state, VST3 integration and native UI.
- `plugins/<slug>/`: each effect's DSP, parameters, editor and tests.
- `cmake/` and `CMakePresets.json`: build configuration.
- `scripts/`: dependency bootstrap and validation helpers.
- `tests/`: shared framework tests.
- `third_party/dependency-lock.json`: exact VST3 SDK and submodule revisions.
- `third_party/licenses/`: third-party license notices.

## License and attribution

JUST project source is licensed under the [MIT License](LICENSE), copyright (c) 2026 Yee Huang. The included runtime artwork is authorized for redistribution under the same license; see [ARTWORK.md](ARTWORK.md). Third-party code retains its own notices.

The pinned Steinberg VST3 SDK 3.8.1 is recorded as MIT licensed. Its notice is retained in `third_party/licenses/VST3-SDK-MIT.txt` and copied into plugin bundles by the build. Preserve the applicable third-party notices when redistributing an authorized release.

See [the 0.1.0 release notes](RELEASE_NOTES_0.1.0.md) for the final baseline changes and verification boundaries.
