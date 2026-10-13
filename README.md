<!-- Keep README.md and README.zh-CN.md equivalent when editing either file. -->
<!-- TODO(owner): Choose the header image after reviewing docs/github-presentation/HEADER-PREVIEW.md. -->
**English** | [简体中文](README.zh-CN.md)

# JUST Audio Plugins

Ten free, open-source VST3 effects — EQ, reverb, delay, dynamics, modulation and distortion — built in C++17 with native editors.

![Version 0.1.0](https://img.shields.io/badge/version-0.1.0-blue) ![VST3](https://img.shields.io/badge/format-VST3-blue) ![MIT](https://img.shields.io/badge/license-MIT-green) ![macOS Apple silicon](https://img.shields.io/badge/macOS-Apple_silicon-lightgrey) ![Windows x64](https://img.shields.io/badge/Windows-x64-blue)

[Explore the ten plugins](#plugins) · [Downloads](#downloads) · [Installation](#installation)

**Download 0.1.0:** [Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe) (unsigned) · [macOS Apple silicon PKG](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg) (arm64 only; unsigned, not notarized).

## About

JUST is a personal project shared as open source: ten independent audio effects, all released as the official **0.1.0** version. They share a custom framework for VST3 integration, parameters, state, presets, analysis and the editor shell. The editors use native platform interfaces, without JUCE or VSTGUI. Both installers offer ten independent choices, all selected by default.

<!-- TODO(owner): 可在这里写一两句做这个项目的起因 -->

## Plugins

| Icon | Effect | What it does |
| --- | --- | --- |
| <img src="common/ui/resources/icons/eq.png" alt="EQ" width="40"> | [EQ](plugins/eq/README.md) | Twelve-band minimum-phase EQ with dynamic bands, stereo/mid/side processing and a spectrum display |
| <img src="common/ui/resources/icons/reverb.png" alt="Reverb" width="40"> | [Reverb](plugins/reverb/README.md) | Room, Hall and Plate spaces with frequency-dependent decay, wet EQ, modulation, ducking and freeze |
| <img src="common/ui/resources/icons/delay.png" alt="Delay" width="40"> | [Delay](plugins/delay/README.md) | Stereo, Dual and Ping-Pong delay with tempo sync, four taps, feedback filtering, modulation, ducking and freeze |
| <img src="common/ui/resources/icons/tremolo.png" alt="Tremolo" width="40"> | [Tremolo](plugins/tremolo/README.md) | Rhythmic level modulation with five waveforms, tempo sync and stereo phase control |
| <img src="common/ui/resources/icons/compressor.png" alt="Compressor" width="40"> | [Compressor](plugins/compressor/README.md) | Clean and Punch compression with Peak/RMS/Blend detection and sidechain filtering |
| <img src="common/ui/resources/icons/limiter.png" alt="Limiter" width="40"> | [Limiter](plugins/limiter/README.md) | Sample-peak and reconstructed-peak limiting, with a compatibility path for saved legacy states |
| <img src="common/ui/resources/icons/gate.png" alt="Gate" width="40"> | [Gate](plugins/gate/README.md) | Gate, Expand and Duck modes with envelope and sidechain controls |
| <img src="common/ui/resources/icons/flanger.png" alt="Flanger" width="40"> | [Flanger](plugins/flanger/README.md) | Modulated short delay with feedback, manual phase, tempo sync and stereo phase control |
| <img src="common/ui/resources/icons/fake_stereo.png" alt="Wider" width="40"> | [Wider¹](plugins/fake_stereo/README.md) | Stereo field width, asymmetry and rotation, plus generated-side processing and mono checking |
| <img src="common/ui/resources/icons/distortion.png" alt="Distortion" width="40"> | [Distortion](plugins/distortion/README.md) | Clean, Soft, Hard, Asym, Fold and Crush models with tone shaping and fixed 4x processing |

¹ Wider retains the internal name `fake_stereo` and bundle name `Just_fake_stereo.vst3` so existing projects can identify it.

## Downloads

### Windows x64

[Download JUST 0.1.0 Windows x64 Setup](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261013-host-fix/JUST-0.1.0-Windows-x64-preview-setup.exe) — **unsigned official 0.1.0 release**; compatibility across all DAW hosts has not been verified.

### macOS Apple silicon

[Download the macOS PKG](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-installers.20261012/JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg) — **arm64 only, unsigned and not notarized**. This is the unchanged package also published in `v0.1.0`.

<details><summary>Checksums / 校验值</summary>

| Original asset / 原附件名 | Bytes / 字节 | SHA-256 |
| --- | ---: | --- |
| `JUST-0.1.0-Windows-x64-preview-setup.exe` | 4,031,050 | `22946543862a919f32621aabee3c00d294d97f6e11cb8e6c4e525a92373c1040` |
| `JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg` | 11,254,806 | `c1359f53d5307ff994c4d8fd1e8930e0f8a5996c3140dc31a09fea69cc4f56cc` |

</details>

All older versions and their assets remain on the [Releases page](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases).

## Installation

### Windows x64

1. Save your projects and run Setup with administrator permission. Choose the plugins you want; all ten are selected by default.
2. Setup installs into the system 64-bit Common Files `VST3` folder. The host may stay open if the selected plugin files can be moved safely.
3. If an open plugin editor locks files, Setup safely refuses and retains the existing contents. Close that editor and retry.
4. Reopen the host or reload the plugin after installation. Hot swapping and uninterrupted audio are not guaranteed in every DAW.
5. Keep the install report and backups. To undo, use the `JUST-uninstall.exe` recorded in that installation's report.

Logs are in `%ProgramData%\JUST\Windows-preview-logs`; backups are in `%ProgramData%\JUST\Windows-preview-backups`. See the [full Windows installation and removal guide](docs/windows/START-HERE-zh.md).

### macOS Apple silicon

1. Save your projects and close audio hosts.
2. Open the PKG in macOS Installer. On Installation Type / Customize, choose the plugins you want; all ten are selected by default.
3. Review the selection and install. Only selected plugins go to `~/Library/Audio/Plug-Ins/VST3` for the current user. Deselecting a plugin skips it and keeps any existing copy.
4. After installation, reopen a native Apple silicon VST3 host and rescan.
5. To uninstall, close hosts and move only the exact bundles listed in [INSTALLING.md](INSTALLING.md) to Trash. Keep user presets at `~/Library/Application Support/JUST/Presets/v1/`; do not delete the whole VST3 folder.

If macOS blocks the package or plugin, obtain a Developer ID signed and notarized release while keeping system security protections enabled. See [INSTALLING.md](INSTALLING.md) for the full instructions and removal details.

## Platform support and known limits

| Platform | Status |
| --- | --- |
| macOS Apple silicon (`arm64`) | Ten Release bundles. PKG unsigned and not notarized; plugins use development ad-hoc signatures, not Developer ID. Deployment target is macOS 11.0, which has not been runtime-tested. |
| Intel macOS / Universal | Build presets exist; no binaries are provided and runtime support is unverified. |
| Windows x64 | Official 0.1.0 release, unsigned. The Setup launcher is x86 and contains ten AMD64 plugins; no ARM64. Eight installer/REAPER checks completed; compatibility across all DAW hosts has not been verified. |
| Linux | Core tests only, without VST3; no plugin release. |

- Compressor and Gate save lookahead settings but process with zero actual lookahead and zero latency.
- Distortion uses fixed 4x processing and reports 32 samples of latency; other saved quality settings are not implemented yet.
- Limiter's reconstructed-peak mode does not claim ITU/EBU compliance.
- Existing processor/controller UIDs and parameter IDs are compatibility contracts and must remain unchanged.
- Windows Setup and plugins are unsigned.
- Compatibility across all DAW hosts has not been verified; support in every host is not guaranteed.

See the [current release record](docs/releases/0.1.0-installers-20261013-host-fix.md) for the detailed verification scope.

## Build from source

Run from the repository root. These commands are copied from [BUILDING.md](BUILDING.md); building does not automatically install plugins.

### macOS arm64

```sh
python3 scripts/bootstrap-sdk.py
cmake --preset mac-arm64-clt -DSMTG_CREATE_PLUGIN_LINK=OFF
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_eq Just_reverb Just_delay Just_tremolo Just_compressor Just_limiter Just_gate Just_flanger Just_fake_stereo Just_distortion
```

### Windows x64

```powershell
python scripts/bootstrap-sdk.py
cmake --preset windows-x64
cmake --build --preset windows-x64 --parallel 2 --target Just_eq Just_reverb Just_delay Just_tremolo Just_compressor Just_limiter Just_gate Just_flanger Just_fake_stereo Just_distortion
```

See [BUILDING.md](BUILDING.md) for toolchains, pinned dependencies and testing. Steinberg VST3 SDK 3.8.1 (`v3.8.1_build_84`) is pinned in [the dependency lock](third_party/dependency-lock.json).

## Project layout

```text
common/                   Shared DSP contracts, parameters, state, VST3 and native UI
plugins/<slug>/           Each effect's DSP, parameters, editor and tests
cmake/, CMakePresets.json  Build configuration
scripts/                  Dependency bootstrap and validation scripts
tests/                    Shared framework tests
installers/               Installer source and packaging recipes
third_party/              Dependency lock and license notices
docs/                     Release records and platform guides
```

## Developer notes

- Keep processor/controller UIDs and parameter IDs stable.
- Keep Wider's `fake_stereo` slug and `Just_fake_stereo.vst3` bundle name unchanged.
- After changing code, run the relevant DSP, state and VST3 tests before considering a replacement release file. Installation and host acceptance require separate checks.

## License and attribution

JUST project source is licensed under the [MIT License](LICENSE), copyright (c) 2026 Yee Huang. The included runtime artwork is authorized by the owner for redistribution under the same license; see [ARTWORK.md](ARTWORK.md). Third-party code retains its own notices.

The pinned Steinberg VST3 SDK 3.8.1 is recorded as MIT licensed. Its notice is retained in [VST3-SDK-MIT.txt](third_party/licenses/VST3-SDK-MIT.txt) and copied into plugin bundles by the build. Preserve the applicable notices when redistributing an authorized release; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
