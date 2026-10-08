# Preparation review — 2026-10-08

This is a source-preparation record, not a Windows execution result.

- Independent checkout base: `b99abd3be4ad89b21e3a0c3caf1f12904503f168`.
- `FrozenContract.hpp` was captured from that accepted commit's ten `Parameters.hpp` registries using the local C++17 compiler. It contains 404 parameter entries across ten products and is an independent constant snapshot for the real-DLL host; the runtime test never includes current product parameter headers.
- The platform-independent VST3 host sections, including both explicit `run<float>` and `run<double>` instantiations, passed `clang++ -std=c++17 -DNDEBUG -fsyntax-only` against the already present, pinned SDK 3.8.1 headers. Temporary declarations stood in for Windows API calls for that limited check. This confirms C++/SDK interface usage only: the full Win32 source was **not** compiled or linked.
- No Windows SDK/compiler or Windows machine was available to this worker. No Windows DLL, executable, BMP or native PASS evidence was produced here.
- PowerShell is not installed on this Mac executor; the PowerShell runner has been reviewed as source but not parsed or run by PowerShell here.
- `git diff --check` was clean before the preparation commit.

Concrete build integration findings sent to the integration owner:

1. Root `CMakeLists.txt` applies `/utf-8` whenever `WIN32`; guard it with `MSVC` (including clang-cl), or explicitly constrain the supported toolchain to Visual Studio 2022 x64. MinGW is not a validated path.
2. `plugins/fake_stereo/tests/FieldTransformTests.cpp` uses `M_PI`. MSVC does not expose this by default; use a local numeric constant or ensure `_USE_MATH_DEFINES` precedes `<cmath>`. The plugin DSP does not need to change.
3. SDK 3.8.1 `module_win32.cpp` uses a narrow `filesystem::path(inPath)` in its bundle branch. A UTF-8 path containing Chinese can be misinterpreted through the Windows active code page. The host validates the complete bundle then uses the SDK's real-DLL loader (`LoadLibraryW` after UTF-8 conversion). The SDK remains unchanged.
4. Link the native host with `sdk_hosting`, `user32`, `gdi32`, `comctl32`, `ole32`, and `shell32`; compile the SDK Win32 module loader and memory stream sources exactly once. Keep plugin UI libraries on plugin/common UI targets.
5. Module-local control IDs overlap shell IDs. The harness scopes IDs 100–104 to the shell and IDs 201–229 to the shell's overlay/panel/body; it does not recursively click the first matching ID.
6. Generic `WM_PRINT` screenshots require custom windows to implement `WM_PRINTCLIENT`. Shared and effect UI owners were notified; saved captures still need human pixel inspection and do not substitute for activated REAPER windows.
