Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require-WindowsX64 {
    if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or
        -not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
        throw 'Use 64-bit PowerShell on Windows x64. This script does not cross-compile.'
    }
    if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Native x64 execution required; ARM64 is not this candidate.' }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments, [string]$Log) {
    # PowerShell 5.1 turns native stderr into ErrorRecord even for exit 0.
    # Preserve diagnostics, and use the native exit code as the success boundary.
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($Log) { & $Program @Arguments 2>&1 | Tee-Object -FilePath $Log }
        else { & $Program @Arguments }
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($nativeExit -ne 0) { throw "$Program failed (exit $nativeExit). See $Log" }
    if ($Log -and -not (Test-Path -LiteralPath $Log -PathType Leaf)) { throw "Required diagnostic log was not written: $Log" }
}
function Find-JustTools {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vs = $null
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed.' }
    }
    if (-not $vs) { throw 'Visual Studio 2022 C++ x64 tools not found. Install only from Microsoft with Windows SDK, then rerun. No automatic installation performed.' }
    $cmake = if ($cmakeCommand) { $cmakeCommand.Source } else { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
    if (-not (Test-Path -LiteralPath $cmake)) { throw 'CMake >= 3.25 is required (Visual Studio CMake tools or official Kitware distribution).' }
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest)) { throw 'Matching ctest.exe not found.' }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python -and $python.Source -notlike '*WindowsApps*') { $pythonPath = $python.Source; $pythonArgs = @() }
    else {
        $launcher = Get-Command py -ErrorAction SilentlyContinue
        if (-not $launcher) { throw 'Python 3.9+ is required. Use the official python.org distribution; no automatic installation performed.' }
        $pythonPath = $launcher.Source; $pythonArgs = @('-3')
    }
    $pythonVersion = & $pythonPath @pythonArgs -c 'import sys; assert sys.version_info >= (3,9); print(sys.version)'
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.9+ check failed.' }
    $version = & $cmake --version
    if ($LASTEXITCODE -ne 0 -or $version[0] -notmatch '([0-9]+\.[0-9]+\.[0-9]+)') { throw 'CMake version probe failed.' }
    if ([version]$Matches[1] -lt [version]'3.25.0') { throw 'CMake >= 3.25 required.' }
    $msvcDefault = Get-Content -LiteralPath (Join-Path $vs 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt') -Raw
    return @{ CMake=$cmake; CTest=$ctest; Python=$pythonPath; PythonArgs=$pythonArgs; PythonVersion=$pythonVersion; VisualStudio=$vs; CMakeVersion=$version[0]; MSVCDefault=$msvcDefault.Trim() }
}
function Write-JustJson($Value, [string]$Path) {
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Get-JustSlugs { return @('eq','reverb','delay','tremolo','compressor','limiter','gate','flanger','fake_stereo','distortion') }
function Get-JustTreeHashes([string]$Directory) {
    $base = (Get-Item -LiteralPath $Directory).FullName.TrimEnd('\') + '\'
    $result = [ordered]@{}
    Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName | ForEach-Object {
        $result[$_.FullName.Substring($base.Length).Replace('\','/')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $result
}
function Get-JustItemHashes([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Container) { return Get-JustTreeHashes $Path }
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return @{ '$file'=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() } }
    return @{}
}
function Convert-JustHashMap($Object) {
    $map=@{}
    if ($Object -is [System.Collections.IDictionary]) { foreach ($key in $Object.Keys) { $map[$key]=$Object[$key] } }
    elseif ($null -ne $Object) { foreach ($property in $Object.PSObject.Properties) { $map[$property.Name]=$property.Value } }
    return $map
}
function Test-JustHashes($Actual, $Expected, [switch]$AllowSubset) {
    $a=Convert-JustHashMap $Actual; $e=Convert-JustHashMap $Expected
    if (-not $AllowSubset -and $a.Count -ne $e.Count) { return $false }
    foreach ($key in $a.Keys) { if (-not $e.ContainsKey($key) -or $a[$key] -ne $e[$key]) { return $false } }
    return $true
}
function Test-JustPathOverlap([string]$First,[string]$Second) {
    $a=[IO.Path]::GetFullPath($First).TrimEnd('\')
    $b=[IO.Path]::GetFullPath($Second).TrimEnd('\')
    return $a.Equals($b,[StringComparison]::OrdinalIgnoreCase) -or $a.StartsWith($b+'\',[StringComparison]::OrdinalIgnoreCase) -or $b.StartsWith($a+'\',[StringComparison]::OrdinalIgnoreCase)
}
