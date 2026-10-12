# JUST Audio Plugins

JUST is a suite of ten audio effects in VST3 format, written in C++17 with native platform editors. Both installers report version **0.1.0** and offer ten independent choices, selected by default.

**Recommended downloads:** [Windows x64 preview and macOS Apple silicon installers](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261012). This prerelease pairs the updated Windows UI installer with the unchanged macOS arm64 package from [v0.1.0](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0). Older releases and assets remain available.

- [Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-0.1.0-Windows-x64-preview-UI-fix-setup.exe) — unsigned preview; see [Windows installation and removal](docs/windows/START-HERE-zh.md).
- [macOS arm64 PKG](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg) — Apple silicon only, unsigned and not notarized; see [macOS installation and removal](INSTALLING.md).

See the [release record and SHA-256 values](docs/releases/0.1.0-installers-20261012.md) for exact bytes, source provenance and verification limits. The current tag contains the final Windows source; the macOS package retains the original source at [`f2a7105`](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/tree/f2a710510415395e13e548400143604c455df8fe). The two packages were not rebuilt together.

## 中文

JUST 包含十款独立 VST3 音频效果器，版本均为 **0.1.0**。上方是本次推荐下载：Windows x64 预览安装器修复了菜单关闭、选项回退和按钮闪烁，并使用已批准的安装器图标及欢迎/完成页；macOS 安装包沿用原已交付文件，仅包含 Apple silicon **arm64**，不包含 Intel 或 Universal 二进制。两个安装器默认十款全选，可分别取消勾选。

Windows 安装器和插件未签名；macOS PKG 未签名、未公证，插件使用开发 ad-hoc 签名。下载后请核对[发布记录中的 SHA-256](docs/releases/0.1.0-installers-20261012.md)，保存工程并正常关闭音频宿主，再按 [Windows 说明](docs/windows/START-HERE-zh.md)或 [macOS 说明](INSTALLING.md)安装、扫描和移除。Windows 本次通过十款有限范围原生 UI 检查及 REAPER 8 项定向检查；macOS Installer GUI 检查停在实际安装前确认页。上述结果不代表所有 DAW、系统或完整音频矩阵均已验证。

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
| Windows x64 | Unsigned selectable preview installer; ten bounded native UI scopes and eight directed REAPER checks passed for this package. Full host/audio acceptance is outside those checks. |
| Linux | Core tests can be configured without VST3; no plugin release. |

The configured macOS deployment target is 11.0. That setting alone does not establish that the plugins have been tested on macOS 11.0. The frozen build uses development ad-hoc signatures; it does not establish Developer ID signing or notarization. Windows and macOS have distinct verification records; see the [current release record](docs/releases/0.1.0-installers-20261012.md).

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
