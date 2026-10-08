# Explicitly selected destination; never guesses, kills REAPER, edits its configuration or installs tooling.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$CandidateDir,
      [Parameter(Mandatory=$true)][string]$Destination,
      [Parameter(Mandatory=$true)][string]$ReaperResourceDir,
      [Parameter(Mandatory=$true)][string]$BackupRoot)
. (Join-Path $PSScriptRoot 'Common.ps1')
Require-WindowsX64
if (Get-Process reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'REAPER is running. Save the current work safely and close it normally before installation; this script never closes it.' }
$candidate = (Resolve-Path -LiteralPath $CandidateDir).Path
$manifest = Get-Content -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.status -ne 'WINDOWS_NATIVE_AUTOMATED_CANDIDATE_NOT_REAPER_ACCEPTED' -or @($manifest.plugins).Count -ne 10) { throw 'Expected the native candidate produced by Build-And-Test.ps1.' }
$target = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
if (Test-Path -LiteralPath $target -PathType Leaf) { throw 'Destination must be a VST3 directory.' }
if (-not (Test-Path -LiteralPath $ReaperResourceDir -PathType Container)) { throw 'REAPER resource directory must be discovered from this computer (portable installations differ).' }
if (-not (Test-Path -LiteralPath (Join-Path $ReaperResourceDir 'reaper.ini'))) { throw 'reaper.ini missing from selected REAPER resource directory.' }
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$backup = Join-Path ([IO.Path]::GetFullPath($BackupRoot)) "JUST-Windows-before-$timestamp"
if (Test-Path -LiteralPath $backup) { throw 'Backup path already exists.' }
foreach ($protected in @($candidate,$target,[IO.Path]::GetFullPath($ReaperResourceDir))) {
    if (Test-JustPathOverlap $backup $protected) { throw 'Backup directory overlaps source, install target, or REAPER resources. Choose a separate backup directory.' }
}
if ((Test-JustPathOverlap $candidate $target) -or (Test-JustPathOverlap $candidate $ReaperResourceDir)) { throw 'Candidate source must be separate from install target and REAPER resources.' }
$vstRoot = Join-Path $candidate 'VST3'
$hashes = Get-JustTreeHashes $vstRoot
foreach ($entry in $manifest.hashes.PSObject.Properties) {
    if (-not $hashes.Contains($entry.Name) -or $hashes[$entry.Name] -ne $entry.Value) { throw "Candidate bundle hash mismatch: $($entry.Name)" }
}
if ($hashes.Count -ne @($manifest.hashes.PSObject.Properties).Count) { throw 'Unexpected candidate files.' }
# Stop before writing if another standard VST3 root contains the same module names.
foreach ($standard in @((Join-Path $env:CommonProgramFiles 'VST3'), (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'))) {
    if (-not (Test-Path -LiteralPath $standard)) { continue }
    foreach ($slug in Get-JustSlugs) {
        $intended=Join-Path $target "Just_$slug.vst3"
        $intendedPrefix=$intended.TrimEnd('\')+'\'
        $duplicates=@(Get-ChildItem -LiteralPath $standard -Filter "Just_$slug.vst3" -Recurse -ErrorAction Stop | Where-Object { $_.FullName.TrimEnd('\') -ine $intended -and -not $_.FullName.StartsWith($intendedPrefix,[StringComparison]::OrdinalIgnoreCase) })
        if ($duplicates.Count) { throw "Duplicate JUST bundle outside selected target in $standard. Resolve the intended existing install location and backup those copies; nothing installed." }
    }
}
New-Item -ItemType Directory -Path (Join-Path $backup 'plugins') -Force | Out-Null
Copy-Item -LiteralPath $ReaperResourceDir -Destination (Join-Path $backup 'REAPER-resource-private') -Recurse
$presets = Join-Path $env:APPDATA 'JUST\Presets'
if (Test-Path -LiteralPath $presets) { Copy-Item -LiteralPath $presets -Destination (Join-Path $backup 'JUST-presets-private') -Recurse }
$previous = @()
foreach ($slug in Get-JustSlugs) {
    $name = "Just_$slug.vst3"; $old = Join-Path $target $name
    $exists = Test-Path -LiteralPath $old
    $originalHashes=Get-JustItemHashes $old
    if ($exists) {
        Copy-Item -LiteralPath $old -Destination (Join-Path $backup 'plugins') -Recurse
        if (-not (Test-JustHashes (Get-JustItemHashes (Join-Path $backup "plugins\$name")) $originalHashes)) { throw "Backup verification failed: $name. Nothing installed." }
    }
    $previous += @{ name=$name; existed=$exists; hashes=$originalHashes }
}
Write-JustJson @{ destination=$target; candidate=$candidate; dateUTC=[DateTime]::UtcNow.ToString('o'); previous=$previous; installedHashes=$hashes; status='BACKUP_COMPLETE_INSTALL_STARTING'; privateBackup='Contains REAPER settings/license; keep local. Do not upload.' } (Join-Path $backup 'restore-manifest.json')
New-Item -ItemType Directory -Path $target -Force | Out-Null
try {
    foreach ($slug in Get-JustSlugs) {
        $name="Just_$slug.vst3"; $old=Join-Path $target $name
        if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
        Copy-Item -LiteralPath (Join-Path $vstRoot $name) -Destination $target -Recurse
        $expected=Get-JustTreeHashes (Join-Path $vstRoot $name); $actual=Get-JustTreeHashes $old
        if (-not (Test-JustHashes $actual $expected)) { throw "Installed hash mismatch: $name" }
    }
} catch { Write-Warning "Installation incomplete. Restore using Restore-Candidate.ps1 -BackupDir '$backup'."; throw }
Write-Host "Installed ten unsigned candidates. Backup: $backup"
Write-Host 'REAPER settings/license/projects were not modified. Open REAPER normally, use a new dedicated test project, and follow ACCEPTANCE-zh.md.'
