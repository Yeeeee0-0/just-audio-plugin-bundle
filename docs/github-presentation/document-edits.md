# T5 / T6 document edits

These edits implement the uncontested documentation corrections and the plugin README layout from §7 of the GitHub presentation plan. The source baseline is `de16de3f7e3b5582a7bbe374a27d7ca4105d04ef`. No build, installer or plugin source files are changed.

## T5: platform-status wording

The same outdated sentence occurred in all ten plugin READMEs. The replacement links to the suite platform status and makes no new platform claim.

**Before (each of the ten files):**

Only the macOS arm64 binary set is covered by this release.

**After (each of the ten files):**

See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.

## T6: plugin README layout

Each plugin README now uses its existing 64 × 64 displayed icon, followed by its existing one-sentence introduction, Features, Latency and limitations, Implementation, Build this plugin, and a return link. Original technical paragraphs, feature bullets and the individual build command are retained verbatim. No latency is inferred for modules without an explicit latency statement.

## T5: current release pointer

`RELEASE_NOTES_0.1.0.md` gets the bilingual current Windows host-fix pointer at the top. Its entire prior contents are retained byte for byte after the new notice, including the historical 2026-10-12 recommendation and its verification limits.

**Before:** the document started with `# JUST 0.1.0` and the 2026-10-12 recommendation.

**After:**

```markdown
> **当前推荐 / Current recommendation**
> 当前推荐的 Windows x64 安装包见 [2026-10-13 host-fix 预发布](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix)。下方保留 2026-10-12 的发布记录，macOS 安装包保持不变。
> The current recommended Windows x64 installer is in the [2026-10-13 host-fix prerelease](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix). The 2026-10-12 release record is retained below, and the macOS package is unchanged.
```

## Complete before/after diffs

Each diff below records the exact removed and inserted lines. Unchanged technical content is included as context.

### `plugins/eq/README.md`

```diff
--- before/plugins/eq/README.md
+++ after/plugins/eq/README.md
@@ -1,15 +1,25 @@
 # JUST EQ
 
+<img src="../../common/ui/resources/icons/eq.png" alt="JUST EQ icon" width="64" height="64">
+
 A twelve-band minimum-phase equalizer with stereo, mid and side band targets.
+
+## Features
 
 - Bell, low shelf, high shelf, high-pass, low-pass and notch shapes.
 - Band frequency, gain, Q and slope controls; dynamic controls with Peak/RMS detection and internal/external sources.
 - Native spectrum editor with selected-band controls and band audition.
 - Input/output gain and bypass.
 
+## Latency and limitations
+
 The engine reports zero latency. The default right-hand spectrum range is 0 to -120 dBFS. Explicit saved current-format ranges remain unchanged. Removing the main graph's explanatory tooltip in the final baseline does not remove the selected-band editor.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.hpp` and `EditorMac.mm`. Parameter IDs and appended slope mappings must stay stable.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -17,4 +27,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_eq
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/reverb/README.md`

```diff
--- before/plugins/reverb/README.md
+++ after/plugins/reverb/README.md
@@ -1,6 +1,10 @@
 # JUST Reverb
 
+<img src="../../common/ui/resources/icons/reverb.png" alt="JUST Reverb icon" width="64" height="64">
+
 An algorithmic reverb with Room, Hall and Plate styles.
+
+## Features
 
 - Decay, size, pre-delay, early/late balance and diffusion.
 - Separate low/mid/high decay controls with adjustable crossover frequencies.
@@ -8,11 +12,17 @@
 - Ducking, freeze and host-synced pre-delay.
 - Simple macro controls and detailed advanced controls.
 
+## Latency and limitations
+
 The 14 factory presets in `Presets.hpp` use original JUST tuning. Third-party manuals informed categories and use cases; the bank does not copy vendor preset data, branded preset names, proprietary algorithms or impulse responses, and does not claim to reproduce another product's sound. Complete preset states include hidden targets and the seed.
 
 Freeze can sustain the reverberant signal. Saved state contains parameter targets and the seed, not the currently sounding delay buffers; render a frozen tail if it must be preserved as audio.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `ReverbEngine.cpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -20,4 +30,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_reverb
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/delay/README.md`

```diff
--- before/plugins/delay/README.md
+++ after/plugins/delay/README.md
@@ -1,6 +1,10 @@
 # JUST Delay
 
+<img src="../../common/ui/resources/icons/delay.png" alt="JUST Delay icon" width="64" height="64">
+
 A delay with Stereo, Dual and Ping-Pong routing.
+
+## Features
 
 - Independent left/right delay times and sync divisions, plus a fallback tempo.
 - Four tap controls with level, pan and enable state; additional time controls for taps 3 and 4.
@@ -8,9 +12,15 @@
 - Wet drive/tilt, modulation, ducking, wet width and wet level.
 - Repitch and Smooth time-change modes.
 
+## Latency and limitations
+
 The Simple Time control can represent a custom state when independent timing settings differ. Advanced values remain part of the saved sound state. Freeze can sustain repeats.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `DelayEngine.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -18,4 +28,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_delay
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/tremolo/README.md`

```diff
--- before/plugins/tremolo/README.md
+++ after/plugins/tremolo/README.md
@@ -1,6 +1,10 @@
 # JUST Tremolo
 
+<img src="../../common/ui/resources/icons/tremolo.png" alt="JUST Tremolo icon" width="64" height="64">
+
 A level-modulation effect with Sine, Triangle, Square, Saw Up and Saw Down waveforms.
+
+## Features
 
 - Depth, mix, free rate and host-synced rhythmic divisions.
 - Phase and stereo phase offset.
@@ -8,9 +12,15 @@
 - Free, Transport and On Play timing modes.
 - Input/output gain and bypass.
 
+## Latency and limitations
+
 Transport timing requires Sync; the implementation reports a Free fallback when Transport is selected without Sync.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -18,4 +28,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_tremolo
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/compressor/README.md`

```diff
--- before/plugins/compressor/README.md
+++ after/plugins/compressor/README.md
@@ -1,14 +1,24 @@
 # JUST Compressor
 
+<img src="../../common/ui/resources/icons/compressor.png" alt="JUST Compressor icon" width="64" height="64">
+
 A compressor with Clean and Punch styles and Peak, RMS or blended detection.
+
+## Features
 
 - Threshold, ratio, attack, release, knee and gain-reduction range.
 - Input gain, makeup gain, parallel mix and stereo link.
 - Internal/external sidechain selection, sidechain gain and optional high-pass/low-pass filters.
 
+## Latency and limitations
+
 **Lookahead is pending in this release.** The parameter and maximum-lookahead configuration can retain saved requests, but the engine uses zero actual lookahead and reports zero latency. A stored nonzero request must not be presented as an active pre-delay.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.cpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -16,4 +26,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_compressor
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/limiter/README.md`

```diff
--- before/plugins/limiter/README.md
+++ after/plugins/limiter/README.md
@@ -1,15 +1,25 @@
 # JUST Limiter
 
+<img src="../../common/ui/resources/icons/limiter.png" alt="JUST Limiter icon" width="64" height="64">
+
 A limiter with sample-peak and reconstructed-peak processing and an explicit algorithm-version compatibility path.
+
+## Features
 
 - Input/output gain, ceiling, release and transient controls.
 - Lookahead and comparison trim.
 - A loudness guide used as a display reference.
 - A current Transparent insurance algorithm and a Legacy compatibility path for older saved states.
 
+## Latency and limitations
+
 The saved parameter labels include Live Sample Peak and Mix TP prototype. Reconstructed-peak processing is not a claim of ITU/EBU compliance. Read the host-reported latency instead of assuming that a selected lookahead value equals total processing latency. The loudness guide does not normalize the audio automatically.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp`, `TransparentDsp.hpp`, `LegacyDsp.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -17,4 +27,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_limiter
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/gate/README.md`

```diff
--- before/plugins/gate/README.md
+++ after/plugins/gate/README.md
@@ -1,15 +1,25 @@
 # JUST Gate
 
+<img src="../../common/ui/resources/icons/gate.png" alt="JUST Gate icon" width="64" height="64">
+
 A dynamics effect with Gate, Expand and Duck modes.
+
+## Features
 
 - Threshold and attenuation range; ratio/knee for expansion.
 - Attack, hold, release and hysteresis where applicable to the selected mode.
 - Peak/RMS detection, internal/external sidechain and optional sidechain high-pass/low-pass filtering.
 - Input/output gain and bypass.
 
+## Latency and limitations
+
 **Lookahead is pending in this release.** Nonzero lookahead/configuration requests are recorded as pending. The current engine reports zero latency and does not provide an active lookahead delay.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `GateEngine.cpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -17,4 +27,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_gate
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/flanger/README.md`

```diff
--- before/plugins/flanger/README.md
+++ after/plugins/flanger/README.md
@@ -1,6 +1,10 @@
 # JUST Flanger
 
+<img src="../../common/ui/resources/icons/flanger.png" alt="JUST Flanger icon" width="64" height="64">
+
 A short-delay modulation effect with LFO and Manual modes.
+
+## Features
 
 - Base delay, modulation depth and rate.
 - Sine/Triangle waveforms, rhythmic sync divisions and Free/Transport/On Play timing.
@@ -8,9 +12,15 @@
 - Positive or negative feedback with a feedback low-pass filter.
 - Wet-polarity inversion, mix, input/output gain and bypass.
 
+## Latency and limitations
+
 Manual mode uses the phase control directly; modulation depth and rate are not editable in that mode.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `FlangerEngine.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -18,4 +28,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_flanger
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/fake_stereo/README.md`

```diff
--- before/plugins/fake_stereo/README.md
+++ after/plugins/fake_stereo/README.md
@@ -1,15 +1,25 @@
 # JUST Wider
 
+<img src="../../common/ui/resources/icons/fake_stereo.png" alt="JUST Wider icon" width="64" height="64">
+
 A stereo-field processor with Width, Asymmetry and Rotation controls, plus generated-side processing.
+
+## Features
 
 - L/R or M/S input interpretation, channel inversion and L/R swap.
 - Generated Width and Existing Side controls.
 - Low Protect, optional generated-side High Cut, Tight/Diffuse character and mix.
 - Mono Check and input/output gain.
 
+## Latency and limitations
+
 The product display name is **JUST Wider**. Its internal slug remains `fake_stereo`, its target is `Just_fake_stereo`, and its bundle is `Just_fake_stereo.vst3`. Preserve these identities for session compatibility. The module accepts mono/stereo configurations defined by its bus policy and reports zero latency.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Dsp.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -17,4 +27,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_fake_stereo
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `plugins/distortion/README.md`

```diff
--- before/plugins/distortion/README.md
+++ after/plugins/distortion/README.md
@@ -1,6 +1,10 @@
 # JUST Distortion
 
+<img src="../../common/ui/resources/icons/distortion.png" alt="JUST Distortion icon" width="64" height="64">
+
 A distortion effect with Clean, Soft, Hard, Asym, Fold and Crush models.
+
+## Features
 
 - Drive, boost, bias and model-specific shape controls.
 - Pre high-pass/tilt and post bass/treble/low-pass shaping.
@@ -8,13 +12,19 @@
 - Makeup, drive compensation, match gain and wet/dry mix.
 - Fixed 4x nonlinear processing with 32 samples of reported latency.
 
+## Latency and limitations
+
 New instances select Soft. Clean remains the same saved enumeration value, so old states retain their model selection. The saved Quality enumeration contains 1x/2x/4x/8x requests, but the shipping factory prepares only 4x. Other host-state quality requests remain pending, and the preset validator rejects unsupported quality requests. Dynamic quality/PDC switching is not implemented.
 
 Hard processing uses first-order antiderivative antialiasing (ADAA) from the exact integral of this module's cubic shoulder, with a half-sample average and a project-specific five-tap correction. The method references are [Bilbao et al. (2017)](https://doi.org/10.1109/LSP.2017.2675541) and [Holters (2019), section 2](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_4.pdf). The project records its implementation as original; these papers are references, not bundled source or assets.
 
 Clean skips wet tone shaping and nonlinearity. Dry, Clean, Crush and settled bypass paths are aligned to the reported 32-sample latency. Crush uses a base-rate quantizer and hold-rate process. Drive Compensation is a fixed approximation, not adaptive loudness matching. No all-input alias-free or subjective sound-quality guarantee is made.
 
+## Implementation
+
 Implementation: `Module.cpp`, `Parameters.hpp`, `Engine.hpp` and `EditorMac.mm`.
+
+## Build this plugin
 
 Build from the repository root after configuration:
 
@@ -22,4 +32,6 @@
 cmake --build --preset mac-arm64-clt --parallel 2 --target Just_distortion
 ```
 
-See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. Only the macOS arm64 binary set is covered by this release.
+See [BUILDING.md](../../BUILDING.md) for dependencies and test boundaries, and the [suite README](../../README.md) for platform and license information. See [platform support and known limits](../../README.md#platform-support-and-known-limits) for the current platform status.
+
+[← Back to JUST Audio Plugins](../../README.md)
```

### `RELEASE_NOTES_0.1.0.md`

```diff
--- before/RELEASE_NOTES_0.1.0.md
+++ after/RELEASE_NOTES_0.1.0.md
@@ -1,3 +1,7 @@
+> **当前推荐 / Current recommendation**
+> 当前推荐的 Windows x64 安装包见 [2026-10-13 host-fix 预发布](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix)。下方保留 2026-10-12 的发布记录，macOS 安装包保持不变。
+> The current recommended Windows x64 installer is in the [2026-10-13 host-fix prerelease](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix). The 2026-10-12 release record is retained below, and the macOS package is unchanged.
+
 # JUST 0.1.0
 
 The recommended [2026-10-12 installer prerelease](https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261012) contains the updated Windows x64 preview Setup and the unchanged macOS Apple silicon installer from `v0.1.0`. Product version remains **0.1.0**. Old releases and assets are retained.
```

## Pending owner confirmation

`BUILDING.md` is unchanged. Its opening source/tag wording is deferred under §9.6. The draft PR must ask the owner to confirm the plugin-source and installer-source wording before that sentence is edited.
