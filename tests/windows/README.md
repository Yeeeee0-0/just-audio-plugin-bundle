# Windows native runtime verification

Status at preparation: **NOT BUILT OR RUN ON WINDOWS**. These sources were prepared on macOS. There is no claim that the candidate already passed Windows compilation, the Windows CLI, activated-window visual inspection, REAPER, or real input/audio tests.

`EditorHostWindows.cpp` is a native x64 command-line VST3 host. It loads each actual candidate DLL through the pinned Steinberg SDK `module_win32.cpp`, creates a hidden HWND, and never opens an audio device, starts REAPER, installs plugins, or changes an existing project. It does not link JUST's processor/controller/DSP implementation into the host. `FrozenContract.hpp` is an explicit snapshot from accepted source commit `b99abd3be4ad89b21e3a0c3caf1f12904503f168`; do not regenerate it to make an incompatible candidate pass.

## Integration

The current integration declares this target under `if(WIN32 AND BUILD_TESTING)` in the root CMake file:

```cmake
add_executable(just_editor_host_windows
    tests/windows/EditorHostWindows.cpp
    "${JUST_VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp"
    "${JUST_VST3_SDK}/public.sdk/source/common/memorystream.cpp")
target_link_libraries(just_editor_host_windows PRIVATE
    just_common sdk_hosting user32 gdi32 comctl32 ole32 shell32)
target_compile_definitions(just_editor_host_windows PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(just_editor_host_windows PRIVATE /utf-8)
endif()
```

Use a Visual Studio 2022 x64 generator/configuration with the Windows SDK. `module_win32.cpp` is an explicit source, matching the SDK's own host examples; do not also compile the Mac loader. CMake and SDK dependency versions remain those fixed by the main handoff. DLL UI dependencies such as GDI+ belong to plugin targets; this host dynamically loads those plugins.

The host takes **exactly three arguments**:

```powershell
& .\build-windows\Release\just_editor_host_windows.exe `
  .\candidate\Just_eq.vst3 eq .\evidence\eq-run-001
```

The evidence directory must not already exist. This deliberately prevents previous logs/captures from becoming ambiguous. For all ten staged bundles:

```powershell
.\tests\windows\Run-RuntimeTests.ps1 `
  -HostExe .\build-windows\Release\just_editor_host_windows.exe `
  -BundleRoot .\candidate `
  -EvidenceRoot .\evidence\runtime-run-001
```

PowerShell 5.1 or later is supported by the runner's syntax. It resolves exactly one bundle for each frozen slug, records SHA-256 of each binary and the host executable, runs plugins serially in separate processes, collects JSON/logs/BMPs, and gives each process a 120-second timeout. A timeout terminates only that newly launched test host. A missing/ambiguous bundle, native crash, nonzero exit, missing result or failed precision is a failure. Inspect and fix it before repeating into a **new** evidence directory.

## Checks performed by a successful native run

- Standard `Just_<slug>.vst3/Contents/x86_64-win/Just_<slug>.vst3`, actual PE machine `0x8664`, PE32+, DLL characteristic, local UI icon/manifest and SDK license.
- Two runtime factory classes, `Yee Huang`, `0.1.0`, accepted product names, fixed processor/controller FUIDs, and component-advertised controller ID. Class order is not assumed.
- Frozen parameter count, order, IDs, step counts, default normalized values and automation/bypass flags. Counts are EQ 195, Reverb 36, Delay 45, Tremolo 14, Compressor 21, Limiter 11, Gate 20, Flanger 17, Wider 18 and Distortion 27.
- Actual component/controller initialization, connection points, stereo buses, optional sidechain deactivated, and 48 kHz / 64-sample processing in both float32 and float64.
- Two independent instances of the same DLL receive identical complete initial states, including configuration and random seed. One has an editor; the reference does not. Output samples, silence flags and serialized sound state must remain bit-exact during view changes. Finite output and unchanged out-of-place inputs are checked.
- HWND support, attach, legal host resize, 13 Simple/Advanced toggles, English default, Chinese/English settings changes, About and preset-manager presence, close/reopen, independent UI chunk persistence and one balanced ID 0 bypass edit gesture. Shell IDs are scoped to the shell and its settings panel, avoiding colliding module control IDs.
- Fresh EQ's frozen UI v3 chunk reports the 120 dB analyzer default.
- Three host automation points at offsets 7, 47 and 63, silent-input blocks, bypass processing, exact sound-state save/restore, and proof that restoring UI preferences does not overwrite the sound bypass parameter.
- Eight BMP views and their control inventories in the float64 run: Simple, Advanced, resized, English settings, Chinese settings, About, preset manager, bypass. Captures use hidden-child `WM_PRINT`; a uniform capture is logged and requires investigation. No screenshot is automatically declared visually correct.

The loader first validates the standard bundle layout and then passes the actual DLL path to SDK `Module::create`. SDK 3.8.1's package path constructs a narrow `filesystem::path`; the direct DLL path follows the SDK's UTF-8-to-`LoadLibraryW` conversion and avoids misreading Chinese user-directory names. This is still the real packaged DLL, not a linked test double.

## Evidence limits and remaining Windows acceptance

A CLI PASS establishes only the checks above. The JSON always marks REAPER, physical input, audio-device playback, and Mac-versus-Windows audio comparison as **NOT_RUN**. Same-DLL twins establish that editor activity does not perturb processing; they do not independently prove the DSP algorithm or cross-platform numerical equivalence. Parameter/DSP/module tests and actual host acceptance remain necessary.

The test deliberately does not click the preset popup, invoke native file dialogs, import/export or mutate user presets. The manager's existence and factory choices are checked; existing common preset/state tests cover serialization, while Windows native user-preset save/load/rename/delete still require the acceptance checklist; import/export are not part of the frozen Mac UI contract. `JUST_USER_PRESET_ROOT` is set before DLL loading to the new evidence directory's isolated preset root so incidental manager reads do not touch the real preset location.

Still verify in the user's existing Windows REAPER, using a new test project and the approved backup workflow: activated rendering at requested scales/DPI; rounded shapes and typography against accepted Mac references; actual mouse/keyboard focus, drag capture, tooltips and EQ band card; full parameter automation/recall; user/factory preset workflows; live feedback, bypass red button/frozen displays/resume; optional sidechain, mono/stereo layouts and supported sample rates; audio listening and rendered comparison; preservation of existing REAPER settings, authorization and projects. Record actual results and failures, not assumed passes.
