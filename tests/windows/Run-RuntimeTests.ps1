#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$HostExe,
    [Parameter(Mandatory = $true)][string]$BundleRoot,
    [Parameter(Mandatory = $true)][string]$EvidenceRoot,
    [ValidateRange(20, 600)][int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this test on Windows. macOS preparation is not Windows execution evidence.'
}
$HostExe = (Resolve-Path -LiteralPath $HostExe).Path
$BundleRoot = (Resolve-Path -LiteralPath $BundleRoot).Path
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
if (Test-Path -LiteralPath $EvidenceRoot) { throw 'EvidenceRoot must be new; retain earlier results.' }
$slugs = @('eq', 'reverb', 'delay', 'tremolo', 'compressor', 'limiter', 'gate', 'flanger', 'fake_stereo', 'distortion')
# Resolve every exact bundle before running anything. Ambiguous builds are errors.
$bundles = @{}
foreach ($slug in $slugs) {
    $name = "Just_$slug.vst3"
    $matches = @(Get-ChildItem -LiteralPath $BundleRoot -Directory -Recurse -Filter $name)
    if ($matches.Count -ne 1) { throw "Expected one $name below BundleRoot, found $($matches.Count). Use one staged candidate directory." }
    $binary = Join-Path $matches[0].FullName "Contents\x86_64-win\$name"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing x64 DLL: $binary" }
    $bundles[$slug] = $matches[0].FullName
}
$null = New-Item -ItemType Directory -Path $EvidenceRoot
function Quote-NativeArgument([string]$Value) {
    # ProcessStartInfo with UseShellExecute=false; no shell interpolation.
    # Windows paths cannot contain a literal double quote; double trailing slashes.
    if ($Value.Contains('"')) { throw 'Invalid double quote in native argument.' }
    return '"' + [regex]::Replace($Value, '(\\+)$', '$1$1') + '"'
}
$started = [DateTime]::UtcNow.ToString('o')
$rows = @()
$hostHash = (Get-FileHash -LiteralPath $HostExe -Algorithm SHA256).Hash.ToLowerInvariant()
foreach ($slug in $slugs) {
    $bundle = $bundles[$slug]
    $binary = Join-Path $bundle "Contents\x86_64-win\Just_$slug.vst3"
    $testEvidence = Join-Path $EvidenceRoot $slug
    $row = [ordered]@{
        slug = $slug; bundle = $bundle; binary = $binary
        binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
        process_exit_code = $null; timed_out = $false; status = 'FAIL'; error = ''
        result_path = (Join-Path $testEvidence 'result.json')
        real_reaper_validation = 'NOT_RUN'; physical_input = 'NOT_RUN'; audio_device_playback = 'NOT_RUN'
    }
    try {
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName = $HostExe
        $start.Arguments = ((@($bundle, $slug, $testEvidence) | ForEach-Object { Quote-NativeArgument $_ }) -join ' ')
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = New-Object Diagnostics.Process
        $process.StartInfo = $start
        if (-not $process.Start()) { throw 'Could not start native test process.' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $row.timed_out = $true
            $process.Kill() # Only this newly launched test host, never REAPER.
            $process.WaitForExit()
        }
        $stdout.Result | Set-Content -LiteralPath (Join-Path $EvidenceRoot "$slug-stdout.log") -Encoding UTF8
        $stderr.Result | Set-Content -LiteralPath (Join-Path $EvidenceRoot "$slug-stderr.log") -Encoding UTF8
        $row.process_exit_code = $process.ExitCode
        $process.Dispose()
        if ($row.timed_out) { throw "Native hidden-window test exceeded $TimeoutSeconds seconds." }
        if ($row.process_exit_code -ne 0) { throw "Native test exited $($row.process_exit_code); inspect runtime.log and stderr." }
        if (-not (Test-Path -LiteralPath $row.result_path -PathType Leaf)) { throw 'Process returned without result.json.' }
        $report = Get-Content -LiteralPath $row.result_path -Raw | ConvertFrom-Json
        if ($report.status -ne 'PASS' -or -not $report.actual_vst3_dll_loaded -or -not $report.float32_passed -or -not $report.float64_passed) {
            throw 'Incomplete runtime evidence; do not count this plugin as passed.'
        }
        $row.status = 'PASS'
    }
    catch { $row.error = $_.Exception.Message }
    $rows += [pscustomobject]$row
    Write-Host "$slug $($row.status) $($row.error)"
}
$allPassed = @($rows | Where-Object { $_.status -ne 'PASS' }).Count -eq 0
$summary = [ordered]@{
    schema = 1; started_utc = $started; finished_utc = [DateTime]::UtcNow.ToString('o')
    status = $(if ($allPassed) { 'PASS' } else { 'FAIL' })
    scope = 'Windows x64 native CLI / hidden HWND only; no REAPER, physical input or audio device acceptance'
    host_exe = $HostExe; host_sha256 = $hostHash; powershell_version = $PSVersionTable.PSVersion.ToString()
    plugins = $rows
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $EvidenceRoot 'summary.json') -Encoding UTF8
if (-not $allPassed) { exit 1 }
