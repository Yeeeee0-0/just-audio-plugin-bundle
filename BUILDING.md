# Building JUST 0.1.0

These instructions describe the public source preparation derived from frozen macOS arm64 snapshot `b99abd3be4ad89b21e3a0c3caf1f12904503f168`. That provenance hash is not a checkout instruction for the prepared public repository's new history. These instructions do not claim that Windows, Intel macOS or an older macOS runtime has passed release acceptance.

The project is MIT licensed. Runtime artwork required by the build is included under the terms documented in ARTWORK.md. Keep these files to reproduce the frozen product.

## Requirements

- Git and Python 3.
- CMake 3.25 or newer.
- A C++17 compiler and, for macOS, Apple Command Line Tools or Xcode with a macOS SDK, Objective-C++ support and `codesign`.
- Network access to the official Steinberg repositories for the initial dependency bootstrap, or a separately verified copy of the pinned dependency checkout.

Run the commands from the repository root. No command below installs plugins, changes a DAW configuration or launches an editor.

## Pinned dependency

```sh
python3 scripts/bootstrap-sdk.py
```

The bootstrap script checks out the official VST3 SDK at the revision recorded in `third_party/dependency-lock.json` and checks each required submodule revision. It refuses to overwrite an SDK checkout with local changes. An existing SDK source copy without Git metadata requires separate verification; the script deliberately refuses to replace it.

| Component | Pinned revision |
| --- | --- |
| `vst3sdk` 3.8.1 (`v3.8.1_build_84`) | `3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96` |
| `base` | `fcf9da0bd27a16f7f03773a3a39822f28f5c8477` |
| `cmake` | `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5` |
| `pluginterfaces` | `4f547e8e102b47de4a8b8aaf343c73b700786372` |
| `public.sdk` | `586dc5e6c8012c3e4b01c79389375cbe96bdb1da` |

Do not substitute the latest SDK or submodule revisions for a release reproduction. The project does not use JUCE or VSTGUI. Toolchain and macOS SDK versions can affect binary output: pinned source inputs do not guarantee byte-identical builds across different toolchains.

## macOS arm64 with Command Line Tools

```sh
cmake --preset mac-arm64-clt -DSMTG_CREATE_PLUGIN_LINK=OFF
cmake --build --preset mac-arm64-clt --parallel 2 --target Just_eq Just_reverb Just_delay Just_tremolo Just_compressor Just_limiter Just_gate Just_flanger Just_fake_stereo Just_distortion
```

This preset uses Unix Makefiles, Release configuration and `arm64`. Its `XCODE_VERSION=9.0` cache value and `CLT-compatibility-gate` environment marker are an SDK detection compatibility gate. They do not identify the installed compiler as Xcode 9 and do not select or install an old Xcode release.

For a full Xcode installation, use the `mac-arm64` configure/build presets instead. The `mac-universal` preset configures both `arm64` and `x86_64`, but universal output is outside this release's accepted binary scope.

The ten products are written under `build/mac-arm64-clt/VST3/Release/`:

```text
Just_eq.vst3
Just_reverb.vst3
Just_delay.vst3
Just_tremolo.vst3
Just_compressor.vst3
Just_limiter.vst3
Just_gate.vst3
Just_flanger.vst3
Just_fake_stereo.vst3
Just_distortion.vst3
```

`Just_fake_stereo.vst3` is displayed as JUST Wider. Keep the bundle identity and internal slug unchanged. Test-only fixture bundles may also appear in the build directory; they are not release products.

The root build forces `SMTG_CREATE_PLUGIN_LINK=OFF`, disables automatic VST validation and copies the SDK notice into each bundle. The macOS post-build step applies an ad-hoc development signature. It does not perform Developer ID signing or notarization. The configured deployment target is macOS 11.0; actual runtime compatibility requires testing on the intended OS version.

## Tests without a GUI

For shared core tests without the SDK or any plugin build:

```sh
cmake --preset core-only
cmake --build --preset core-only --parallel 2
ctest --preset core-only --output-on-failure
```

After the macOS VST3 build, this explicit shared-contract subset avoids editor-host tests:

```sh
cmake --build --preset mac-arm64-clt --parallel 2 --target just_common_tests just_analysis_core_tests just_extended_spectrum_tests just_visual_contract_tests just_interaction_tests just_restore_default_tests just_vst3_tests just_module_tests
ctest --preset mac-arm64-clt --output-on-failure -R '^(common-contract|analysis-core-contract|extended-spectrum-contract|visual-contract|interaction-contract|restore-default-contract|vst3-contract|module-injection)$'
python3 scripts/check-contracts.py
python3 tests/common/ContractAuditTests.py
cmake --build --preset mac-arm64-clt --parallel 2 --target validator
python3 scripts/validate-vst3.py build/mac-arm64-clt
```

The validation script runs the SDK command-line validator on the ten build-directory bundles and writes logs and a summary there. It does not install them. Building the complete default target also builds test executables; building them does not run them.

The full macOS CTest registry includes native/offscreen editor fixtures. Inspect `ctest --preset mac-arm64-clt -N` before running a broader set. Activated UI tests are separately gated by `JUST_ACTIVATED_UI_TESTS`, which defaults to `OFF`. Automated, offscreen and command-line results do not substitute for native mouse/keyboard or DAW acceptance.

## Other configured platforms

The `windows-x64` preset selects Visual Studio 2022 and x64. It is a development configuration: the separate Windows port has not been accepted for 0.1.0. No supported Windows binary is promised by this document. Linux is limited here to `JUST_BUILD_VST3=OFF` core builds.

## Compatibility when changing code

Keep processor/controller UIDs and parameter IDs stable. Module parameter tables and golden mappings document saved-state and automation contracts. Test a change against the relevant DSP, state and VST3 tests before replacing a release artifact. Run installation and host acceptance as separate, explicit steps after build validation.

Some module manifests retain status fields from their original implementation checkpoints. Use their numeric IDs and mappings as compatibility evidence; consult the current suite README and release notes for the release platform status.
