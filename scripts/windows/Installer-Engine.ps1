# Standalone engine packaged inside the unsigned NSIS preview installer.
# It never discovers, reads, copies or edits REAPER settings, licenses or presets.
[CmdletBinding()]
param(
    [ValidateSet('Install','Restore')][string]$Action='Install',
    [string]$CandidateDir,
    [string]$Destination,
    [string]$BackupRoot,
    [string]$Selected='all',
    [string]$BackupDir,
    [Parameter(Mandatory=$true)][string]$ReportPath,
    [int]$TestFailAfter=0,
    [string]$IsolationTestRoot
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$script:Slugs=@('eq','reverb','delay','tremolo','compressor','limiter','gate','flanger','fake_stereo','distortion')
$script:Journal=$null
$script:Backup=''
$script:Changed=$false
$script:CanReport=$false
$script:InstallMutex=$null
$script:MutexOwned=$false
$script:Report=[ordered]@{ schema=1; action=$Action; status='PREFLIGHT'; version='0.1.0'; vendor='Yee Huang'; signed=$false; userReaperValidation='NOT_RUN'; selected=@(); backup=''; error=''; utc=[DateTime]::UtcNow.ToString('o') }

function Full-InstallerPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -notmatch '^[A-Za-z]:[\\/]') { throw 'An absolute local drive path is required.' }
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    if ($full.Length -le 3) { throw 'Drive roots are not an allowed installation, candidate or backup location.' }
    foreach ($part in $full.Substring(3).Split('\')) {
        if ($part -match '[. ]$' -or $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)' -or $part.Contains(':')) { throw 'Ambiguous Windows path component refused.' }
    }
    $probe=$full
    while ($probe.Length -gt 3) {
        if (Test-Path -LiteralPath $probe) {
            $item=Get-Item -LiteralPath $probe -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse points are not supported: $probe" }
        }
        $next=[IO.Path]::GetDirectoryName($probe)
        if (-not $next -or $next -eq $probe) { break }
        $probe=$next
    }
    return $full
}
function Overlap-InstallerPath([string]$A,[string]$B) {
    return $A.Equals($B,[StringComparison]::OrdinalIgnoreCase) -or $A.StartsWith($B+'\',[StringComparison]::OrdinalIgnoreCase) -or $B.StartsWith($A+'\',[StringComparison]::OrdinalIgnoreCase)
}
function Assert-InstallerSeparate([string]$A,[string]$B) { if (Overlap-InstallerPath $A $B) { throw "Paths must not overlap: $A ; $B" } }
function Assert-InstallerTree([string]$Path) {
    $null=Full-InstallerPath $Path
    if (-not (Test-Path -LiteralPath $Path)) { return }
    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force -Recurse)) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point refused: $($item.FullName)" }
    }
}
function Get-InstallerHashes([string]$Path) {
    Assert-InstallerTree $Path
    $map=@{}
    if (Test-Path -LiteralPath $Path -PathType Leaf) { $map['$file']=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant(); return $map }
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $base=(Get-Item -LiteralPath $Path).FullName.TrimEnd('\')+'\'
        foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force -Recurse -File)) {
            $key=$item.FullName.Substring($base.Length).Replace('\','/')
            if ($map.ContainsKey($key)) { throw 'Case-colliding candidate file names refused.' }
            $map[$key]=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return $map
}
function Convert-InstallerHashes($Object) {
    $map=@{}
    if ($Object -is [Collections.IDictionary]) { $keys=@($Object.Keys) } else { $keys=@($Object.PSObject.Properties | ForEach-Object { $_.Name }) }
    foreach ($key in $keys) {
        if ($key -ne '$file' -and ($key -notmatch '^[^\\:]+$' -or $key.StartsWith('/') -or @($key.Split('/') | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' }).Count)) { throw 'Unsafe manifest hash path.' }
        $value=if ($Object -is [Collections.IDictionary]) { $Object[$key] } else { $Object.$key }
        if ($value -notmatch '^[0-9a-fA-F]{64}$' -or $map.ContainsKey($key)) { throw 'Invalid or duplicate SHA256 entry.' }
        $map[$key]=$value.ToLowerInvariant()
    }
    return $map
}
function Equal-InstallerHashes($Actual,$Expected,[switch]$Subset) {
    $a=Convert-InstallerHashes $Actual; $e=Convert-InstallerHashes $Expected
    if (-not $Subset -and $a.Count -ne $e.Count) { return $false }
    foreach ($key in $a.Keys) { if (-not $e.ContainsKey($key) -or $a[$key] -ne $e[$key]) { return $false } }
    return $true
}
function Write-InstallerJson($Value,[string]$Path) {
    $parent=Split-Path -Parent $Path
    $null=Full-InstallerPath $parent
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temp=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $Value | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $temp -Encoding UTF8
    Move-Item -LiteralPath $temp -Destination $Path -Force
}
function Lock-InstallerDestination([string]$Target) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Target.ToUpperInvariant())))).Replace('-','') }
    finally { $sha.Dispose() }
    $script:InstallMutex=New-Object Threading.Mutex -ArgumentList @($false,('Global\JUST-VST3-Installer-'+$key))
    try { $script:MutexOwned=$script:InstallMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $script:MutexOwned=$true }
    if (-not $script:MutexOwned) { throw 'Another installer or restore transaction is already using this destination.' }
}
function Assert-InstallerIdle {
    if (Get-Process -Name reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'REAPER is running. Save work and close it normally, then retry. This installer never terminates processes.' }
}
function Save-InstallerJournal { Write-InstallerJson $script:Journal (Join-Path $script:Backup 'installer-transaction.json') }
function Read-InstallerCandidate([string]$Root) {
    Assert-InstallerTree $Root
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'candidate-manifest.json') -Raw | ConvertFrom-Json
    if ($manifest.status -ne 'WINDOWS_NATIVE_AUTOMATED_CANDIDATE_NOT_REAPER_ACCEPTED' -or $manifest.version -ne '0.1.0' -or $manifest.vendor -ne 'Yee Huang' -or $manifest.architecture -ne 'x86_64' -or $manifest.format -ne 'VST3' -or @($manifest.plugins).Count -ne 10) { throw 'Expected the native 0.1.0 x64 VST3 candidate manifest.' }
    $actual=Get-InstallerHashes (Join-Path $Root 'VST3')
    if (-not (Equal-InstallerHashes $actual $manifest.hashes)) { throw 'Candidate tree SHA256 verification failed (including unselected plugins).' }
    $seen=@{}
    foreach ($plugin in $manifest.plugins) {
        $slug=[string]$plugin.slug
        if ($slug -notin $script:Slugs -or $seen.ContainsKey($slug)) { throw 'Unexpected or duplicate plugin identity.' }
        $seen[$slug]=$true; $name="Just_$slug.vst3"
        $relative="VST3/$name/Contents/x86_64-win/$name"
        if ($plugin.bundle -cne "VST3/$name" -or $plugin.binary -cne $relative -or $plugin.version -ne '0.1.0' -or $plugin.vendor -ne 'Yee Huang') { throw "Unexpected candidate layout or metadata: $slug" }
        $binary=Join-Path $Root $relative
        if ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ine $plugin.sha256) { throw "Binary SHA256 mismatch: $slug" }
        $stream=[IO.File]::OpenRead($binary)
        try {
            $reader=New-Object IO.BinaryReader($stream)
            if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Missing PE MZ header: $slug" }
            $stream.Position=0x3c; $offset=$reader.ReadUInt32()
            if ($offset -gt $stream.Length-6) { throw "Invalid PE offset: $slug" }
            $stream.Position=$offset
            if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Candidate is not AMD64 PE: $slug" }
        } finally { $stream.Dispose() }
        $version=(Get-Item -LiteralPath $binary).VersionInfo
        if ($version.FileVersion -ne '0.1.0' -or $version.ProductVersion -ne '0.1.0' -or $version.CompanyName -ne 'Yee Huang') { throw "Binary version/vendor mismatch: $slug" }
    }
    foreach ($file in $actual.Keys) { if (($file -split '/')[0] -notin @($script:Slugs | ForEach-Object { "Just_$_.vst3" })) { throw "Unlisted bundle in candidate: $file" } }
    return $manifest
}
function Restore-InstallerTransaction($Journal,[string]$Backup) {
    if ($Journal.schema -ne 1 -or $Journal.version -ne '0.1.0' -or @($Journal.items).Count -lt 1 -or @($Journal.items).Count -gt 10) { throw 'Invalid installer transaction.' }
    $target=Full-InstallerPath ([string]$Journal.destination)
    Assert-InstallerSeparate $target $Backup
    if (Test-Path -LiteralPath $target -PathType Leaf) { throw 'Restore destination must be a directory.' }
    Assert-InstallerTree $target
    Assert-InstallerTree $Backup
    $seen=@{}
    # Preflight every selected entry before touching any target.
    foreach ($item in $Journal.items) {
        if ($item.slug -notin $script:Slugs -or $seen.ContainsKey($item.slug)) { throw 'Invalid transaction plugin selection.' }; $seen[$item.slug]=$true
        $name="Just_$($item.slug).vst3"; $old=Join-Path $Backup "previous\$name"; $dest=Join-Path $target $name
        if ($item.existed -and (-not (Test-Path -LiteralPath $old) -or -not (Equal-InstallerHashes (Get-InstallerHashes $old) $item.oldHashes))) { throw "Backup verification failed; no restore attempted: $name" }
        $current=Get-InstallerHashes $dest
        $unchanged=($item.existed -and (Test-Path -LiteralPath $dest) -and (Equal-InstallerHashes $current $item.oldHashes)) -or (-not $item.existed -and -not (Test-Path -LiteralPath $dest))
        if ($unchanged) { continue }
        if (-not $item.touched) { throw "An untouched plugin changed externally; restore refused: $name" }
        # Exact candidate, or partial known old/new files from an interrupted
        # transaction, are recoverable. Unknown later edits are preserved.
        if (-not (Equal-InstallerHashes $current $item.newHashes -Subset) -and -not (Equal-InstallerHashes $current $item.oldHashes -Subset)) { throw "Target changed after installation; preserve it and resolve manually: $name" }
    }
    Assert-InstallerIdle
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    foreach ($item in $Journal.items) {
        if (-not $item.touched) { continue }
        $name="Just_$($item.slug).vst3"; $dest=Join-Path $target $name
        $current=Get-InstallerHashes $dest
        if ($item.existed -and (Test-Path -LiteralPath $dest) -and (Equal-InstallerHashes $current $item.oldHashes)) { continue }
        if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
        if ($item.existed) {
            Copy-Item -LiteralPath (Join-Path $Backup "previous\$name") -Destination $target -Recurse -Force
            if (-not (Equal-InstallerHashes (Get-InstallerHashes $dest) $item.oldHashes)) { throw "Restored backup hash mismatch: $name" }
        }
    }
    $Journal.status='RESTORED'; Write-InstallerJson $Journal (Join-Path $Backup 'installer-transaction.json')
}

try {
    if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Run in native Windows x64 PowerShell. No software is installed automatically.' }
    $ReportPath=Full-InstallerPath $ReportPath
    Assert-InstallerIdle
    if ($Action -eq 'Restore') {
        $script:Backup=Full-InstallerPath $BackupDir
        $script:Journal=Get-Content -LiteralPath (Join-Path $script:Backup 'installer-transaction.json') -Raw | ConvertFrom-Json
        Assert-InstallerSeparate $ReportPath (Full-InstallerPath $script:Journal.destination)
        Assert-InstallerSeparate $ReportPath $script:Backup
        $script:CanReport=$true
        Lock-InstallerDestination (Full-InstallerPath $script:Journal.destination)
        Restore-InstallerTransaction $script:Journal $script:Backup
        $script:Report.status='RESTORED'; $script:Report.backup=$script:Backup; $script:Report.selected=@($script:Journal.items | ForEach-Object { $_.slug })
    } else {
        $candidate=Full-InstallerPath $CandidateDir; $target=Full-InstallerPath $Destination; $backups=Full-InstallerPath $BackupRoot
        Assert-InstallerSeparate $candidate $target; Assert-InstallerSeparate $candidate $backups; Assert-InstallerSeparate $target $backups
        Assert-InstallerSeparate $ReportPath $candidate; Assert-InstallerSeparate $ReportPath $target; Assert-InstallerSeparate $ReportPath $backups
        $script:CanReport=$true
        Lock-InstallerDestination $target
        if (-not (Test-Path -LiteralPath $candidate -PathType Container) -or (Test-Path -LiteralPath $target -PathType Leaf)) { throw 'Candidate and destination must be directories.' }
        $manifest=Read-InstallerCandidate $candidate
        $selection=@(if ($Selected -eq 'all') { $script:Slugs } else { $Selected.Split(',') | ForEach-Object { $_.Trim().ToLowerInvariant() } })
        if ($selection.Count -lt 1 -or @($selection | Where-Object { $_ -notin $script:Slugs }).Count -or @($selection | Select-Object -Unique).Count -ne $selection.Count) { throw 'Select all or a comma-separated list of unique known plugin slugs.' }
        $script:Report.selected=$selection
        Assert-InstallerTree $target
        if ($TestFailAfter -ne 0) {
            $testRoot=Full-InstallerPath $IsolationTestRoot; $temp=Full-InstallerPath ([IO.Path]::GetTempPath())
            if ($TestFailAfter -lt 1 -or -not $testRoot.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Failure injection is limited to a dedicated temporary test root.' }
            foreach ($path in @($candidate,$target,$backups,$ReportPath)) { if (-not $path.StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Failure injection requires every path inside its temporary test root.' } }
        }
        $script:Backup=Join-Path $backups ('JUST-0.1.0-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N'))
        $script:Report.backup=$script:Backup
        New-Item -ItemType Directory -Path (Join-Path $script:Backup 'previous') -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $script:Backup 'staged') -Force | Out-Null
        $items=@()
        foreach ($slug in $selection) {
            $name="Just_$slug.vst3"; $dest=Join-Path $target $name; $exists=Test-Path -LiteralPath $dest; $old=Get-InstallerHashes $dest
            if ($exists) {
                Copy-Item -LiteralPath $dest -Destination (Join-Path $script:Backup 'previous') -Recurse -Force
                if (-not (Equal-InstallerHashes (Get-InstallerHashes (Join-Path $script:Backup "previous\$name")) $old)) { throw "Backup SHA256 verification failed: $name. Nothing installed." }
            }
            $new=Get-InstallerHashes (Join-Path $candidate "VST3\$name")
            Copy-Item -LiteralPath (Join-Path $candidate "VST3\$name") -Destination (Join-Path $script:Backup 'staged') -Recurse -Force
            if (-not (Equal-InstallerHashes (Get-InstallerHashes (Join-Path $script:Backup "staged\$name")) $new)) { throw "Staged SHA256 mismatch: $name" }
            $items+=@{ slug=$slug; existed=$exists; oldHashes=$old; newHashes=$new; touched=$false }
        }
        $script:Journal=@{ schema=1; version='0.1.0'; status='BACKUP_VERIFIED'; destination=$target; source=$candidate; items=$items; utc=[DateTime]::UtcNow.ToString('o'); scope='Selected VST3 bundles only; no presets, settings, licenses or projects' }
        Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $script:Backup 'Installer-Engine.ps1')
        Save-InstallerJournal
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        $completed=0
        foreach ($item in $items) {
            Assert-InstallerIdle
            $name="Just_$($item.slug).vst3"; $dest=Join-Path $target $name
            if (-not (Equal-InstallerHashes (Get-InstallerHashes (Join-Path $script:Backup "staged\$name")) $item.newHashes)) { throw "Staged candidate changed before replacement: $name" }
            if ($item.existed -and -not (Equal-InstallerHashes (Get-InstallerHashes (Join-Path $script:Backup "previous\$name")) $item.oldHashes)) { throw "Backup changed before replacement: $name" }
            $current=Get-InstallerHashes $dest
            if ((Test-Path -LiteralPath $dest) -ne $item.existed -or -not (Equal-InstallerHashes $current $item.oldHashes)) { throw "Target changed after backup; refused: $name" }
            $item.touched=$true; $script:Journal.status='INSTALLING'; Save-InstallerJournal; $script:Changed=$true
            if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
            Copy-Item -LiteralPath (Join-Path $script:Backup "staged\$name") -Destination $target -Recurse -Force
            if (-not (Equal-InstallerHashes (Get-InstallerHashes $dest) $item.newHashes)) { throw "Installed SHA256 mismatch: $name" }
            ++$completed
            if ($TestFailAfter -eq $completed) { throw 'Intentional isolated installer test failure.' }
        }
        $script:Journal.status='INSTALLED'; Save-InstallerJournal
        $script:Report.status='INSTALLED'
    }
    Write-InstallerJson $script:Report $ReportPath
    Write-Output "$($script:Report.status): $($script:Report.selected -join ','). Backup: $($script:Report.backup)"
    Write-Output 'Unsigned Windows preview. User REAPER visual/audio acceptance has not been performed.'
    exit 0
} catch {
    $script:Report.error=$_.Exception.Message
    $script:Report.status='REFUSED'
    if ($Action -eq 'Install' -and $script:Changed -and $null -ne $script:Journal) {
        try { Restore-InstallerTransaction $script:Journal $script:Backup; $script:Report.status='ROLLED_BACK' }
        catch { $script:Report.status='ROLLBACK_BLOCKED'; $script:Report.error+=' | Rollback: '+$_.Exception.Message }
    }
    try { if ($script:CanReport) { Write-InstallerJson $script:Report $ReportPath } } catch { Write-Warning 'Could not write installer report.' }
    Write-Error "$($script:Report.status): $($script:Report.error) Backup: $script:Backup" -ErrorAction Continue
    exit 1
} finally {
    if ($null -ne $script:InstallMutex) {
        if ($script:MutexOwned) { $script:InstallMutex.ReleaseMutex() }
        $script:InstallMutex.Dispose()
    }
}
