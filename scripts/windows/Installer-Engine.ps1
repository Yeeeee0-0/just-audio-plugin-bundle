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
    [string]$ReportPath,
    [int]$TestFailAfter=0,
    [string]$IsolationTestRoot,
    [string]$BindingPath,
    [string]$ExpectedBindingSHA256,
    [string]$TestRestoreBoundarySlug,
    [string]$SetupInvocationId='',
    [switch]$LibraryOnly
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
$script:Report=[ordered]@{ schema=2; action=$Action; status='PREFLIGHT'; version='0.1.0'; vendor='Yee Huang'; signed=$false; userReaperValidation='NOT_RUN'; selected=@(); backup=''; backupDirectory=''; transactionId=''; setupInvocationId=$SetupInvocationId; setupInvocationScope=if ($SetupInvocationId) {'SETUP_INVOCATION'} else {'RAW_ENGINE_NO_SETUP_INVOCATION'}; candidateManifestSHA256=''; engineSHA256=''; itemsStableIdentitySHA256=''; testRestoreBoundary=$null; availabilityChecks=@(); replacementStages=@(); restoreCompleted=@(); error=''; utc=[DateTime]::UtcNow.ToString('o') }

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
# Schema 2 distinguishes an absent bundle, a file, an empty directory and all
# child directory names. File-only maps remain in use for candidate manifests.
function Assert-InstallerRelative([string]$Name) {
    if ([string]::IsNullOrEmpty($Name) -or $Name -notmatch '^[^\\:]+$' -or $Name.StartsWith('/') -or @($Name.Split('/') | Where-Object { $_ -eq '..' -or $_ -eq '.' -or $_ -eq '' -or $_ -match '[. ]$' -or $_ -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(?:\.|$)' }).Count) { throw 'Unsafe transaction relative path.' }
}
function Get-InstallerState([string]$Path) {
    $full=Full-InstallerPath $Path
    $state=[ordered]@{ exists=$false; type='absent'; dirs=@(); hashes=[ordered]@{} }
    if (-not (Test-Path -LiteralPath $full)) { return $state }
    $root=Get-Item -LiteralPath $full -Force
    if (($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse bundle root refused.' }
    $state.exists=$true
    if (-not $root.PSIsContainer) { $state.type='file'; $state.hashes['$file']=(Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash.ToLowerInvariant(); return $state }
    $state.type='directory'; $base=$root.FullName.TrimEnd('\')+'\'
    $dirs=New-Object Collections.Generic.List[string]; $files=@{}; $seen=@{}
    $pending=New-Object Collections.Generic.Stack[string]; $pending.Push($full)
    while ($pending.Count) {
        $folder=$pending.Pop()
        # Inspect each entry before recursion; do not traverse reparse children.
        foreach ($entry in @(Get-ChildItem -LiteralPath $folder -Force)) {
            if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse transaction entry refused: $($entry.FullName)" }
            $relative=$entry.FullName.Substring($base.Length).Replace('\','/'); Assert-InstallerRelative $relative
            if ($seen.ContainsKey($relative)) { throw 'Case-colliding transaction entry refused.' }; $seen[$relative]=$true
            if ($entry.PSIsContainer) { $dirs.Add($relative); $pending.Push($entry.FullName) }
            else { $files[$relative]=(Get-FileHash -LiteralPath $entry.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
        }
    }
    $sorted=$dirs.ToArray(); [Array]::Sort($sorted,[StringComparer]::OrdinalIgnoreCase); $state.dirs=@($sorted)
    $keys=[string[]]@($files.Keys); [Array]::Sort($keys,[StringComparer]::OrdinalIgnoreCase)
    foreach ($key in $keys) { $state.hashes[$key]=$files[$key] }
    return $state
}
function Convert-InstallerState($Object) {
    if ($Object.exists -isnot [bool] -or $Object.type -notin @('absent','file','directory')) { throw 'Invalid transaction root existence/type.' }
    $hashes=Convert-InstallerHashes $Object.hashes; $dirs=@(); $seen=@{}
    foreach ($entry in @($Object.dirs)) { $name=[string]$entry; Assert-InstallerRelative $name; if ($seen.ContainsKey($name) -or $hashes.ContainsKey($name)) { throw 'Duplicate transaction directory/type.' }; $seen[$name]=$true; $dirs+=$name }
    if (-not $Object.exists) { if ($Object.type -ne 'absent' -or $dirs.Count -or $hashes.Count) { throw 'Absent transaction state contains entries.' } }
    elseif ($Object.type -eq 'file') { if ($dirs.Count -or $hashes.Count -ne 1 -or -not $hashes.ContainsKey('$file')) { throw 'Invalid file transaction state.' } }
    elseif ($Object.type -eq 'directory') {
        if ($hashes.ContainsKey('$file')) { throw 'File marker in directory transaction state.' }
        foreach ($name in @($dirs)+@($hashes.Keys)) { $slash=$name.LastIndexOf('/'); if ($slash -ge 0 -and -not $seen.ContainsKey($name.Substring(0,$slash))) { throw 'Transaction parent directory is missing.' } }
    } else { throw 'Existing transaction state has absent type.' }
    $sorted=[string[]]$dirs; [Array]::Sort($sorted,[StringComparer]::OrdinalIgnoreCase)
    $canonical=[ordered]@{ exists=[bool]$Object.exists; type=[string]$Object.type; dirs=@($sorted); hashes=[ordered]@{} }
    $keys=[string[]]@($hashes.Keys); [Array]::Sort($keys,[StringComparer]::OrdinalIgnoreCase); foreach ($key in $keys) { $canonical.hashes[$key]=$hashes[$key] }
    return $canonical
}
function Equal-InstallerState($Actual,$Expected,[switch]$Subset) {
    $a=Convert-InstallerState $Actual; $e=Convert-InstallerState $Expected
    if ($a.exists -ne $e.exists -or $a.type -cne $e.type) { return $false }
    if (-not $Subset -and $a.dirs.Count -ne $e.dirs.Count) { return $false }
    foreach ($dir in $a.dirs) { if ($dir -cnotin $e.dirs) { return $false } }
    return (Equal-InstallerHashes $a.hashes $e.hashes -Subset:$Subset)
}
function Get-InstallerJsonSHA256($Object) {
    $json=ConvertTo-Json -InputObject $Object -Depth 30 -Compress
    $sha=[Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Read-InstallerJournalSnapshot([string]$Path) {
    $full=Full-InstallerPath $Path
    # One immutable raw byte snapshot supplies both authorization metadata and
    # its digest. A later disk version cannot become the first hash baseline.
    $bytes=[IO.File]::ReadAllBytes($full)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $digest=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $text=[Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff)
    $journal=$text | ConvertFrom-Json
    return [ordered]@{ journal=$journal; sha256=$digest }
}
function Get-InstallerItemsStableData($Journal) {
    $stable=@(); $seen=@{}
    foreach ($item in @($Journal.items)) {
        if ($item.slug -notin $script:Slugs -or $seen.ContainsKey($item.slug) -or $item.existed -isnot [bool] -or $item.touched -isnot [bool]) { throw 'Invalid transaction selected item.' }; $seen[$item.slug]=$true
        $old=Convert-InstallerState $item.oldState; $new=Convert-InstallerState $item.newState
        if ($old.exists -ne $item.existed -or -not $new.exists -or $new.type -ne 'directory' -or $new.hashes.Count -lt 1) { throw 'Invalid transaction old/new identity.' }
        $stable+=,[ordered]@{ slug=[string]$item.slug; existed=[bool]$item.existed; oldState=$old; newState=$new }
    }
    if ($stable.Count -lt 1 -or $stable.Count -gt 10 -or (($Journal.selected -join ',') -cne (($stable | ForEach-Object { $_.slug }) -join ','))) { throw 'Transaction selection/order mismatch.' }
    return ,$stable
}
function Get-InstallerItemsStableIdentity($Journal) { return (Get-InstallerJsonSHA256 (Get-InstallerItemsStableData $Journal)) }
function Get-InstallerStableContext($Journal,[string]$Backup) {
    if ($Journal.schema -ne 2 -or $Journal.version -ne '0.1.0' -or $Journal.status -notin @('BACKUP_VERIFIED','INSTALLING','INSTALLED','RESTORED')) { throw 'This engine requires a valid schema 2 transaction; older transactions use their original engine.' }
    $backupPath=Full-InstallerPath $Backup; $destinationPath=Full-InstallerPath ([string]$Journal.destination)
    if ([string]$Journal.backupDirectory -cne $backupPath -or [string]$Journal.transactionId -cne (Split-Path -Leaf $backupPath)) { throw 'Transaction directory/id mismatch.' }
    # Actual Setup supplies its private invocation nonce. Raw isolated engine
    # calls may omit it, but cannot claim to originate from a Setup invocation.
    if ($Journal.setupInvocationId -isnot [string] -or ($Journal.setupInvocationId -ne '' -and $Journal.setupInvocationId -cnotmatch '^[0-9a-f]{32}$')) { throw 'Invalid Setup invocation identity.' }
    if ($Journal.candidateManifestSHA256 -notmatch '^[0-9a-f]{64}$' -or $Journal.engineSHA256 -notmatch '^[0-9a-f]{64}$' -or $Journal.itemsStableIdentitySHA256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid stable transaction SHA256.' }
    $items=Get-InstallerItemsStableData $Journal; $identity=Get-InstallerJsonSHA256 $items
    if ($identity -cne $Journal.itemsStableIdentitySHA256) { throw 'Stable transaction items identity mismatch.' }
    return [ordered]@{ schema=2; transactionId=[string]$Journal.transactionId; setupInvocationId=[string]$Journal.setupInvocationId; backupDirectory=$backupPath; journalPath=(Join-Path $backupPath 'installer-transaction.json'); destination=$destinationPath; version='0.1.0'; candidateManifestSHA256=[string]$Journal.candidateManifestSHA256; engineSHA256=[string]$Journal.engineSHA256; selected=@($Journal.selected); itemsStableIdentitySHA256=$identity; items=$items }
}
function Assert-InstallerRestoreBinding($Context,[string]$Path,[string]$ExpectedSHA256) {
    if (-not $Path) { if ($ExpectedSHA256) { throw 'Binding digest requires a binding file.' }; return }
    $fixed=Join-Path $Context.backupDirectory 'transaction-binding.json'
    if ((Full-InstallerPath $Path) -cne $fixed -or $ExpectedSHA256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Restore binding must be the fixed file in its original transaction directory.' }
    if ((Get-FileHash -LiteralPath $fixed -Algorithm SHA256).Hash -ine $ExpectedSHA256) { throw 'Restore binding changed or is missing.' }
    $binding=Get-Content -LiteralPath $fixed -Raw | ConvertFrom-Json
    foreach ($field in @('schema','transactionId','setupInvocationId','backupDirectory','journalPath','destination','version','candidateManifestSHA256','engineSHA256','itemsStableIdentitySHA256')) { if ([string]$binding.$field -cne [string]$Context[$field]) { throw "Restore binding field mismatch: $field" } }
    if (($binding.selected -join ',') -cne ($Context.selected -join ',') -or (Get-InstallerJsonSHA256 @($binding.items)) -cne (Get-InstallerJsonSHA256 $Context.items)) { throw 'Restore binding full items/selection mismatch.' }
}
function New-InstallerReplacement([string]$Source,$Expected,[string]$Target) {
    $sourcePath=Full-InstallerPath $Source; $targetPath=Full-InstallerPath $Target
    $state=Convert-InstallerState $Expected
    if (-not $state.exists -or -not (Equal-InstallerState (Get-InstallerState $sourcePath) $state)) { throw 'Replacement source does not match its full recorded state.' }
    # Stage beside selected bundle roots on the destination volume, so a C:
    # target with D: backups is supported. Never force-overwrite a new target.
    $stageRoot=Join-Path $targetPath ('.JUST-replacement-'+[Guid]::NewGuid().ToString('N'))
    $null=Full-InstallerPath $stageRoot; Assert-InstallerSeparate $sourcePath $stageRoot
    New-Item -ItemType Directory -Path $stageRoot | Out-Null
    $entry=[ordered]@{ directory=$stageRoot; leaf=(Join-Path $stageRoot (Split-Path -Leaf $sourcePath)); status='PREPARING'; utc=[DateTime]::UtcNow.ToString('o') }
    $script:Report.replacementStages+=,$entry
    Copy-Item -LiteralPath $sourcePath -Destination $stageRoot -Recurse
    if (-not (Equal-InstallerState (Get-InstallerState $entry.leaf) $state) -or @(Get-ChildItem -LiteralPath $stageRoot -Force).Count -ne 1) { throw 'Replacement stage changed; preserve it and refuse the replacement.' }
    $entry.status='VERIFIED'; return $entry
}
function Assert-InstallerReplacement($Stage,$Expected) {
    $state=Convert-InstallerState $Expected
    $null=Full-InstallerPath $Stage.directory; $null=Full-InstallerPath $Stage.leaf
    if (-not (Equal-InstallerState (Get-InstallerState $Stage.leaf) $state) -or @(Get-ChildItem -LiteralPath $Stage.directory -Force).Count -ne 1) { throw 'Replacement stage changed; refuse the replacement and preserve it.' }
}
function Commit-InstallerReplacement($Stage,[string]$Destination,$Expected) {
    $dest=Full-InstallerPath $Destination; $state=Convert-InstallerState $Expected
    Assert-InstallerReplacement $Stage $state
    if (Test-Path -LiteralPath $dest) { throw 'An external destination appeared; preserve it and the replacement stage.' }
    # These same-volume move APIs refuse any existing destination. Unlike
    # recursive copy, they cannot merge into or replace a newly created tree.
    if ($state.type -eq 'directory') { [IO.Directory]::Move($Stage.leaf,$dest) }
    else { [IO.File]::Move($Stage.leaf,$dest) }
    $Stage.status='COMMITTED'
    # Remove only our now-empty stage directory. Unknown additions are kept.
    try { [IO.Directory]::Delete($Stage.directory,$false); $Stage.status='COMMITTED_EMPTY_STAGE_REMOVED' }
    catch { $Stage.status='COMMITTED_STAGE_RETAINED'; Write-Warning 'Replacement stage contains later content or cannot be removed; it has been preserved.' }
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
function Initialize-InstallerFileAvailability {
    if ('JustInstallerFileAvailabilityV1' -as [type]) { return }
    $clrDirectory=[Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()
    $clrReferences=@([object].Assembly.Location,(Join-Path $clrDirectory 'System.dll'),(Join-Path $clrDirectory 'System.Core.dll'))
    foreach ($reference in $clrReferences) { if (-not [IO.Path]::IsPathRooted($reference) -or -not (Test-Path -LiteralPath $reference -PathType Leaf)) { throw 'The trusted CLR compiler reference is unavailable.' } }
    Add-Type -ReferencedAssemblies $clrReferences -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public sealed class JustInstallerAvailabilityResultV1 {
    public string Path;
    public bool Directory;
    public bool Available;
    public int Win32Error;
    public int HResult;
    public string Message;
    public uint DesiredAccess;
    public uint ShareMode;
}
public static class JustInstallerFileAvailabilityV1 {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    public static JustInstallerAvailabilityResultV1 Probe(string path, bool directory) {
        uint access = directory ? 0x00010000u : 0xC0010000u;
        uint flags = directory ? 0x02000000u : 0x00000080u;
        using (SafeFileHandle handle=CreateFileW(path, access, 7u, IntPtr.Zero, 3u, flags, IntPtr.Zero)) {
            int error=handle.IsInvalid ? Marshal.GetLastWin32Error() : 0;
            return new JustInstallerAvailabilityResultV1 { Path=path, Directory=directory, Available=!handle.IsInvalid, Win32Error=error, HResult=error==0 ? 0 : unchecked((int)(0x80070000u | (uint)error)), Message=error==0 ? "Available" : new Win32Exception(error).Message, DesiredAccess=access, ShareMode=7u };
        }
    }
}
"@ -ErrorAction Stop
}
function Assert-InstallerTreeAvailability([string]$Root,[string]$Phase) {
    $full=Full-InstallerPath $Root
    if (-not (Test-Path -LiteralPath $full)) {
        $script:Report.availabilityChecks+=@([ordered]@{ phase=$Phase; root=$full; status='ABSENT'; files=0; directories=0; utc=[DateTime]::UtcNow.ToString('o') })
        return
    }
    Assert-InstallerTree $full
    Initialize-InstallerFileAvailability
    $entries=@(Get-Item -LiteralPath $full -Force)+@(if (Test-Path -LiteralPath $full -PathType Container) { Get-ChildItem -LiteralPath $full -Force -Recurse })
    $files=0; $directories=0
    foreach ($entry in $entries) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse entry appeared during availability preflight.' }
        $null=Full-InstallerPath $entry.FullName
        $probe=[JustInstallerFileAvailabilityV1]::Probe($entry.FullName,[bool]$entry.PSIsContainer)
        if (-not $probe.Available) {
            $detail=[ordered]@{ phase=$Phase; root=$full; status='REFUSED'; path=$probe.Path; directory=$probe.Directory; win32Error=$probe.Win32Error; hresult=$probe.HResult; hresultHex=('0x{0:X8}' -f $probe.HResult); message=$probe.Message; desiredAccess=$probe.DesiredAccess; shareMode=$probe.ShareMode; method='CreateFileW_OPEN_EXISTING_NO_WRITE'; utc=[DateTime]::UtcNow.ToString('o') }
            $script:Report.availabilityChecks+=@($detail)
            throw "Selected plugin is unavailable for replacement. Path: $($probe.Path). Win32Error: $($probe.Win32Error). HRESULT: $($detail.hresultHex). $($probe.Message)"
        }
        if ($entry.PSIsContainer) { ++$directories } else { ++$files }
    }
    $script:Report.availabilityChecks+=@([ordered]@{ phase=$Phase; root=$full; status='AVAILABLE'; files=$files; directories=$directories; method='CreateFileW_OPEN_EXISTING_NO_WRITE'; atomicExclusion=$false; utc=[DateTime]::UtcNow.ToString('o') })
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
function Assert-InstallerJournalUnchanged($Context,[string]$JournalSHA256) {
    $currentSnapshot=Read-InstallerJournalSnapshot $Context.journalPath
    if ($currentSnapshot.sha256 -cne $JournalSHA256) { throw 'Transaction journal changed during restore; preserve remaining targets.' }
    $currentJournal=$currentSnapshot.journal
    $currentContext=Get-InstallerStableContext $currentJournal $Context.backupDirectory
    if ((Get-InstallerJsonSHA256 $currentContext) -cne (Get-InstallerJsonSHA256 $Context)) { throw 'Stable transaction context changed during restore.' }
    if ((Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash -ine $Context.engineSHA256) { throw 'Executing engine does not match the transaction engine identity.' }
    Assert-InstallerRestoreBinding $Context $BindingPath $ExpectedBindingSHA256
}
function Test-InstallerRestoreItem($Item,[string]$Backup,[string]$Target,[bool]$AlreadyRestored=$false) {
    $name="Just_$($Item.slug).vst3"; $dest=Join-Path $Target $name; $previous=Join-Path $Backup "previous\$name"
    $old=Convert-InstallerState $Item.oldState; $new=Convert-InstallerState $Item.newState
    if (-not (Equal-InstallerState (Get-InstallerState $previous) $old)) { throw "Backup full-state verification failed; no replacement authorized: $name" }
    $current=Get-InstallerState $dest
    if (Equal-InstallerState $current $old) { return [ordered]@{ state='ORIGINAL'; current=$current; needsChange=$false } }
    if ($AlreadyRestored) { throw "Previously restored transaction found later changes; only exact original full state is an idempotent no-op: $name" }
    if (-not $Item.touched) { throw "An untouched plugin changed externally; restore refused: $name" }
    # Explicit absence is recoverable only for an item this transaction touched.
    # Initial installs are idempotently absent; upgrades restore verified originals.
    if (-not $current.exists) { return [ordered]@{ state='ABSENT'; current=$current; needsChange=[bool]$Item.existed } }
    $hasKnownEntry=($current.hashes.Count -gt 0 -or $current.dirs.Count -gt 0)
    if ($hasKnownEntry -and ((Equal-InstallerState $current $new -Subset) -or ($old.exists -and (Equal-InstallerState $current $old -Subset)))) { return [ordered]@{ state='KNOWN_CURRENT_OR_PARTIAL'; current=$current; needsChange=$true } }
    throw "Target full state changed after installation (unknown bytes/file/directory/type); preserve it: $name"
}
function Invoke-InstallerRestoreBoundaryHook($Journal,[string]$Backup) {
    if (-not $TestRestoreBoundarySlug) { return }
    if ($Action -ne 'Restore' -or $TestRestoreBoundarySlug -notin @($Journal.selected) -or -not $IsolationTestRoot) { throw 'Restore boundary injection requires a selected slug and explicit isolated test root.' }
    $isolation=Full-InstallerPath $IsolationTestRoot; $temp=Full-InstallerPath ([IO.Path]::GetTempPath())
    $insideTemp=$isolation.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)
    $nestedTemp=$temp.Equals((Join-Path $isolation 'temp'),[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $isolation) -cmatch '^JUST-uninstaller-tests-[0-9a-f]{32}$'
    if (-not $insideTemp -and -not $nestedTemp) { throw 'Restore boundary injection is limited to its dedicated temporary test root.' }
    $paths=@($Journal.source,$Journal.destination,$Backup,$ReportPath,(Join-Path $Backup 'installer-transaction.json'))
    if ($BindingPath) { $paths+=$BindingPath }
    foreach ($path in $paths) { $full=Full-InstallerPath ([string]$path); if (-not $full.StartsWith($isolation+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Every restore boundary test path must remain inside its isolated temporary root.' } }
    $bundle=Join-Path $Journal.destination "Just_$TestRestoreBoundarySlug.vst3"
    if (-not (Test-Path -LiteralPath $bundle -PathType Container)) { throw 'Restore boundary test requires an existing selected directory bundle.' }
    $fixed=Join-Path $bundle '.resume25-boundary-unknown-empty'
    if (Test-Path -LiteralPath $fixed) { throw 'Restore boundary fixture already exists; refusing repeated injection.' }
    $null=Full-InstallerPath $fixed; New-Item -ItemType Directory -Path $fixed | Out-Null
    $script:Report.testRestoreBoundary=[ordered]@{ slug=$TestRestoreBoundarySlug; createdDirectory=$fixed; utc=[DateTime]::UtcNow.ToString('o'); phase='AFTER_ALL_PREFLIGHT_BEFORE_SELECTED_DELETE_GUARD'; mutation='FIXED_UNKNOWN_EMPTY_DIRECTORY' }
}
function Restore-InstallerTransaction($Journal,[string]$Backup,[string]$JournalSnapshotSHA256='') {
    $context=Get-InstallerStableContext $Journal $Backup
    $target=$context.destination; Assert-InstallerSeparate $target $Backup
    if (Test-Path -LiteralPath $target -PathType Leaf) { throw 'Restore destination must be a directory.' }
    Assert-InstallerTree $target; Assert-InstallerTree $Backup
    if ($JournalSnapshotSHA256) { $journalSHA256=$JournalSnapshotSHA256 }
    else {
        # Internal install rollback starts from in-memory metadata. Cross-check
        # stable fields and every mutable authorization flag with one snapshot.
        $snapshot=Read-InstallerJournalSnapshot $context.journalPath
        $snapshotContext=Get-InstallerStableContext $snapshot.journal $Backup
        $memoryAuthority=[ordered]@{ context=$context; status=[string]$Journal.status; touched=@($Journal.items | ForEach-Object { [bool]$_.touched }) }
        $snapshotAuthority=[ordered]@{ context=$snapshotContext; status=[string]$snapshot.journal.status; touched=@($snapshot.journal.items | ForEach-Object { [bool]$_.touched }) }
        if ((Get-InstallerJsonSHA256 $memoryAuthority) -cne (Get-InstallerJsonSHA256 $snapshotAuthority)) { throw 'Initial rollback journal snapshot differs from in-memory authorization.' }
        $journalSHA256=$snapshot.sha256
    }
    Assert-InstallerJournalUnchanged $context $journalSHA256
    $alreadyRestored=($Journal.status -eq 'RESTORED')
    foreach ($item in $Journal.items) { $null=Test-InstallerRestoreItem $item $Backup $target $alreadyRestored }
    Assert-InstallerIdle
    if ($alreadyRestored) {
        Assert-InstallerJournalUnchanged $context $journalSHA256
        foreach ($item in $Journal.items) { $script:Report.restoreCompleted+=@{ slug=$item.slug; result='ALREADY_RESTORED_EXACT_ORIGINAL_NO_OP' } }
        return
    }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    foreach ($item in $Journal.items) {
        if ($TestRestoreBoundarySlug -and $item.slug -eq $TestRestoreBoundarySlug) { Invoke-InstallerRestoreBoundaryHook $Journal $Backup }
        Assert-InstallerIdle
        Assert-InstallerJournalUnchanged $context $journalSHA256
        $decision=Test-InstallerRestoreItem $item $Backup $target $alreadyRestored
        if (-not $decision.needsChange) { $script:Report.restoreCompleted+=@{ slug=$item.slug; result='ALREADY_ORIGINAL_OR_ABSENT' }; continue }
        $name="Just_$($item.slug).vst3"; $dest=Join-Path $target $name
        $replacement=$null
        if ($item.existed) { $replacement=New-InstallerReplacement (Join-Path $Backup "previous\$name") $item.oldState $target }
        Assert-InstallerIdle
        Assert-InstallerJournalUnchanged $context $journalSHA256
        if ($replacement) { Assert-InstallerReplacement $replacement $item.oldState }
        $decision=Test-InstallerRestoreItem $item $Backup $target $alreadyRestored
        if (-not $decision.needsChange) { $script:Report.restoreCompleted+=@{ slug=$item.slug; result='ORIGINAL_APPEARED_DURING_PREPARATION_STAGE_RETAINED' }; continue }
        # Checks directly above authorize only this known tree. External writers
        # are not mutex-bound; this is not an atomic filesystem exclusion claim.
        Assert-InstallerTreeAvailability $dest 'BEFORE_RESTORE_REPLACEMENT'
        if ($decision.current.exists) { Remove-Item -LiteralPath $dest -Recurse -Force }
        if ($item.existed) {
            Commit-InstallerReplacement $replacement $dest $item.oldState
        }
        if (-not (Equal-InstallerState (Get-InstallerState $dest) $item.oldState)) { throw "Restored full-state mismatch: $name" }
        $script:Report.restoreCompleted+=@{ slug=$item.slug; result='ORIGINAL_FULL_STATE_RESTORED' }
    }
    Assert-InstallerJournalUnchanged $context $journalSHA256
    $Journal.status='RESTORED'; Write-InstallerJson $Journal $context.journalPath
}

# Importing this library only defines helpers; it does not install or restore.
if ($LibraryOnly) { return }

try {
    if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Run in native Windows x64 PowerShell. No software is installed automatically.' }
    $ReportPath=Full-InstallerPath $ReportPath
    if ($SetupInvocationId -and $SetupInvocationId -cnotmatch '^[0-9a-f]{32}$') { throw 'SetupInvocationId must be a lowercase 32-hex invocation nonce.' }
    if ($Action -eq 'Install' -and ($BindingPath -or $ExpectedBindingSHA256 -or $TestRestoreBoundarySlug)) { throw 'Restore-only binding/test arguments are not accepted for Install.' }
    Assert-InstallerIdle
    if ($Action -eq 'Restore') {
        $script:Backup=Full-InstallerPath $BackupDir
        $initialJournalSnapshot=Read-InstallerJournalSnapshot (Join-Path $script:Backup 'installer-transaction.json')
        $script:Journal=$initialJournalSnapshot.journal
        Assert-InstallerSeparate $ReportPath (Full-InstallerPath $script:Journal.destination)
        Assert-InstallerSeparate $ReportPath $script:Backup
        $script:CanReport=$true
        Lock-InstallerDestination (Full-InstallerPath $script:Journal.destination)
        $context=Get-InstallerStableContext $script:Journal $script:Backup
        $script:Report.backupDirectory=$context.backupDirectory; $script:Report.transactionId=$context.transactionId
        $script:Report.setupInvocationId=$context.setupInvocationId; $script:Report.setupInvocationScope=if ($context.setupInvocationId) {'SETUP_INVOCATION'} else {'RAW_ENGINE_NO_SETUP_INVOCATION'}
        $script:Report.candidateManifestSHA256=$context.candidateManifestSHA256; $script:Report.engineSHA256=$context.engineSHA256; $script:Report.itemsStableIdentitySHA256=$context.itemsStableIdentitySHA256
        Restore-InstallerTransaction $script:Journal $script:Backup $initialJournalSnapshot.sha256
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
        foreach ($slug in $selection) { Assert-InstallerTreeAvailability (Join-Path $target "Just_$slug.vst3") 'BEFORE_BACKUP' }
        $script:Backup=Join-Path $backups ('JUST-0.1.0-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N'))
        $script:Report.backup=$script:Backup
        $script:Report.backupDirectory=$script:Backup
        New-Item -ItemType Directory -Path (Join-Path $script:Backup 'previous') -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $script:Backup 'staged') -Force | Out-Null
        $items=@()
        foreach ($slug in $selection) {
            $name="Just_$slug.vst3"; $dest=Join-Path $target $name; $oldState=Get-InstallerState $dest; $exists=[bool]$oldState.exists; $old=$oldState.hashes
            if ($exists) {
                Copy-Item -LiteralPath $dest -Destination (Join-Path $script:Backup 'previous') -Recurse -Force
                if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "previous\$name")) $oldState)) { throw "Backup full-state verification failed: $name. Nothing installed." }
            }
            $newState=Get-InstallerState (Join-Path $candidate "VST3\$name"); $new=$newState.hashes
            Copy-Item -LiteralPath (Join-Path $candidate "VST3\$name") -Destination (Join-Path $script:Backup 'staged') -Recurse -Force
            if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "staged\$name")) $newState)) { throw "Staged full-state mismatch: $name" }
            $items+=@{ slug=$slug; existed=$exists; oldHashes=$old; newHashes=$new; oldState=$oldState; newState=$newState; touched=$false }
        }
        $script:Journal=@{ schema=2; version='0.1.0'; status='BACKUP_VERIFIED'; transactionId=(Split-Path -Leaf $script:Backup); setupInvocationId=$SetupInvocationId; backupDirectory=$script:Backup; destination=$target; source=$candidate; candidateManifestSHA256=(Get-FileHash -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant(); engineSHA256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant(); selected=$selection; items=$items; itemsStableIdentitySHA256=''; utc=[DateTime]::UtcNow.ToString('o'); scope='Selected VST3 bundles only; no presets, settings, licenses or projects'; restorePolicy='Exact original/candidate or known same-type partial trees only; touched absent upgrades recover verified original; absent initial installs are idempotent; unknown bytes/files/directories/types/reparse are refused. Per-item rechecks are not atomic exclusion of external writers.' }
        $script:Journal.itemsStableIdentitySHA256=Get-InstallerItemsStableIdentity $script:Journal
        $script:Report.transactionId=$script:Journal.transactionId; $script:Report.candidateManifestSHA256=$script:Journal.candidateManifestSHA256; $script:Report.engineSHA256=$script:Journal.engineSHA256; $script:Report.itemsStableIdentitySHA256=$script:Journal.itemsStableIdentitySHA256
        Copy-Item -LiteralPath $PSCommandPath -Destination (Join-Path $script:Backup 'Installer-Engine.ps1')
        if ((Get-FileHash -LiteralPath (Join-Path $script:Backup 'Installer-Engine.ps1') -Algorithm SHA256).Hash -ine $script:Journal.engineSHA256) { throw 'Saved engine identity mismatch.' }
        Save-InstallerJournal
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        $completed=0
        foreach ($item in $items) {
            Assert-InstallerIdle
            $name="Just_$($item.slug).vst3"; $dest=Join-Path $target $name
            if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "staged\$name")) $item.newState)) { throw "Staged candidate changed before replacement: $name" }
            if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "previous\$name")) $item.oldState)) { throw "Backup changed before replacement: $name" }
            if (-not (Equal-InstallerState (Get-InstallerState $dest) $item.oldState)) { throw "Target changed after backup; refused: $name" }
            $replacement=New-InstallerReplacement (Join-Path $script:Backup "staged\$name") $item.newState $target
            $item.touched=$true; $script:Journal.status='INSTALLING'; Save-InstallerJournal; $script:Changed=$true
            $installContext=Get-InstallerStableContext $script:Journal $script:Backup
            $installJournalSHA=(Get-FileHash -LiteralPath $installContext.journalPath -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-InstallerJournalUnchanged $installContext $installJournalSHA
            Assert-InstallerReplacement $replacement $item.newState
            if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "previous\$name")) $item.oldState) -or -not (Equal-InstallerState (Get-InstallerState (Join-Path $script:Backup "staged\$name")) $item.newState) -or -not (Equal-InstallerState (Get-InstallerState $dest) $item.oldState)) { throw "State changed immediately before replacement: $name" }
            Assert-InstallerTreeAvailability $dest 'BEFORE_INSTALL_REPLACEMENT'
            if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
            Commit-InstallerReplacement $replacement $dest $item.newState
            if (-not (Equal-InstallerState (Get-InstallerState $dest) $item.newState)) { throw "Installed full-state mismatch: $name" }
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
