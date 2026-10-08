# No install, network access, GitHub write, REAPER launch or host configuration changes.
[CmdletBinding()]
param([ValidateRange(1,16)][int]$Jobs=2)
. (Join-Path $PSScriptRoot 'Common.ps1')
Require-WindowsX64
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $root 'build\windows-x64'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$evidence = Join-Path $root "build\windows-evidence\$stamp"
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$tools = Find-JustTools
$env:PYTHONUTF8 = '1'
Write-JustJson @{ platform='Windows native x64'; dateUTC=[DateTime]::UtcNow.ToString('o'); tools=$tools; os=[Environment]::OSVersion.VersionString; powershell=$PSVersionTable.PSVersion.ToString(); jobs=$Jobs; stage='preflight' } (Join-Path $evidence 'environment.json')
try {
    foreach ($check in @('scripts\verify-source-copy.py','scripts\check-contracts.py','scripts\windows\check-source.py')) {
        Invoke-Checked $tools.Python ($tools.PythonArgs + @((Join-Path $root $check))) (Join-Path $evidence ((Split-Path $check -Leaf)+'.log'))
    }
    Invoke-Checked $tools.CMake @('-S',$root,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-DBUILD_TESTING=ON','-DJUST_BUILD_VST3=ON',"-DJUST_VST3_SDK=$root\third_party\vst3sdk", "-DJUST_WINDOWS_EVIDENCE_ROOT=$evidence\native",'-DSMTG_USE_STATIC_CRT=ON','-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>') (Join-Path $evidence 'configure.log')
    Copy-Item -LiteralPath (Join-Path $build 'CMakeCache.txt') -Destination $evidence
    Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -Recurse -File | Where-Object { $_.Name -in @('CMakeCXXCompiler.cmake','CMakeSystem.cmake') } | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $evidence }
    Invoke-Checked $tools.CMake @('--build',$build,'--config','Release','--parallel',"$Jobs") (Join-Path $evidence 'build.log')
    Invoke-Checked $tools.CTest @('--test-dir',$build,'-C','Release','--output-on-failure','--no-tests=error','--output-junit',(Join-Path $evidence 'ctest.xml')) (Join-Path $evidence 'ctest.log')
    $presetTest = Get-ChildItem -LiteralPath $build -Filter just_user_preset_store_tests.exe -Recurse | Where-Object { $_.Directory.Name -eq 'Release' } | Select-Object -First 1
    if (-not $presetTest) { throw 'User preset test executable missing.' }
    Invoke-Checked $presetTest.FullName @((Join-Path $evidence ('preset-fixture-'+[guid]::NewGuid()))) (Join-Path $evidence 'preset-store.log')
    Invoke-Checked $tools.Python ($tools.PythonArgs + @((Join-Path $PSScriptRoot 'validate-candidates.py'),$build,$evidence)) (Join-Path $evidence 'validation.log')
    & (Join-Path $PSScriptRoot 'Stage-Candidate.ps1') -BuildDir $build -EvidenceDir $evidence
    Write-JustJson @{ result='PASS_NATIVE_BUILD_AND_AUTOMATED_TESTS'; reaper='NOT_RUN'; visualManual='NOT_RUN'; evidence=$evidence } (Join-Path $evidence 'result.json')
    Write-Host "Native build + automated tests passed. REAPER/manual visual/audio acceptance remains pending. Evidence: $evidence"
} catch {
    Write-JustJson @{ result='FAILED'; error=$_.Exception.Message; reaper='NOT_RUN'; evidence=$evidence } (Join-Path $evidence 'result.json')
    throw
}
