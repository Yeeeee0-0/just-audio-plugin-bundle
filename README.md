# JUST Audio Plugins

JUST is a suite of ten VST3 audio effects, written in C++17 with native platform editors. Both installers report version **0.1.0** and offer ten independent choices, selected by default.

**Recommended downloads:** [Windows installer host fix](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix). This prerelease updates only the Windows installation mechanism; the ten plugin DLLs, UI, DSP and approved artwork are unchanged. Older releases and assets remain available.

- [Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe) — unsigned preview; [installation and removal](docs/windows/START-HERE-zh.md).
- [macOS arm64 PKG](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg) — unchanged Apple silicon package, unsigned and not notarized; [installation and removal](INSTALLING.md).

Save projects. Windows hosts may remain running when selected bundles can be moved safely. If an open plugin editor locks files, close that editor and retry; the installer safely refuses rather than terminating the host. Reopen the host or reload the plugin to use the installed copy. See the [release record and SHA-256 values](docs/releases/0.1.0-installers-20261013-host-fix.md) for exact bytes and bounded verification. The new tag contains matching Windows installer source; macOS keeps its [original source](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/tree/f2a710510415395e13e548400143604c455df8fe).

## 中文

JUST 包含十款独立 VST3 效果器，版本均为 **0.1.0**，默认全选，可分别取消勾选。推荐下载上方新版 Windows x64 安装器：第三方未选中的链接目录不再误挡，宿主运行时可在文件允许安全移动的条件下安装和撤销。插件 DLL、UI、DSP 与批准美术没有改动。旧版本仍保留。

先保存工程；若插件编辑窗口锁住文件，安装器会安全拒绝，关闭该插件编辑窗口后重试。安装完成后正常重新打开宿主或重新加载插件使用新副本，不承诺所有 DAW 无条件热替换。Windows 仍为未签名预览，本轮八项安装器/REAPER 定向检查完成，未重跑完整插件矩阵。macOS 沿用原包，只包含 Apple silicon arm64，PKG 未签名、未公证；详见[发布记录](docs/releases/0.1.0-installers-20261013-host-fix.md)、[Windows 说明](docs/windows/START-HERE-zh.md)和 [macOS 说明](INSTALLING.md)。

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
| Windows x64 | Unsigned selectable preview; eight directed installer/REAPER branches checked. DLL/UI/DSP unchanged from the prior UI-fix release; full host/audio acceptance remains pending. |
| Linux | Core tests can be configured without VST3; no plugin release. |

The configured macOS deployment target is 11.0. That setting alone does not establish that the plugins have been tested on macOS 11.0. The frozen build uses development ad-hoc signatures; it does not establish Developer ID signing or notarization. Windows and macOS have distinct verification records; see the [current release record](docs/releases/0.1.0-installers-20261013-host-fix.md).

## Build and use

See [BUILDING.md](BUILDING.md) for pinned dependencies and platform build commands. Build outputs stay in the build directory; automatic links into plugin installation folders are disabled.

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
