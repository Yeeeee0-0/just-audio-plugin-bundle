[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BackupDir)
. (Join-Path $PSScriptRoot 'Common.ps1')
Require-WindowsX64
if (Get-Process reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'Save and close REAPER normally first. No process will be killed.' }
$backup=(Resolve-Path -LiteralPath $BackupDir).Path
$manifest=Get-Content -LiteralPath (Join-Path $backup 'restore-manifest.json') -Raw | ConvertFrom-Json
if (@($manifest.previous).Count -ne 10) { throw 'Expected a ten-plugin backup manifest.' }
foreach ($item in $manifest.previous) {
    if ($item.name -notin @((Get-JustSlugs) | ForEach-Object { "Just_$_.vst3" })) { throw 'Unexpected backup filename.' }
    $target=Join-Path $manifest.destination $item.name
    $old=Join-Path $backup "plugins\$($item.name)"
    if ($item.existed -and -not (Test-Path -LiteralPath $old)) { throw "Original backup missing: $old" }
    if ($item.existed -and -not (Test-JustHashes (Get-JustItemHashes $old) $item.hashes)) { throw "Original backup hash mismatch: $old. Nothing restored." }
    $current=Get-JustItemHashes $target
    if (Test-JustHashes $current $item.hashes) { continue } # untouched old version after a partial install
    $new=@{}; $prefix="$($item.name)/"
    foreach ($property in $manifest.installedHashes.PSObject.Properties) { if ($property.Name.StartsWith($prefix)) { $new[$property.Name.Substring($prefix.Length)]=$property.Value } }
    # A missing or partially copied candidate can be rolled back; any unexpected
    # byte is preserved for manual review, never silently deleted.
    if (-not (Test-JustHashes $current $new -AllowSubset)) { throw "Files changed since installation. Preserve them separately before restore: $target" }
}
foreach ($item in $manifest.previous) {
    $target=Join-Path $manifest.destination $item.name
    if (Test-JustHashes (Get-JustItemHashes $target) $item.hashes) { continue }
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    if ($item.existed) { Copy-Item -LiteralPath (Join-Path $backup "plugins\$($item.name)") -Destination $manifest.destination -Recurse }
    if (-not (Test-JustHashes (Get-JustItemHashes $target) $item.hashes)) { throw "Restore verification failed: $target. Backup retained." }
}
Write-Host 'Original JUST modules restored. The private backup is retained. REAPER resource/preset backups were not copied over current user settings.'
