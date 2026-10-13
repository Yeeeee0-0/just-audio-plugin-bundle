# Changelog (draft)

<!-- Owner approval required before copying this draft to /CHANGELOG.md. -->

This draft follows the Keep a Changelog structure. Each heading uses the existing release tag and publication date, because several packages share plugin version 0.1.0. The entries summarize only the six original Release bodies; they do not add acceptance claims. Previous releases and assets remain available.

## [v0.1.0-installers.20261013-host-fix] - 2026-10-13

### Fixed

- Unselected third-party linked directories no longer falsely block installation; they are neither traversed nor modified. A selected JUST bundle that is itself a link is still refused.
- Same-volume transactions retain old bundles and allow safe installation and undo while the host remains running when files can be moved. Hard file locks safely refuse installation; close the locking plugin editor and retry.

### Unchanged

- Plugin version remains 0.1.0. All ten Windows DLLs, 151 payload files, UI, DSP, parameters and approved artwork are unchanged.
- The macOS arm64 PKG is unchanged and was not rebuilt.

### Known limits

- Windows remains an unsigned preview pending real-machine acceptance. Reopen the host or reload the plugin after installation; universal DAW hot swapping and gap-free audio are not promised.
- Eight directed checks passed on the final EXE, with seven unaffected earlier installer cases reused. Two invalid recovery-report cases were checked in native PowerShell clone fixtures. No full DSP/UI matrix was rerun.
- Restoration preserved 153 files, 71 directories and recorded identities; the scan-root modification time changed. Original observer false-failure records were retained and reconciled against raw evidence.
- The unchanged macOS PKG is arm64 only, unsigned and not notarized.

Source: [original Release body](../../releases/archive/v0.1.0-installers.20261013-host-fix.original.md).

## [v0.1.0-installers.20261012] - 2026-10-12

### Fixed

- Windows module menus no longer close instantly, selections no longer revert, and button repaint flicker is fixed.

### Changed

- Windows includes the approved installer icon and welcome/finish artwork.

### Unchanged

- Plugin version remains 0.1.0. DSP, parameter and identity contracts, the 33 ms refresh interval, and checked install/uninstall logic retain their prior behavior.
- The macOS Apple silicon PKG is the original published file, with unchanged bytes; it was not rebuilt or repackaged.
- Both installers include ten independently selectable effects, all selected by default.

### Known limits

- Windows final ten-DLL build and limited native UI checks passed, along with eight directed REAPER 7.41 checks. Installer cases covered isolated installation, file-lock refusal and uninstall content/directory restoration; earlier 51 installation and 40 uninstallation cases were reused for unchanged logic. The full matrix was not rerun.
- The new Setup's physical Cancel button was not retested; uninstall metadata did not all restore identically.
- macOS Installer GUI checks stopped at pre-install confirmation; actual PKG installation/upgrade and post-install scanning were not executed.
- Windows Setup and AMD64 plugins are unsigned. macOS is arm64 only: ad-hoc plugin signatures and an unsigned, unnotarized PKG. Intel/Universal binaries are not included, and macOS 11.0 is a deployment target rather than a verified runtime claim.

Source: [original Release body](../../releases/archive/v0.1.0-installers.20261012.original.md).

## [v0.1.0-windows-preview.17] - 2026-10-08

### Changed

- Added a Windows UI fix candidate that keeps scale/language/preset combo controls alive through selection notifications and updates them in place. Destructive modal button and Escape actions are queued after callback return, with stale-action checks.
- Added native grayscale repaint after direct native state/text setters for bypassed controls, including the reported Limiter checkbox; retained the earlier sibling-clipping fix.
- Expanded hidden-window native lifetime, common-control, modal, scale and concurrent DLL audio checks, including rejection of the pinned preview.13 EQ DLL by new lifetime assertions.

### Unchanged

- DSP, plugin IDs, parameters, state formats and Mac code are unchanged. The stable macOS main branch and v0.1.0 release are unchanged.

### Known limits

- This is an unsigned x64 prerelease candidate. REAPER playback/scale and bypass-color revalidation remained pending, including the reported REAPER 7.41 COMCTL32.dll access violation when changing EQ overlay scale during playback.
- Native Windows build, 69 CTest cases, ten official VST3 validator runs and 51 isolated installer checks passed. Hidden-window checks do not replace physical input or real REAPER acceptance; live themed animation/color and real screen-reader acceptance remain unverified.
- The candidate had not been tested in the user's Windows REAPER session. Prior preview.12/13 observations do not establish acceptance of these rebuilt files; GUI/input, high-DPI, presets, automation and audible DSP acceptance remain required.

Source: [original Release body](../../releases/archive/v0.1.0-windows-preview.17.original.md).

## [v0.1.0-windows-preview.13] - 2026-10-08

### Changed

- Added a single-file Windows clipping-style fix candidate based on preview.12, addressing the report that underlying controls paint over modal settings in REAPER 7.41 at 175% display scaling.

### Unchanged

- DSP, plugin IDs, parameters, state formats and Mac code are unchanged. The stable macOS main branch and v0.1.0 release are unchanged.

### Known limits

- This is an unsigned x64 prerelease candidate. Automated checks do not confirm that the reported visual defect is fixed; 175% DPI REAPER revalidation remained pending.
- Native Windows build, 69 CTest cases, ten official VST3 validator runs and 51 isolated installer checks passed.
- The candidate had not been tested in the user's Windows REAPER session. Prior preview.12 observations do not establish acceptance of these rebuilt files; GUI/input, high-DPI, presets, automation and audible DSP acceptance remain required. Real screen-reader acceptance remains pending.

Source: [original Release body](../../releases/archive/v0.1.0-windows-preview.13.original.md).

## [v0.1.0-windows-preview.12] - 2026-10-08

### Added

- Unsigned Windows x64 preview with ten VST3 bundles, a selectable installer, a portable package, public source/runtime artwork and workflow evidence.
- The installer verifies its payloads, backs up selected previous JUST bundles and restores them after installation failure; it does not edit user presets, REAPER configuration, licenses or projects.

### Known limits

- Native Windows build, 69 CTest cases, ten official VST3 validator runs and 51 isolated installer checks passed. No user Windows REAPER session had been tested.
- GUI/input, high-DPI, presets, automation, audible DSP and real screen-reader acceptance remained pending. This prerelease did not claim Mac/Windows visual parity or production readiness.
- Plugins are AMD64; the setup bootstrap may be a 32-bit NSIS executable. Plugin version is 0.1.0 and vendor is Yee Huang. The stable macOS main branch and v0.1.0 release are unchanged.

Source: [original Release body](../../releases/archive/v0.1.0-windows-preview.12.original.md).

## [v0.1.0] - 2026-10-08

### Added

- Ten native VST3 effects for macOS Apple silicon: EQ, Reverb, Delay, Tremolo, Compressor, Limiter, Gate, Flanger, Wider and Distortion.
- A selectable PKG installer for the current user, all ten effects selected by default, plus manual installation and reviewed source archives. Project code and authorized artwork are MIT licensed.

### Known limits

- The package uses the original 0.1.0 binaries. Clean public source rebuilt all ten arm64 Release targets and passed eight shared CLI contract tests plus 32 identity/resource audit regressions.
- Installer GUI checks covered selections, current-user scope and pre-install confirmation; the check stopped before installation.
- The PKG is unsigned and not notarized; plugins have ad-hoc development signatures. macOS 11.0 is the build deployment target rather than a verified runtime claim.
- Only macOS arm64 binaries are included. Intel/Universal builds and Windows binaries are unverified; Windows porting work is outside this stable release.

Source: [original Release body](../../releases/archive/v0.1.0.original.md).

[v0.1.0-installers.20261013-host-fix]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261013-host-fix
[v0.1.0-installers.20261012]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-installers.20261012
[v0.1.0-windows-preview.17]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-windows-preview.17
[v0.1.0-windows-preview.13]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-windows-preview.13
[v0.1.0-windows-preview.12]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0-windows-preview.12
[v0.1.0]: https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/tag/v0.1.0
