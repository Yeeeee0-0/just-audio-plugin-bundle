# Run from an x64 Native Tools Command Prompt for Visual Studio 2022.
# This creates only a hidden isolated Win32 fixture. It never loads REAPER,
# installs a plug-in, changes presets, or touches an existing project.
[CmdletBinding()]
param([string]$BuildDirectory = (Join-Path $env:TEMP 'just-eq-native-contract'))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'Use the x64 Native Tools Command Prompt for Visual Studio 2022 with the Desktop C++ workload.' }
if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'The active MSVC target must be x64.' }
New-Item -ItemType Directory -Force $BuildDirectory | Out-Null
Push-Location $BuildDirectory
try {
    & cl.exe /nologo /std:c++17 /permissive- /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$root" `
        (Join-Path $root 'plugins\eq\EditorTestsWindows.cpp') (Join-Path $root 'common\ui\ControlsWindows.cpp') `
        /Fe:just_eq_windows_editor_tests.exe /link user32.lib gdi32.lib comctl32.lib gdiplus.lib ole32.lib
    if ($LASTEXITCODE -ne 0) { throw "EQ Windows adapter test compilation failed: $LASTEXITCODE" }
    & '.\just_eq_windows_editor_tests.exe'
    if ($LASTEXITCODE -ne 0) { throw "EQ Windows adapter test failed: $LASTEXITCODE" }
} finally { Pop-Location }
