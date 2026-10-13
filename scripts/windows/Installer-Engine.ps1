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
    [ValidateSet('','AfterPark','AfterCommit','ForeignCanonical','ForeignCanonicalSecond','SecondMoveFailure','CancelAfterPark','Finalize')][string]$TestSwapFailure='',
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
$script:EngineFile=$PSCommandPath
$script:JournalSHA256=''
$script:Report=[ordered]@{ schema=3; action=$Action; status='PREFLIGHT'; version='0.1.0'; vendor='Yee Huang'; signed=$false; userReaperValidation='NOT_RUN'; selected=@(); backup=''; backupDirectory=''; transactionId=''; setupInvocationId=$SetupInvocationId; setupInvocationScope=if ($SetupInvocationId) {'SETUP_INVOCATION'} else {'RAW_ENGINE_NO_SETUP_INVOCATION'}; candidateManifestSHA256=''; engineSHA256=''; itemsStableIdentitySHA256=''; testRestoreBoundary=$null; availabilityChecks=@(); bundleOperations=@(); bundleMoveFailure=$null; scanRoot=''; swapRoot=''; installerCompletion=''; replacementStages=@(); restoreCompleted=@(); error=''; utc=[DateTime]::UtcNow.ToString('o') }

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
function Assert-InstallerContainer([string]$Path) {
    # The shared VST3 directory is a container, not this transaction's tree.
    # Full-InstallerPath checks the container and its ancestors for reparse points.
    $full=Full-InstallerPath $Path
    if ((Test-Path -LiteralPath $full) -and -not (Test-Path -LiteralPath $full -PathType Container)) { throw 'Installer destination must be a directory.' }
}
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
# Full transaction state distinguishes an absent bundle, a file, an empty directory and all
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
function Lock-InstallerDestination([string]$Target) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Target.ToUpperInvariant())))).Replace('-','') }
    finally { $sha.Dispose() }
    $script:InstallMutex=New-Object Threading.Mutex -ArgumentList @($false,('Global\JUST-VST3-Installer-'+$key))
    try { $script:MutexOwned=$script:InstallMutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $script:MutexOwned=$true }
    if (-not $script:MutexOwned) { throw 'Another installer or restore transaction is already using this destination.' }
}
# Legacy helper retained for the approved A scope fixture only. Schema 3 never calls it.
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
# Schema 3 live-bundle operations. There is no recursive bundle deletion.
function Get-InstallerField($Object,[string]$Name,$Default=$null) {
    if ($Object -is [Collections.IDictionary]) { if ($Object.Contains($Name)) { return $Object[$Name] } }
    elseif ($null -ne $Object -and $null -ne $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $Default
}
function Initialize-InstallerBundleMoves {
    if ('JustInstallerBundleMovesV3' -as [type]) { return }
    $clrDirectory=[Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()
    $clrReferences=@([object].Assembly.Location,(Join-Path $clrDirectory 'System.dll'),(Join-Path $clrDirectory 'System.Core.dll'))
    foreach ($reference in $clrReferences) { if (-not [IO.Path]::IsPathRooted($reference) -or -not (Test-Path -LiteralPath $reference -PathType Leaf)) { throw 'The trusted CLR compiler reference is unavailable.' } }
    Add-Type -ReferencedAssemblies $clrReferences -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Text;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public sealed class JustInstallerMoveResultV3 {
    public bool Success;
    public int Win32Error;
    public int HResult;
    public string Message;
    public uint Flags;
}
public sealed class JustInstallerIdentityV3 {
    public string VolumeIdentity;
    public string RootIdentity;
}
public static class JustInstallerBundleMovesV3 {
    [StructLayout(LayoutKind.Sequential)] struct Info {
        public uint Attributes; public System.Runtime.InteropServices.ComTypes.FILETIME Created;
        public System.Runtime.InteropServices.ComTypes.FILETIME Accessed;
        public System.Runtime.InteropServices.ComTypes.FILETIME Written;
        public uint VolumeSerial, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool MoveFileExW(string source,string destination,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateDirectoryW(string path,IntPtr security);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle handle,out Info information);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetVolumePathNameW(string path,StringBuilder root,uint length);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetVolumeNameForVolumeMountPointW(string root,StringBuilder name,uint length);
    static JustInstallerMoveResultV3 Result(bool success,int error) {
        return new JustInstallerMoveResultV3 { Success=success,Win32Error=error,HResult=error==0 ? 0 : unchecked((int)(0x80070000u | (uint)error)),Message=error==0 ? "Success" : new Win32Exception(error).Message,Flags=0 };
    }
    public static JustInstallerMoveResultV3 Move(string source,string destination) {
        bool success=MoveFileExW(source,destination,0); int error=success ? 0 : Marshal.GetLastWin32Error(); return Result(success,error);
    }
    public static JustInstallerMoveResultV3 CreateDirectory(string path) {
        bool success=CreateDirectoryW(path,IntPtr.Zero); int error=success ? 0 : Marshal.GetLastWin32Error(); return Result(success,error);
    }
    public static void ReplaceMetadata(string source,string destination) { System.IO.File.Replace(source,destination,null); }
    public static JustInstallerIdentityV3 Identity(string path) {
        StringBuilder root=new StringBuilder(32768),name=new StringBuilder(32768);
        if(!GetVolumePathNameW(path,root,(uint)root.Capacity)) throw new Win32Exception(Marshal.GetLastWin32Error());
        if(!GetVolumeNameForVolumeMountPointW(root.ToString(),name,(uint)name.Capacity)) throw new Win32Exception(Marshal.GetLastWin32Error());
        using(SafeFileHandle handle=CreateFileW(path,0x80u,7u,IntPtr.Zero,3u,0x02200000u,IntPtr.Zero)) {
            if(handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            Info info; if(!GetFileInformationByHandle(handle,out info)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if((info.Attributes & 0x400u)!=0) throw new InvalidOperationException("Reparse object identity refused.");
            string volume=name.ToString().ToUpperInvariant();
            return new JustInstallerIdentityV3 {VolumeIdentity=volume,RootIdentity=volume+"|"+info.VolumeSerial.ToString("X8")+"|"+info.IndexHigh.ToString("X8")+info.IndexLow.ToString("X8")};
        }
    }
}
"@ -ErrorAction Stop
}
function Get-InstallerObjectIdentity([string]$Path) {
    $full=Full-InstallerPath $Path
    if (-not (Test-Path -LiteralPath $full)) { return '' }
    Initialize-InstallerBundleMoves
    return [JustInstallerBundleMovesV3]::Identity($full).RootIdentity
}
function Get-InstallerVolumeIdentity([string]$Path) {
    $probe=Full-InstallerPath $Path
    while (-not (Test-Path -LiteralPath $probe)) { $probe=[IO.Path]::GetDirectoryName($probe); if (-not $probe) { throw 'An existing local volume ancestor is required.' } }
    Initialize-InstallerBundleMoves
    return [JustInstallerBundleMovesV3]::Identity($probe).VolumeIdentity
}
function New-InstallerExclusiveDirectory([string]$Path) {
    $full=Full-InstallerPath $Path; Initialize-InstallerBundleMoves
    $result=[JustInstallerBundleMovesV3]::CreateDirectory($full)
    if (-not $result.Success) { throw "Owned directory allocation failed. Path: $full. Win32Error: $($result.Win32Error). HRESULT: $('0x{0:X8}' -f $result.HResult). $($result.Message)" }
}
function Get-InstallerScanRoot([string]$Destination) {
    $scan=Full-InstallerPath $Destination; $probe=$scan
    while ($probe.Length -gt 3) { if ((Split-Path -Leaf $probe) -ieq 'VST3') { $scan=$probe }; $probe=[IO.Path]::GetDirectoryName($probe); if (-not $probe) { break } }
    return $scan
}
function Write-InstallerJson($Value,[string]$Path,[switch]$NoOverwrite) {
    $full=Full-InstallerPath $Path; $parent=Split-Path -Parent $full
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $temp=$full+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $bytes=[Text.Encoding]::UTF8.GetBytes((ConvertTo-Json -InputObject $Value -Depth 40))
    $stream=New-Object IO.FileStream -ArgumentList @($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if ($NoOverwrite) { [IO.File]::Move($temp,$full) }
    elseif (Test-Path -LiteralPath $full) { $null=Full-InstallerPath $full; Initialize-InstallerBundleMoves; [JustInstallerBundleMovesV3]::ReplaceMetadata($temp,$full) }
    else { [IO.File]::Move($temp,$full) }
}
function Save-InstallerJournal {
    $path=Join-Path $script:Backup 'installer-transaction.json'
    if ($script:JournalSHA256) {
        if (-not (Test-Path -LiteralPath $path)) { throw 'Transaction journal is missing after a verified snapshot; preserve all trees.' }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $script:JournalSHA256) { throw 'Transaction journal changed externally; preserve all trees.' }
    }
    Write-InstallerJson $script:Journal $path -NoOverwrite:([string]::IsNullOrEmpty($script:JournalSHA256))
    $script:JournalSHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Unlock-InstallerDestination {
    if ($null -ne $script:InstallMutex) {
        if ($script:MutexOwned) { $script:InstallMutex.ReleaseMutex() }
        $script:InstallMutex.Dispose(); $script:InstallMutex=$null; $script:MutexOwned=$false
    }
}
function Copy-InstallerTreeExact([string]$Source,[string]$Destination,$Expected) {
    $sourcePath=Full-InstallerPath $Source; $dest=Full-InstallerPath $Destination; $state=Convert-InstallerState $Expected
    if (-not $state.exists -or -not (Equal-InstallerState (Get-InstallerState $sourcePath) $state)) { throw 'Copy source full state changed.' }
    if (Test-Path -LiteralPath $dest) { throw "Copy destination already exists; preserve it: $dest" }
    if ($state.type -eq 'directory') {
        New-InstallerExclusiveDirectory $dest
        foreach ($relative in $state.dirs) { New-InstallerExclusiveDirectory (Join-Path $dest $relative) }
    }
    foreach ($relative in @($state.hashes.Keys)) {
        $src=if ($state.type -eq 'file') { $sourcePath } else { Join-Path $sourcePath $relative }
        $out=if ($state.type -eq 'file') { $dest } else { Join-Path $dest $relative }
        $null=Full-InstallerPath $src; $null=Full-InstallerPath $out
        $copyInput=[IO.File]::Open($src,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
        try {
            $copyOutput=[IO.File]::Open($out,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            try { $copyInput.CopyTo($copyOutput); $copyOutput.Flush($true) } finally { $copyOutput.Dispose() }
        } finally { $copyInput.Dispose() }
    }
    if (-not (Equal-InstallerState (Get-InstallerState $sourcePath) $state) -or -not (Equal-InstallerState (Get-InstallerState $dest) $state)) { throw "Copy full-state verification failed; preserve the copy: $dest" }
}
function Get-InstallerItemsStableData($Journal) {
    $stable=@(); $seen=@{}
    foreach ($item in @($Journal.items)) {
        if ($item.slug -cnotin $script:Slugs -or $seen.ContainsKey($item.slug) -or $item.existed -isnot [bool] -or $item.touched -isnot [bool]) { throw 'Invalid transaction selected item.' }; $seen[$item.slug]=$true
        $old=Convert-InstallerState $item.oldState; $new=Convert-InstallerState $item.newState
        if ($old.exists -ne $item.existed -or -not $new.exists -or $new.type -ne 'directory' -or $new.hashes.Count -lt 1) { throw 'Invalid transaction old/new full state.' }
        $name="Just_$($item.slug).vst3"
        $paths=[ordered]@{ canonicalPath=(Join-Path $Journal.destination $name); stagePath=(Join-Path $Journal.swapRoot "stage\$name"); parkPath=(Join-Path $Journal.swapRoot "park\$name"); retiredPath=(Join-Path $Journal.swapRoot "retired\$name"); restoreStagePath=(Join-Path $Journal.swapRoot "restore-stage\$name") }
        foreach ($field in $paths.Keys) { if ((Full-InstallerPath ([string]$item.$field)) -cne $paths[$field]) { throw "Transaction item path mismatch: $field" } }
        if (($old.exists -and [string]::IsNullOrEmpty([string]$item.oldRootIdentity)) -or (-not $old.exists -and $item.oldRootIdentity -ne '') -or [string]::IsNullOrEmpty([string]$item.newRootIdentity)) { throw 'Invalid transaction object identities.' }
        $stable+=,[ordered]@{ slug=[string]$item.slug; existed=[bool]$item.existed; oldState=$old; newState=$new; oldRootIdentity=[string]$item.oldRootIdentity; newRootIdentity=[string]$item.newRootIdentity; canonicalPath=$paths.canonicalPath; stagePath=$paths.stagePath; parkPath=$paths.parkPath; retiredPath=$paths.retiredPath; restoreStagePath=$paths.restoreStagePath }
    }
    if ($stable.Count -lt 1 -or $stable.Count -gt 10 -or (($Journal.selected -join ',') -cne (($stable | ForEach-Object { $_.slug }) -join ','))) { throw 'Transaction selection/order mismatch.' }
    return ,$stable
}
function Get-InstallerItemsStableIdentity($Journal) { return (Get-InstallerJsonSHA256 (Get-InstallerItemsStableData $Journal)) }
function Get-InstallerStableContext($Journal,[string]$Backup) {
    if ($Journal.schema -eq 2) { throw "Schema 2 must use its original packaged Engine. Run the retained original entry: $(Join-Path $Backup 'JUST-uninstall.exe')." }
    if ($Journal.schema -ne 3 -or $Journal.version -ne '0.1.0' -or $Journal.status -notin @('BACKUP_VERIFIED','INSTALLING','SWAPPED_ENTRY_PENDING','INSTALLED','RESTORING','RESTORED','ROLLING_BACK','ROLLED_BACK','ROLLBACK_BLOCKED','RESTORE_BLOCKED')) { throw 'This Engine requires a valid schema 3 bundle transaction.' }
    $backupPath=Full-InstallerPath $Backup; $destinationPath=Full-InstallerPath ([string]$Journal.destination)
    if ([string]$Journal.backupDirectory -cne $backupPath -or [string]$Journal.transactionId -cne (Split-Path -Leaf $backupPath)) { throw 'Transaction directory/id mismatch.' }
    if ($Journal.setupInvocationId -isnot [string] -or ($Journal.setupInvocationId -ne '' -and $Journal.setupInvocationId -cnotmatch '^[0-9a-f]{32}$')) { throw 'Invalid Setup invocation identity.' }
    foreach ($field in @('candidateManifestSHA256','engineSHA256','itemsStableIdentitySHA256','swapOwnerSHA256')) { if ($Journal.$field -cnotmatch '^[0-9a-f]{64}$') { throw "Invalid stable transaction digest: $field" } }
    if ($Journal.swapNonce -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid swap nonce.' }
    $scanRoot=Get-InstallerScanRoot $destinationPath
    $swapRoot=Full-InstallerPath (Join-Path (Split-Path -Parent $scanRoot) ('JUST-VST3-transaction-'+$Journal.swapNonce))
    if ([string]$Journal.scanRoot -cne $scanRoot -or [string]$Journal.swapRoot -cne $swapRoot) { throw 'Swap root is not the bound sibling outside the scan root.' }
    foreach ($other in @($scanRoot,$backupPath,[string]$Journal.source)) { Assert-InstallerSeparate $swapRoot (Full-InstallerPath $other) }
    Assert-InstallerSeparate $backupPath $destinationPath
    $items=Get-InstallerItemsStableData $Journal; $identity=Get-InstallerJsonSHA256 $items
    if ($identity -cne $Journal.itemsStableIdentitySHA256 -or $Journal.destinationVolumeIdentity -cne $Journal.swapVolumeIdentity -or $Journal.volumeIdentity -cne $Journal.destinationVolumeIdentity) { throw 'Stable transaction item/volume identity mismatch.' }
    return [ordered]@{ schema=3; transactionId=[string]$Journal.transactionId; setupInvocationId=[string]$Journal.setupInvocationId; backupDirectory=$backupPath; journalPath=(Join-Path $backupPath 'installer-transaction.json'); destination=$destinationPath; version='0.1.0'; candidateManifestSHA256=[string]$Journal.candidateManifestSHA256; engineSHA256=[string]$Journal.engineSHA256; selected=@($Journal.selected); itemsStableIdentitySHA256=$identity; scanRoot=$scanRoot; swapRoot=$swapRoot; swapNonce=[string]$Journal.swapNonce; swapOwnerSHA256=[string]$Journal.swapOwnerSHA256; volumeIdentity=[string]$Journal.volumeIdentity; destinationVolumeIdentity=[string]$Journal.destinationVolumeIdentity; swapVolumeIdentity=[string]$Journal.swapVolumeIdentity; items=$items }
}
function Assert-InstallerSwapSpace($Context) {
    $ownerPath=Join-Path $Context.swapRoot 'swap-owner.json'; $null=Full-InstallerPath $ownerPath
    if ((Get-FileHash -LiteralPath $ownerPath -Algorithm SHA256).Hash -ine $Context.swapOwnerSHA256) { throw 'Swap ownership marker changed or is missing.' }
    $owner=Get-Content -LiteralPath $ownerPath -Raw | ConvertFrom-Json
    foreach ($field in @('schema','transactionId','setupInvocationId','destination','scanRoot','swapRoot','swapNonce','engineSHA256','volumeIdentity')) { if ([string]$owner.$field -cne [string]$Context[$field]) { throw "Swap ownership field mismatch: $field" } }
    if ((Get-InstallerVolumeIdentity $Context.destination) -cne $Context.destinationVolumeIdentity -or (Get-InstallerVolumeIdentity $Context.swapRoot) -cne $Context.swapVolumeIdentity) { throw 'Selected destination or swap volume identity changed.' }
    foreach ($role in @('stage','park','retired','restore-stage')) { Assert-InstallerContainer (Join-Path $Context.swapRoot $role) }
}
function Assert-InstallerRestoreBinding($Context,[string]$Path,[string]$ExpectedSHA256) {
    if (-not $Path) { if ($ExpectedSHA256) { throw 'Binding digest requires a binding file.' }; return }
    $fixed=Join-Path $Context.backupDirectory 'transaction-binding.json'
    if ((Full-InstallerPath $Path) -cne $fixed -or $ExpectedSHA256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Restore binding must be the fixed file in its original transaction directory.' }
    if ((Get-FileHash -LiteralPath $fixed -Algorithm SHA256).Hash -ine $ExpectedSHA256) { throw 'Restore binding changed or is missing.' }
    $binding=Get-Content -LiteralPath $fixed -Raw | ConvertFrom-Json
    foreach ($field in @('schema','transactionId','setupInvocationId','backupDirectory','journalPath','destination','version','candidateManifestSHA256','engineSHA256','itemsStableIdentitySHA256','scanRoot','swapRoot','swapNonce','swapOwnerSHA256','volumeIdentity','destinationVolumeIdentity','swapVolumeIdentity')) { if ([string]$binding.$field -cne [string]$Context[$field]) { throw "Restore binding field mismatch: $field" } }
    if (($binding.selected -join ',') -cne ($Context.selected -join ',') -or (Get-InstallerJsonSHA256 @($binding.items)) -cne (Get-InstallerJsonSHA256 $Context.items)) { throw 'Restore binding full items/selection mismatch.' }
}
function Assert-InstallerJournalUnchanged($Context,[string]$JournalSHA256) {
    $snapshot=Read-InstallerJournalSnapshot $Context.journalPath
    if ($snapshot.sha256 -cne $JournalSHA256 -or (Get-InstallerJsonSHA256 (Get-InstallerStableContext $snapshot.journal $Context.backupDirectory)) -cne (Get-InstallerJsonSHA256 $Context)) { throw 'Transaction journal/context changed; preserve all trees.' }
    if ((Get-FileHash -LiteralPath $script:EngineFile -Algorithm SHA256).Hash -ine $Context.engineSHA256) { throw 'Executing Engine does not match the transaction Engine SHA256.' }
    Assert-InstallerRestoreBinding $Context $BindingPath $ExpectedBindingSHA256
    Assert-InstallerSwapSpace $Context
}
function Assert-InstallerExactObject([string]$Path,$State,[string]$Identity) {
    if (-not (Equal-InstallerState (Get-InstallerState $Path) $State) -or (Get-InstallerObjectIdentity $Path) -cne $Identity) { throw "Bundle full state or object identity changed; preserve it: $Path" }
}
function Assert-InstallerSavedStates($Context) {
    foreach ($item in $Context.items) {
        $name="Just_$($item.slug).vst3"
        if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $Context.backupDirectory "previous\$name")) $item.oldState) -or -not (Equal-InstallerState (Get-InstallerState (Join-Path $Context.backupDirectory "staged\$name")) $item.newState)) { throw "Saved original/candidate full-state mismatch: $name" }
    }
    if ((Get-FileHash -LiteralPath (Join-Path $Context.backupDirectory 'Installer-Engine.ps1') -Algorithm SHA256).Hash -ine $Context.engineSHA256) { throw 'Retained Engine copy identity mismatch.' }
}
function Assert-InstallerLiveInstalled($Context) {
    Assert-InstallerSwapSpace $Context; Assert-InstallerSavedStates $Context
    foreach ($item in $Context.items) {
        Assert-InstallerExactObject $item.canonicalPath $item.newState $item.newRootIdentity
        Assert-InstallerExactObject $item.parkPath $item.oldState $item.oldRootIdentity
        foreach ($path in @($item.stagePath,$item.retiredPath,$item.restoreStagePath)) { if (Test-Path -LiteralPath $path) { throw "Unexpected later content in a completed swap role; preserve it: $path" } }
    }
}
function Complete-InstallerLiveTransaction($Context,$Completion) {
    if (-not $script:MutexOwned) { throw 'Finalize must hold the destination transaction mutex.' }
    if ($Context.setupInvocationId -cnotmatch '^[0-9a-f]{32}$') { throw 'Raw Engine swaps cannot claim fully completed Setup installation.' }
    $snapshot=Read-InstallerJournalSnapshot $Context.journalPath; $actual=Get-InstallerStableContext $snapshot.journal $Context.backupDirectory
    if ((Get-InstallerJsonSHA256 $actual) -cne (Get-InstallerJsonSHA256 $Context) -or $snapshot.journal.status -cne 'SWAPPED_ENTRY_PENDING') { throw 'Finalize requires this exact pending schema 3 transaction.' }
    Assert-InstallerLiveInstalled $Context
    if ($null -eq $Completion -or (Get-InstallerField $Completion 'completionId' '') -cnotmatch '^[0-9a-f]{32}$') { throw 'Finalize completion identity is required.' }
    foreach ($pair in @(@('uninstallerPath','JUST-uninstall.exe'),@('bindingPath','transaction-binding.json'))) {
        $path=Join-Path $Context.backupDirectory $pair[1]; $field=$pair[0]; $hashField=if ($field -eq 'uninstallerPath') { 'uninstallerSHA256' } else { 'bindingSHA256' }
        if ((Full-InstallerPath ([string](Get-InstallerField $Completion $field ''))) -cne $path -or (Get-InstallerField $Completion $hashField '') -cnotmatch '^[0-9a-f]{64}$' -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine (Get-InstallerField $Completion $hashField '')) { throw "Finalize artifact identity mismatch: $field" }
    }
    Assert-InstallerRestoreBinding $Context $Completion.bindingPath $Completion.bindingSHA256
    $script:Backup=$Context.backupDirectory; $script:Journal=$snapshot.journal; $script:JournalSHA256=$snapshot.sha256
    if ((Get-InstallerField $script:Journal 'testSwapFailure' '') -eq 'Finalize') { throw 'Intentional isolated Finalize failure; transaction remains pending.' }
    $completionRecord=[ordered]@{ state='READY'; completionId=[string]$Completion.completionId; uninstallerPath=[string]$Completion.uninstallerPath; uninstallerSHA256=[string]$Completion.uninstallerSHA256; bindingPath=[string]$Completion.bindingPath; bindingSHA256=[string]$Completion.bindingSHA256; setupInvocationId=$Context.setupInvocationId; transactionId=$Context.transactionId; utc=[DateTime]::UtcNow.ToString('o') }
    $script:Journal.completion=$completionRecord; $script:Journal.status='INSTALLED'; Save-InstallerJournal
    return $completionRecord
}
function Add-InstallerOperationEvent([string]$Operation,[string]$Slug,[string]$Source,[string]$Destination,$ExpectedState,[string]$ExpectedIdentity='') {
    $entry=[ordered]@{ sequence=@($script:Journal.events).Count+1; utc=[DateTime]::UtcNow.ToString('o'); operation=$Operation; slug=$Slug; source=$Source; destination=$Destination; phase='INTENT'; expectedSourceState=$ExpectedState; expectedSourceRootIdentity=$ExpectedIdentity; destinationExpectedAbsent=$true; nativeSuccess=$null; win32Error=$null; hresult=$null; hresultHex=''; message=''; verified=$false }
    $script:Journal.events+=,$entry; Save-InstallerJournal
    return $entry
}
function Move-InstallerBundleNoOverwrite($Context,$Item,[string]$Operation,[string]$Source,[string]$Destination,$ExpectedState,[string]$ExpectedIdentity) {
    $sourcePath=Full-InstallerPath $Source; $dest=Full-InstallerPath $Destination
    $allowed=@($Item.canonicalPath,$Item.stagePath,$Item.parkPath,$Item.retiredPath,$Item.restoreStagePath)
    if ($sourcePath -cnotin $allowed -or $dest -cnotin $allowed -or $sourcePath -ceq $dest) { throw 'Bundle move is outside this selected item and its bound roles.' }
    Assert-InstallerJournalUnchanged $Context $script:JournalSHA256
    Assert-InstallerExactObject $sourcePath $ExpectedState $ExpectedIdentity
    if (Test-Path -LiteralPath $dest) { throw "An external destination exists; no overwrite authorized: $dest" }
    $event=Add-InstallerOperationEvent $Operation $Item.slug $sourcePath $dest (Convert-InstallerState $ExpectedState) $ExpectedIdentity
    $script:Changed=$true
    $native=[JustInstallerBundleMovesV3]::Move($sourcePath,$dest)
    $event.nativeSuccess=$native.Success; $event.win32Error=$native.Win32Error; $event.hresult=$native.HResult; $event.hresultHex=('0x{0:X8}' -f $native.HResult); $event.message=$native.Message; $event.phase=if ($native.Success) { 'APPLIED' } else { 'FAILED' }
    $detail=[ordered]@{ sequence=$event.sequence; operation=$Operation; slug=$Item.slug; source=$sourcePath; destination=$dest; success=$native.Success; win32Error=$native.Win32Error; hresult=$native.HResult; hresultHex=$event.hresultHex; message=$native.Message; flags=0; method='MoveFileExW_NO_REPLACE_NO_COPY'; utc=[DateTime]::UtcNow.ToString('o') }
    $script:Report.bundleOperations+=,$detail
    if (-not $native.Success) { $script:Report.bundleMoveFailure=$detail }
    Save-InstallerJournal
    if (-not $native.Success) { throw "Selected bundle move failed. Operation: $Operation. Source: $sourcePath. Destination: $dest. Win32Error: $($native.Win32Error). HRESULT: $($event.hresultHex). $($native.Message)" }
    Assert-InstallerExactObject $dest $ExpectedState $ExpectedIdentity
    if (Test-Path -LiteralPath $sourcePath) { throw 'Bundle source reappeared after move; preserve it.' }
    $event.verified=$true; Save-InstallerJournal
}
function Copy-InstallerJournaled($Item,[string]$Operation,[string]$Source,[string]$Destination,$Expected) {
    $event=Add-InstallerOperationEvent $Operation $Item.slug $Source $Destination (Convert-InstallerState $Expected)
    Copy-InstallerTreeExact $Source $Destination $Expected
    $event.observedDestinationRootIdentity=Get-InstallerObjectIdentity $Destination
    $event.phase='APPLIED'; $event.verified=$true; Save-InstallerJournal
}
function Assert-InstallerTestIsolation([string]$Root,[string[]]$Paths) {
    $isolation=Full-InstallerPath $Root
    $temp=Full-InstallerPath ([IO.Path]::GetTempPath())
    $tempScoped=$isolation.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase) -or $temp.Equals((Join-Path $isolation 'temp'),[StringComparison]::OrdinalIgnoreCase)
    $named=(Split-Path -Leaf $isolation) -cmatch '^JUST-(?:schema3|installer|uninstaller|live)-tests-[0-9a-f]{32}$'
    if (-not $tempScoped -and -not $named) { throw 'Failure hooks require a fresh, explicitly named isolated test root.' }
    foreach ($path in $Paths) { if (-not (Full-InstallerPath $path).StartsWith($isolation+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Every failure-hook path must remain inside its isolated test root.' } }
}
function Invoke-InstallerSwapHook($Context,$Item,[string]$Phase) {
    $hookIndex=if ($TestSwapFailure -eq 'ForeignCanonicalSecond') { 1 } else { 0 }
    if (-not $TestSwapFailure -or ($script:Journal.items[$hookIndex].slug -cne $Item.slug)) { return }
    if ($Phase -eq 'AFTER_PARK' -and $TestSwapFailure -in @('AfterPark','CancelAfterPark')) { if ($TestSwapFailure -eq 'CancelAfterPark') { throw (New-Object OperationCanceledException -ArgumentList 'Intentional isolated cancellation after park.') }; throw 'Intentional isolated failure after park.' }
    if ($Phase -eq 'AFTER_COMMIT' -and $TestSwapFailure -eq 'AfterCommit') { throw 'Intentional isolated failure after commit.' }
    if ($Phase -eq 'BEFORE_COMMIT' -and $TestSwapFailure -in @('ForeignCanonical','ForeignCanonicalSecond','SecondMoveFailure')) {
        $event=Add-InstallerOperationEvent ('TEST_'+$TestSwapFailure) $Item.slug '' $Item.canonicalPath (Get-InstallerState $Item.canonicalPath)
        New-InstallerExclusiveDirectory $Item.canonicalPath
        $identity=Get-InstallerObjectIdentity $Item.canonicalPath
        if ($TestSwapFailure -in @('ForeignCanonical','ForeignCanonicalSecond')) { [IO.File]::WriteAllText((Join-Path $Item.canonicalPath 'unknown-external.txt'),'fixed isolated foreign-target sentinel; preserve') }
        $event.phase='APPLIED'; $event.verified=$true; $script:Journal.testCollision=[ordered]@{ kind=$TestSwapFailure; path=$Item.canonicalPath; identity=$identity; state=(Get-InstallerState $Item.canonicalPath) }; Save-InstallerJournal
    }
}
function Remove-InstallerEmptyTestCollision {
    $collision=Get-InstallerField $script:Journal 'testCollision'
    if ($null -eq $collision -or $collision.kind -ne 'SecondMoveFailure') { return }
    Assert-InstallerExactObject $collision.path $collision.state $collision.identity
    if ($collision.state.type -ne 'directory' -or @($collision.state.dirs).Count -or (Convert-InstallerHashes $collision.state.hashes).Count) { throw 'Nonempty test collision is preserved.' }
    $event=Add-InstallerOperationEvent 'REMOVE_OWNED_EMPTY_TEST_COLLISION' '' $collision.path '' $collision.state $collision.identity
    [IO.Directory]::Delete($collision.path,$false); $event.phase='APPLIED'; $event.verified=$true; Save-InstallerJournal
}
function Get-InstallerRestoredIdentity($Journal,$Item) {
    $identity=[string](Get-InstallerField $Item 'restoredRootIdentity' '')
    if ($identity) { return $identity }
    foreach ($event in @($Journal.events)) {
        if ($event.slug -ceq $Item.slug -and $event.operation -ceq 'RESTORE_OLD' -and $event.phase -in @('INTENT','APPLIED') -and $event.destination -ceq $Item.canonicalPath -and $event.source -cin @($Item.parkPath,$Item.restoreStagePath) -and (Equal-InstallerState $event.expectedSourceState $Item.oldState) -and -not (Test-Path -LiteralPath $event.source) -and (Get-InstallerObjectIdentity $Item.canonicalPath) -ceq [string]$event.expectedSourceRootIdentity) { $identity=[string]$event.expectedSourceRootIdentity }
    }
    return $identity
}
function Get-InstallerRestoreDecision($Journal,$Item,$Context) {
    $current=Get-InstallerState $Item.canonicalPath; $identity=Get-InstallerObjectIdentity $Item.canonicalPath
    $restoredIdentity=Get-InstallerRestoredIdentity $Journal $Item
    if (Equal-InstallerState $current $Item.oldState) {
        if (-not $Item.existed -or $identity -ceq $Item.oldRootIdentity -or ($restoredIdentity -and $identity -ceq $restoredIdentity)) { return [ordered]@{ kind='ORIGINAL'; needsChange=$false; current=$current; identity=$identity; originalSource=''; originalIdentity='' } }
        # Identical-payload reinstalls still have a different owned new root.
        # Fall through to the joint new-state/new-identity check below.
    }
    if (-not $Item.touched) { throw "An untouched selected bundle changed externally; preserve it: $($Item.canonicalPath)" }
    if (-not $current.exists) { $kind='ABSENT' }
    elseif ((Equal-InstallerState $current $Item.newState) -and $identity -ceq $Item.newRootIdentity) { $kind='NEW' }
    else { throw "Unknown canonical full state/object identity; preserve it: $($Item.canonicalPath)" }
    if ($kind -eq 'NEW' -and (Test-Path -LiteralPath $Item.retiredPath)) { throw 'Retired destination already exists; preserve both bundles.' }
    foreach ($pair in @(@($Item.stagePath,$Item.newState,$Item.newRootIdentity),@($Item.parkPath,$Item.oldState,$Item.oldRootIdentity),@($Item.retiredPath,$Item.newState,$Item.newRootIdentity))) { if (Test-Path -LiteralPath $pair[0]) { Assert-InstallerExactObject $pair[0] $pair[1] $pair[2] } }
    if (-not $Item.existed) { return [ordered]@{ kind=$kind; needsChange=$true; current=$current; identity=$identity; originalSource=''; originalIdentity='' } }
    if (Test-Path -LiteralPath $Item.parkPath) { return [ordered]@{ kind=$kind; needsChange=$true; current=$current; identity=$identity; originalSource=$Item.parkPath; originalIdentity=$Item.oldRootIdentity } }
    $stageIdentity=[string](Get-InstallerField $Item 'restoreStageIdentity' '')
    if (-not $stageIdentity) {
        foreach ($event in @($Journal.events)) {
            if ($event.slug -ceq $Item.slug -and $event.operation -ceq 'COPY_RESTORE_ORIGINAL' -and $event.phase -ceq 'APPLIED' -and $event.destination -ceq $Item.restoreStagePath -and (Equal-InstallerState $event.expectedSourceState $Item.oldState)) { $stageIdentity=[string](Get-InstallerField $event 'observedDestinationRootIdentity' '') }
        }
    }
    if (Test-Path -LiteralPath $Item.restoreStagePath) { if (-not $stageIdentity) { throw 'Unknown occupied restore stage; preserve it.' }; Assert-InstallerExactObject $Item.restoreStagePath $Item.oldState $stageIdentity }
    else {
        $source=Join-Path $Context.backupDirectory "previous\Just_$($Item.slug).vst3"
        Copy-InstallerJournaled $Item 'COPY_RESTORE_ORIGINAL' $source $Item.restoreStagePath $Item.oldState
        $stageIdentity=Get-InstallerObjectIdentity $Item.restoreStagePath; $Item.restoreStageIdentity=$stageIdentity; Save-InstallerJournal
    }
    return [ordered]@{ kind=$kind; needsChange=$true; current=$current; identity=$identity; originalSource=$Item.restoreStagePath; originalIdentity=$stageIdentity }
}
function Invoke-InstallerRollback($Journal,$Context) {
    $Journal.status='ROLLING_BACK'; Save-InstallerJournal
    $errors=@(); $ordered=@($Journal.items); [Array]::Reverse($ordered)
    foreach ($item in $ordered) {
        try {
            $decision=Get-InstallerRestoreDecision $Journal $item $Context
            if (-not $decision.needsChange) { $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='ALREADY_EXACT_ORIGINAL' }; continue }
            Assert-InstallerExactObject $item.canonicalPath $decision.current $decision.identity
            if ($decision.kind -eq 'NEW') { Move-InstallerBundleNoOverwrite $Context $item 'RETIRE_NEW' $item.canonicalPath $item.retiredPath $item.newState $item.newRootIdentity }
            if ($item.existed) { Move-InstallerBundleNoOverwrite $Context $item 'RESTORE_OLD' $decision.originalSource $item.canonicalPath $item.oldState $decision.originalIdentity; $item.restoredRootIdentity=$decision.originalIdentity; Save-InstallerJournal }
            $item.progress='ORIGINAL_RESTORED'; Save-InstallerJournal
            $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='ORIGINAL_FULL_STATE_RESTORED'; originalSource=$decision.originalSource; originalFileIdentityPreserved=($decision.originalIdentity -ceq $item.oldRootIdentity) }
        } catch {
            $errors+="$( $item.slug ): $($_.Exception.Message)"
            $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='BLOCKED_PRESERVED'; error=$_.Exception.Message }
            # If old could not return after retiring new, restore that exact
            # installed root where its empty canonical still permits it.
            try { if (-not (Test-Path -LiteralPath $item.canonicalPath) -and (Test-Path -LiteralPath $item.retiredPath)) { Move-InstallerBundleNoOverwrite $Context $item 'UNDO_RETIRE_NEW' $item.retiredPath $item.canonicalPath $item.newState $item.newRootIdentity } } catch { $errors+=$_.Exception.Message }
        }
    }
    $Journal.status=if ($errors.Count) { 'ROLLBACK_BLOCKED' } else { 'ROLLED_BACK' }; Save-InstallerJournal
    if ($errors.Count) { throw ($errors -join ' | ') }
}
function Restore-InstallerTransaction($Journal,[string]$Backup,[string]$JournalSnapshotSHA256='',[switch]$Rollback) {
    $context=Get-InstallerStableContext $Journal $Backup
    $script:Journal=$Journal; $script:Backup=$context.backupDirectory
    if ($JournalSnapshotSHA256) { $script:JournalSHA256=$JournalSnapshotSHA256 }
    elseif (-not $script:JournalSHA256) { throw 'Recovery requires the previously verified journal snapshot digest.' }
    Assert-InstallerJournalUnchanged $context $script:JournalSHA256; Assert-InstallerSavedStates $context
    $completion=Get-InstallerField $Journal 'completion'
    $wasComplete=($Journal.status -in @('INSTALLED','RESTORING','RESTORE_BLOCKED','RESTORED') -or ($null -ne $completion -and (Get-InstallerField $completion 'state' '') -eq 'READY'))
    $alreadyDone=($Journal.status -in @('RESTORED','ROLLED_BACK'))
    if (($Rollback -or -not $wasComplete) -and -not $alreadyDone) { Invoke-InstallerRollback $Journal $context; return }
    $decisions=@{}
    foreach ($item in $Journal.items) {
        if ($alreadyDone) {
            $restoredIdentity=Get-InstallerRestoredIdentity $Journal $item
            $originalIdentity=if ($restoredIdentity) { $restoredIdentity } else { $item.oldRootIdentity }
            Assert-InstallerExactObject $item.canonicalPath $item.oldState $originalIdentity
        } else { $decisions[$item.slug]=Get-InstallerRestoreDecision $Journal $item $context }
    }
    if ($alreadyDone) { foreach ($item in $Journal.items) { $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='EXACT_ORIGINAL_IDEMPOTENT_NO_OP' } }; return }
    $Journal.status=if ($Rollback -or -not $wasComplete) { 'ROLLING_BACK' } else { 'RESTORING' }; Save-InstallerJournal
    $changed=New-Object Collections.Generic.List[object]
    $ordered=@($Journal.items); [Array]::Reverse($ordered)
    try {
        foreach ($item in $ordered) {
            $decision=$decisions[$item.slug]
            if (-not $decision.needsChange) { $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='ALREADY_EXACT_ORIGINAL' }; continue }
            Assert-InstallerJournalUnchanged $context $script:JournalSHA256
            Assert-InstallerExactObject $item.canonicalPath $decision.current $decision.identity
            $record=[ordered]@{ item=$item; decision=$decision }; $changed.Add($record)
            if ($decision.kind -eq 'NEW') { Move-InstallerBundleNoOverwrite $context $item 'RETIRE_NEW' $item.canonicalPath $item.retiredPath $item.newState $item.newRootIdentity }
            if ($item.existed) {
                Move-InstallerBundleNoOverwrite $context $item 'RESTORE_OLD' $decision.originalSource $item.canonicalPath $item.oldState $decision.originalIdentity
                $item.restoredRootIdentity=$decision.originalIdentity; Save-InstallerJournal
            }
            if (-not (Equal-InstallerState (Get-InstallerState $item.canonicalPath) $item.oldState)) { throw 'Restored original full-state mismatch.' }
            $item.progress='ORIGINAL_RESTORED'; Save-InstallerJournal
            $script:Report.restoreCompleted+=,[ordered]@{ slug=$item.slug; result='ORIGINAL_FULL_STATE_RESTORED'; originalSource=$decision.originalSource; originalFileIdentityPreserved=($decision.originalIdentity -ceq $item.oldRootIdentity) }
        }
        foreach ($item in $Journal.items) { $final=Get-InstallerRestoreDecision $Journal $item $context; if ($final.needsChange) { throw 'Restore final verification did not find every original.' } }
        $Journal.status=if ($Rollback -or -not $wasComplete) { 'ROLLED_BACK' } else { 'RESTORED' }; Save-InstallerJournal
    } catch {
        $primary=$_.Exception.Message; $inverseErrors=@()
        $reverse=$changed.ToArray(); [Array]::Reverse($reverse)
        foreach ($record in $reverse) {
            try {
                $item=$record.item; $decision=$record.decision
                if ($item.existed -and (Test-Path -LiteralPath $item.canonicalPath) -and (Get-InstallerObjectIdentity $item.canonicalPath) -ceq $decision.originalIdentity) {
                    Move-InstallerBundleNoOverwrite $context $item 'UNDO_RESTORE_OLD' $item.canonicalPath $decision.originalSource $item.oldState $decision.originalIdentity
                }
                if ($decision.kind -eq 'NEW' -and -not (Test-Path -LiteralPath $item.canonicalPath) -and (Test-Path -LiteralPath $item.retiredPath)) { Move-InstallerBundleNoOverwrite $context $item 'UNDO_RETIRE_NEW' $item.retiredPath $item.canonicalPath $item.newState $item.newRootIdentity }
            } catch { $inverseErrors+=$_.Exception.Message }
        }
        $Journal.status=if ($Rollback -or -not $wasComplete) { 'ROLLBACK_BLOCKED' } else { 'RESTORE_BLOCKED' }
        try { Save-InstallerJournal } catch { $inverseErrors+=$_.Exception.Message }
        throw ($primary+' | Restore inverse: '+($inverseErrors -join ' | '))
    }
}
function New-InstallerSwapSpace([string]$Target,[string]$Backup,[string]$EngineSHA256) {
    $scanRoot=Get-InstallerScanRoot $Target; $nonce=[Guid]::NewGuid().ToString('N')
    $swapRoot=Full-InstallerPath (Join-Path (Split-Path -Parent $scanRoot) ('JUST-VST3-transaction-'+$nonce))
    foreach ($other in @($scanRoot,$Backup,$CandidateDir,$ReportPath)) { Assert-InstallerSeparate $swapRoot (Full-InstallerPath $other) }
    if ($TestFailAfter -ne 0 -or $TestSwapFailure -or $TestRestoreBoundarySlug) { Assert-InstallerTestIsolation $IsolationTestRoot @($CandidateDir,$Target,$Backup,$ReportPath,$swapRoot) }
    New-InstallerExclusiveDirectory $swapRoot
    foreach ($role in @('stage','park','retired','restore-stage')) { New-InstallerExclusiveDirectory (Join-Path $swapRoot $role) }
    $volume=Get-InstallerVolumeIdentity $Target
    if ((Get-InstallerVolumeIdentity $swapRoot) -cne $volume) { throw 'Swap preparation must be on the actual destination volume.' }
    $owner=[ordered]@{ schema=3; transactionId=(Split-Path -Leaf $Backup); setupInvocationId=$SetupInvocationId; destination=$Target; scanRoot=$scanRoot; swapRoot=$swapRoot; swapNonce=$nonce; engineSHA256=$EngineSHA256; volumeIdentity=$volume }
    $ownerPath=Join-Path $swapRoot 'swap-owner.json'; Write-InstallerJson $owner $ownerPath -NoOverwrite
    return [ordered]@{ scanRoot=$scanRoot; swapRoot=$swapRoot; swapNonce=$nonce; swapOwnerSHA256=(Get-FileHash -LiteralPath $ownerPath -Algorithm SHA256).Hash.ToLowerInvariant(); volumeIdentity=$volume; destinationVolumeIdentity=$volume; swapVolumeIdentity=$volume }
}

# Library import defines helpers only. It never invokes a transaction.
if ($LibraryOnly) { return }

try {
    if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Run in native Windows x64 PowerShell. No software is installed automatically.' }
    $ReportPath=Full-InstallerPath $ReportPath
    if ($SetupInvocationId -and $SetupInvocationId -cnotmatch '^[0-9a-f]{32}$') { throw 'SetupInvocationId must be a lowercase 32-hex invocation nonce.' }
    if ($Action -eq 'Install' -and ($BindingPath -or $ExpectedBindingSHA256)) { throw 'Restore-only binding arguments are not accepted for Install.' }
    if ($Action -eq 'Restore') {
        $script:Backup=Full-InstallerPath $BackupDir
        $snapshot=Read-InstallerJournalSnapshot (Join-Path $script:Backup 'installer-transaction.json'); $script:Journal=$snapshot.journal
        $context=Get-InstallerStableContext $script:Journal $script:Backup
        Assert-InstallerSeparate $ReportPath $context.destination; Assert-InstallerSeparate $ReportPath $script:Backup; Assert-InstallerSeparate $ReportPath $context.swapRoot
        $script:CanReport=$true; Lock-InstallerDestination $context.destination
        foreach ($field in @('backupDirectory','transactionId','setupInvocationId','candidateManifestSHA256','engineSHA256','itemsStableIdentitySHA256','scanRoot','swapRoot')) { $script:Report[$field]=$context[$field] }
        $script:Report.backup=$script:Backup; $script:Report.selected=@($context.selected)
        if ($TestRestoreBoundarySlug) { Assert-InstallerTestIsolation $IsolationTestRoot @($context.destination,$context.backupDirectory,$context.swapRoot,$ReportPath); if ($TestRestoreBoundarySlug -cnotin $context.selected) { throw 'Restore boundary hook must name a selected slug.' }; $leaf=Join-Path $context.destination "Just_$TestRestoreBoundarySlug.vst3"; New-InstallerExclusiveDirectory (Join-Path $leaf '.resume33-boundary-unknown-empty') }
        Restore-InstallerTransaction $script:Journal $script:Backup $snapshot.sha256
        $script:Report.status=$script:Journal.status
    } else {
        $candidate=Full-InstallerPath $CandidateDir; $target=Full-InstallerPath $Destination; $backups=Full-InstallerPath $BackupRoot
        foreach ($pair in @(@($candidate,$target),@($candidate,$backups),@($target,$backups),@($ReportPath,$candidate),@($ReportPath,$target),@($ReportPath,$backups))) { Assert-InstallerSeparate $pair[0] $pair[1] }
        $script:CanReport=$true; Lock-InstallerDestination $target
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) { throw 'Candidate must be an existing directory.' }; Assert-InstallerContainer $target
        $candidateManifestPath=Join-Path $candidate 'candidate-manifest.json'
        $candidateManifestSHA=(Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifest=Read-InstallerCandidate $candidate
        if ((Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine $candidateManifestSHA) { throw 'Candidate manifest changed during initial verification.' }
        $manifestHashes=Convert-InstallerHashes $manifest.hashes
        $selection=@(if ($Selected -eq 'all') { $script:Slugs } else { $Selected.Split(',') | ForEach-Object { $_.Trim().ToLowerInvariant() } })
        if ($selection.Count -lt 1 -or @($selection | Where-Object { $_ -cnotin $script:Slugs }).Count -or @($selection | Select-Object -Unique).Count -ne $selection.Count) { throw 'Select all or a comma-separated list of unique known plugin slugs.' }
        if ($TestFailAfter -lt 0 -or $TestFailAfter -gt $selection.Count) { throw 'TestFailAfter must name a selected completion boundary.' }
        if ($TestSwapFailure -eq 'ForeignCanonicalSecond' -and $selection.Count -lt 2) { throw 'ForeignCanonicalSecond requires at least two selected plugins.' }
        if ($TestFailAfter -ne 0 -or $TestSwapFailure -or $TestRestoreBoundarySlug) { Assert-InstallerTestIsolation $IsolationTestRoot @($candidate,$target,$backups,$ReportPath) }
        $script:Report.selected=$selection
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        $script:Backup=Join-Path $backups ('JUST-0.1.0-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $backups -Force | Out-Null; New-InstallerExclusiveDirectory $script:Backup
        foreach ($role in @('previous','staged')) { New-InstallerExclusiveDirectory (Join-Path $script:Backup $role) }
        $engineSHA=(Get-FileHash -LiteralPath $script:EngineFile -Algorithm SHA256).Hash.ToLowerInvariant()
        $swap=New-InstallerSwapSpace $target $script:Backup $engineSHA
        $items=@()
        foreach ($slug in $selection) {
            $name="Just_$slug.vst3"; $dest=Join-Path $target $name; $old=Get-InstallerState $dest; $new=Get-InstallerState (Join-Path $candidate "VST3\$name")
            $expectedHashes=@{}; $prefix=$name+'/'
            foreach ($key in $manifestHashes.Keys) { if ($key.StartsWith($prefix,[StringComparison]::Ordinal)) { $expectedHashes[$key.Substring($prefix.Length)]=$manifestHashes[$key] } }
            if (-not (Equal-InstallerHashes $new.hashes $expectedHashes)) { throw 'Candidate bundle changed after the immutable manifest verification.' }
            $items+=,[ordered]@{ slug=$slug; existed=[bool]$old.exists; oldState=$old; newState=$new; oldRootIdentity=(Get-InstallerObjectIdentity $dest); newRootIdentity=''; canonicalPath=$dest; stagePath=(Join-Path $swap.swapRoot "stage\$name"); parkPath=(Join-Path $swap.swapRoot "park\$name"); retiredPath=(Join-Path $swap.swapRoot "retired\$name"); restoreStagePath=(Join-Path $swap.swapRoot "restore-stage\$name"); touched=$false; progress='PREPARING'; restoreStageIdentity=''; restoredRootIdentity='' }
        }
        $script:Journal=[ordered]@{ schema=3; version='0.1.0'; status='PREPARING'; transactionId=(Split-Path -Leaf $script:Backup); setupInvocationId=$SetupInvocationId; backupDirectory=$script:Backup; destination=$target; source=$candidate; candidateManifestSHA256=$candidateManifestSHA; engineSHA256=$engineSHA; selected=$selection; items=$items; itemsStableIdentitySHA256=''; scanRoot=$swap.scanRoot; swapRoot=$swap.swapRoot; swapNonce=$swap.swapNonce; swapOwnerSHA256=$swap.swapOwnerSHA256; volumeIdentity=$swap.volumeIdentity; destinationVolumeIdentity=$swap.destinationVolumeIdentity; swapVolumeIdentity=$swap.swapVolumeIdentity; events=@(); completion=$null; testSwapFailure=$TestSwapFailure; testCollision=$null; utc=[DateTime]::UtcNow.ToString('o'); scope='Only selected direct JUST bundle roots; all live objects moved whole on one volume; no settings, projects or recursive bundle deletion.' }
        $script:Report.backup=$script:Backup
        foreach ($field in @('backupDirectory','transactionId','candidateManifestSHA256','engineSHA256','scanRoot','swapRoot')) { $script:Report[$field]=$script:Journal[$field] }
        Save-InstallerJournal
        foreach ($item in $items) {
            $name="Just_$($item.slug).vst3"
            if ($item.existed) { Copy-InstallerJournaled $item 'COPY_ORIGINAL' $item.canonicalPath (Join-Path $script:Backup "previous\$name") $item.oldState }
            Copy-InstallerJournaled $item 'COPY_CANDIDATE_BACKUP' (Join-Path $candidate "VST3\$name") (Join-Path $script:Backup "staged\$name") $item.newState
            Copy-InstallerJournaled $item 'COPY_SAME_VOLUME_STAGE' (Join-Path $candidate "VST3\$name") $item.stagePath $item.newState
            $item.newRootIdentity=Get-InstallerObjectIdentity $item.stagePath; $item.progress='PREPARED'; Save-InstallerJournal
        }
        Copy-InstallerTreeExact $script:EngineFile (Join-Path $script:Backup 'Installer-Engine.ps1') (Get-InstallerState $script:EngineFile)
        $script:Journal.itemsStableIdentitySHA256=Get-InstallerItemsStableIdentity $script:Journal; $script:Journal.status='BACKUP_VERIFIED'; Save-InstallerJournal
        $context=Get-InstallerStableContext $script:Journal $script:Backup; Assert-InstallerSwapSpace $context; Assert-InstallerSavedStates $context
        foreach ($item in $items) { Assert-InstallerExactObject $item.canonicalPath $item.oldState $item.oldRootIdentity; Assert-InstallerExactObject $item.stagePath $item.newState $item.newRootIdentity; foreach ($path in @($item.parkPath,$item.retiredPath,$item.restoreStagePath)) { if (Test-Path -LiteralPath $path) { throw 'Unknown content appeared in a reserved swap role.' } } }
        $script:Report.backup=$script:Backup
        foreach ($field in @('backupDirectory','transactionId','candidateManifestSHA256','engineSHA256','itemsStableIdentitySHA256','scanRoot','swapRoot')) { $script:Report[$field]=$context[$field] }
        if ((Get-FileHash -LiteralPath $candidateManifestPath -Algorithm SHA256).Hash -ine $candidateManifestSHA) { throw 'Candidate manifest changed before the first bundle move.' }
        $completed=0
        foreach ($item in $items) {
            $item.touched=$true; $script:Journal.status='INSTALLING'; Save-InstallerJournal
            if ($item.existed) { Move-InstallerBundleNoOverwrite $context $item 'PARK_OLD' $item.canonicalPath $item.parkPath $item.oldState $item.oldRootIdentity }
            $item.progress='OLD_PARKED'; Save-InstallerJournal; Invoke-InstallerSwapHook $context $item 'AFTER_PARK'; Invoke-InstallerSwapHook $context $item 'BEFORE_COMMIT'
            # Test collision hooks deliberately invoke the actual native no-overwrite API.
            $hookIndex=if ($TestSwapFailure -eq 'ForeignCanonicalSecond') { 1 } else { 0 }
            if ($TestSwapFailure -in @('ForeignCanonical','ForeignCanonicalSecond','SecondMoveFailure') -and $item.slug -ceq $items[$hookIndex].slug) {
                $event=Add-InstallerOperationEvent 'PUT_NEW' $item.slug $item.stagePath $item.canonicalPath $item.newState $item.newRootIdentity
                $script:Changed=$true
                $native=[JustInstallerBundleMovesV3]::Move($item.stagePath,$item.canonicalPath); $event.nativeSuccess=$native.Success; $event.win32Error=$native.Win32Error; $event.hresult=$native.HResult; $event.hresultHex=('0x{0:X8}' -f $native.HResult); $event.message=$native.Message; $event.phase=if ($native.Success) { 'APPLIED' } else { 'FAILED' }
                $detail=[ordered]@{ operation='PUT_NEW'; source=$item.stagePath; destination=$item.canonicalPath; success=$native.Success; win32Error=$native.Win32Error; hresult=$native.HResult; hresultHex=$event.hresultHex; message=$native.Message; flags=0; method='MoveFileExW_NO_REPLACE_NO_COPY' }; $script:Report.bundleOperations+=,$detail; $script:Report.bundleMoveFailure=$detail; Save-InstallerJournal
                if ($native.Success) { throw 'Unexpected test collision move success.' }; throw "Selected bundle move failed. Win32Error: $($native.Win32Error). HRESULT: $($event.hresultHex). $($native.Message)"
            }
            Move-InstallerBundleNoOverwrite $context $item 'PUT_NEW' $item.stagePath $item.canonicalPath $item.newState $item.newRootIdentity
            $item.progress='NEW_CANONICAL'; Save-InstallerJournal; ++$completed; Invoke-InstallerSwapHook $context $item 'AFTER_COMMIT'
            if ($TestFailAfter -eq $completed) { throw 'Intentional isolated installer test failure.' }
        }
        Assert-InstallerLiveInstalled $context; $script:Journal.status='SWAPPED_ENTRY_PENDING'; Save-InstallerJournal
        $script:Report.status='SWAPPED_ENTRY_PENDING'; $script:Report.installerCompletion='UNINSTALLER_PENDING'
    }
    Write-InstallerJson $script:Report $ReportPath
    Write-Output "$($script:Report.status): $($script:Report.selected -join ','). Backup: $($script:Report.backup)"
    exit 0
} catch {
    $script:Report.error=$_.Exception.Message; $script:Report.status='REFUSED'; $script:Report.backup=$script:Backup; $script:Report.backupDirectory=$script:Backup
    if ($Action -eq 'Install' -and $script:Changed -and $null -ne $script:Journal) {
        try { Remove-InstallerEmptyTestCollision; Restore-InstallerTransaction $script:Journal $script:Backup $script:JournalSHA256 -Rollback; $script:Report.status='ROLLED_BACK' }
        catch { $script:Report.status='ROLLBACK_BLOCKED'; $script:Report.error+=' | Rollback: '+$_.Exception.Message }
    } elseif ($Action -eq 'Restore' -and $null -ne $script:Journal) { $journalStatus=[string](Get-InstallerField $script:Journal 'status' ''); $script:Report.status=if ($journalStatus -in @('RESTORE_BLOCKED','ROLLBACK_BLOCKED')) { $journalStatus } else { 'REFUSED' } }
    try { if ($script:CanReport) { Write-InstallerJson $script:Report $ReportPath } } catch { Write-Warning 'Could not write installer report.' }
    Write-Error "$($script:Report.status): $($script:Report.error) Backup: $script:Backup" -ErrorAction Continue
    exit 1
} finally { Unlock-InstallerDestination }
