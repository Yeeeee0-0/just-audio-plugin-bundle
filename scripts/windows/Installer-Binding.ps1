# The NSIS resource extracts this helper and the trusted engine together.
# It never runs the mutable engine copy retained in a transaction backup.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('PrepareInstall','InvokeInstall','PublishInstall','WaitBindingGap','Create','Finalize','RollbackInstall','Describe','Restore','Cancel')][string]$Operation,
    [string]$EnginePath,
    [Parameter(Mandatory=$true)][string]$ExpectedEngineSHA256,
    [string]$ManifestSHA256,
    [string]$InstallReportPath,
    [string]$ExpectedDestination,
    [string]$ExpectedSelected,
    [string]$TransferPath,
    [string]$EntryPath,
    [string]$ClaimedDirectory,
    [int]$EntryProcessId,
    [string]$ReportPath,
    [string]$TestRestoreBoundarySlug,
    [string]$IsolationTestRoot,
    [string]$RequestPath,
    [string]$BackupRoot,
    [string]$SetupInvocationId,
    [string]$TestInstallBindingGapRoot,
    [string]$TestInstallBindingGapRole
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
# Dot-sourcing the engine declares its parameters in this scope. Preserve all
# caller arguments before loading its LibraryOnly functions.
$bindingRequest=@{}
foreach ($name in @('Operation','EnginePath','ExpectedEngineSHA256','ManifestSHA256','InstallReportPath','ExpectedDestination','ExpectedSelected','TransferPath','EntryPath','ClaimedDirectory','EntryProcessId','ReportPath','TestRestoreBoundarySlug','IsolationTestRoot','RequestPath','BackupRoot','SetupInvocationId','TestInstallBindingGapRoot','TestInstallBindingGapRole')) { $bindingRequest[$name]=Get-Variable -Name $name -ValueOnly }
$bindingReport=[ordered]@{ schema=2; action='Uninstall'; status='PREFLIGHT'; RestoreCalled=$false; error=''; utc=[DateTime]::UtcNow.ToString('o') }
$bindingSafeReport=''; $bindingContext=$null; $bindingReportWritable=$false; $bindingEarlyErrorPath=''; $bindingEngineReportPreserved=$false
$bindingTrailerMagic=[Text.Encoding]::ASCII.GetBytes("JUST-BOUND-UNINSTALLER-V1`n")
function Assert-BindingBasicPath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path) -or $Path -notmatch '^[A-Za-z]:\\' -or $Path -match '[\x00-\x1f"<>|*?]') { throw 'An unambiguous absolute local drive path is required.' }
    $full=[IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($full.Length -le 3) { throw 'Drive root refused.' }
    $probe=$full
    while ($probe.Length -gt 3) {
        if (Test-Path -LiteralPath $probe) { if (((Get-Item -LiteralPath $probe -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse path refused.' } }
        $probe=[IO.Path]::GetDirectoryName($probe)
    }
    return $full
}
function Write-BindingReport {
    if ($bindingEngineReportPreserved) { return }
    $path=if ($bindingReportWritable) { $bindingSafeReport } else { $bindingEarlyErrorPath }
    if (-not $path) { return }
    if (-not $bindingReportWritable -and (Test-Path -LiteralPath $path)) { return }
    $parent=[IO.Path]::GetDirectoryName($path)
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $bindingReport | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $path -Encoding UTF8
}
function Protect-BindingReport([string]$Other) {
    if (Overlap-InstallerPath $bindingSafeReport $Other) { $script:bindingReportWritable=$false; $script:bindingSafeReport=''; throw 'Report must remain outside the original destination and transaction directory.' }
}
function Write-BindingTransfer($Context) {
    if (-not $bindingRequest.TransferPath) { return }
    $path=Full-InstallerPath $bindingRequest.TransferPath
    Assert-InstallerSeparate $path $Context.backupDirectory; Assert-InstallerSeparate $path $Context.destination
    if ($Context.schema -eq 3) { Assert-InstallerSeparate $path $Context.swapRoot }
    $text="[Entry]`r`nbackupDirectory=$($Context.backupDirectory)`r`ndestination=$($Context.destination)`r`nselected=$($Context.selected -join ',')`r`n"
    [IO.File]::WriteAllText($path,$text,[Text.Encoding]::Unicode)
}
function Read-BindingTrailer([string]$Path) {
    $stream=[IO.File]::Open($Path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        $footerLength=$bindingTrailerMagic.Length+4
        if ($stream.Length -lt $footerLength) { throw 'Missing transaction trailer.' }
        $stream.Position=$stream.Length-$footerLength
        $reader=New-Object IO.BinaryReader($stream)
        $length=$reader.ReadUInt32(); $magic=$reader.ReadBytes($bindingTrailerMagic.Length)
        if ([Convert]::ToBase64String($magic) -cne [Convert]::ToBase64String($bindingTrailerMagic) -or $length -lt 2 -or $length -gt 8192 -or $stream.Length -lt $footerLength+$length) { throw 'Invalid transaction trailer.' }
        $stream.Position=$stream.Length-$footerLength-$length
        $data=$reader.ReadBytes([int]$length)
        return ([Text.Encoding]::UTF8.GetString($data) | ConvertFrom-Json)
    } finally { $stream.Dispose() }
}
function Get-BindingContext([string]$Backup) {
    $snapshot=Read-InstallerJournalSnapshot (Join-Path $Backup 'installer-transaction.json')
    return (Get-InstallerStableContext $snapshot.journal $Backup)
}
function Assert-BindingInstalled($Context) {
    if ($Context.schema -eq 3) { Assert-InstallerLiveInstalled $Context; return }
    foreach ($item in $Context.items) {
        $bundle="Just_$($item.slug).vst3"
        if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $Context.destination $bundle)) $item.newState)) { throw "Installed full-state mismatch: $bundle" }
        if (-not (Equal-InstallerState (Get-InstallerState (Join-Path $Context.backupDirectory "previous\$bundle")) $item.oldState)) { throw "Saved original full-state mismatch: $bundle" }
    }
}
function Read-SetupInvocation {
    $privateReport=Join-Path $PSScriptRoot 'install-authority.json'
    if ((Full-InstallerPath $bindingRequest.InstallReportPath) -cne $privateReport) { throw 'The authority report must be this Setup resource directory\install-authority.json.' }
    $identity=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'setup-invocation.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($identity.schema -ne 1 -or $bindingRequest.SetupInvocationId -cnotmatch '^[0-9a-f]{32}$' -or $identity.setupInvocationId -cne $bindingRequest.SetupInvocationId -or $identity.authoritativeReportPath -cne $privateReport -or $identity.engineSHA256 -cne $bindingRequest.ExpectedEngineSHA256 -or $identity.candidateManifestSHA256 -cne $bindingRequest.ManifestSHA256) { throw 'This Setup invocation identity/authority report mismatch.' }
    $expected=@(if ($bindingRequest.ExpectedSelected -eq 'all') { @($script:Slugs) } else { @($bindingRequest.ExpectedSelected.Split(',')) })
    if ($identity.destination -cne (Full-InstallerPath $bindingRequest.ExpectedDestination) -or $identity.backupRoot -cne (Full-InstallerPath $bindingRequest.BackupRoot) -or $identity.publicReportPath -cne $bindingSafeReport -or ($identity.selected -join ',') -cne ($expected -join ',')) { throw 'Private Setup invocation destination/backup/report/ordered selection changed.' }
    Protect-BindingReport $identity.destination; Protect-BindingReport $identity.backupRoot
    $script:bindingReportWritable=$true
    return $identity
}
function Read-SetupAuthority($Identity) {
    # An explicit external /REPORT may be shared or overwritten by another
    # Setup. It is never read here and never supplies transaction identity.
    $installed=Get-Content -LiteralPath $Identity.authoritativeReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($installed.action -ne 'Install' -or $installed.version -ne '0.1.0' -or $installed.setupInvocationId -cne $Identity.setupInvocationId) { throw 'Private engine report did not originate from this Setup invocation.' }
    return $installed
}
function Write-SetupEvidence($Installed,$Identity) {
    $Installed | Add-Member -NotePropertyName authoritativeReportKind -NotePropertyValue 'THIS_SETUP_PRIVATE_REPORT' -Force
    Write-InstallerJson $Installed $Identity.authoritativeReportPath
    $Installed | Add-Member -NotePropertyName authoritativeReportSHA256 -NotePropertyValue ((Get-FileHash -LiteralPath $Identity.authoritativeReportPath -Algorithm SHA256).Hash.ToLowerInvariant()) -Force
    Write-InstallerJson $Installed $bindingSafeReport
}
function Initialize-BindingNativeCapture {
    if ('JustBindingNativeCaptureV2' -as [type]) { return }
    $clrDirectory=[Runtime.InteropServices.RuntimeEnvironment]::GetRuntimeDirectory()
    $clrReferences=@([object].Assembly.Location,(Join-Path $clrDirectory 'System.dll'),(Join-Path $clrDirectory 'System.Core.dll'))
    foreach ($reference in $clrReferences) { if (-not [IO.Path]::IsPathRooted($reference) -or -not (Test-Path -LiteralPath $reference -PathType Leaf)) { throw 'The trusted CLR compiler reference is unavailable.' } }
    Add-Type -ReferencedAssemblies $clrReferences -TypeDefinition @"
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
public sealed class JustBindingCaptureResultV2 {
    public string Path;
    public long TotalBytes;
    public long WrittenBytes;
    public bool EofObserved;
    public bool RawFileComplete;
    public byte[] Prefix=new byte[65536];
    public int PrefixLength;
    public bool PrefixTruncated;
    public int ErrorCount;
    public readonly List<string> Errors=new List<string>();
    public string ReadError="";
    public void Error(string phase, Exception error) {
        ++ErrorCount;
        if (Errors.Count<8) {
            string text=phase+": "+error.ToString();
            Errors.Add(text.Length>2048 ? text.Substring(0,2048) : text);
        }
    }
}
public sealed class JustBindingExitResultV2 {
    public bool ExitObserved;
    public int ExitCode;
    public uint WaitResult;
    public int Win32Error;
}
public sealed class JustBindingNativeCaptureV2 : IDisposable {
    readonly ManualResetEvent ready=new ManualResetEvent(false);
    readonly object gate=new object();
    readonly string path;
    readonly Stream suppliedLog;
    readonly bool supplied;
    Stream input;
    bool cancelled;
    public readonly Task<JustBindingCaptureResultV2> Completion;
    public bool IsAttached { get { lock(gate) { return input!=null; } } }
    public JustBindingNativeCaptureV2(string outputPath) : this(outputPath,null,false) {}
    // A stream overload supports isolated fault fixtures; production uses a new file.
    public JustBindingNativeCaptureV2(string outputPath, Stream fixtureLog) : this(outputPath,fixtureLog,true) {}
    JustBindingNativeCaptureV2(string outputPath, Stream fixtureLog, bool suppliedStream) {
        path=outputPath; suppliedLog=fixtureLog; supplied=suppliedStream;
        // Reserve the workers before the child starts; attachment starts no task.
        Completion=Task.Factory.StartNew<JustBindingCaptureResultV2>(Drain,CancellationToken.None,TaskCreationOptions.LongRunning,TaskScheduler.Default);
    }
    public void Attach(Stream source) {
        if (source==null) throw new ArgumentNullException("source");
        lock(gate) {
            if (input!=null && input!=source) throw new InvalidOperationException("Capture was already attached to another stream.");
            input=source; ready.Set();
        }
    }
    public void CancelUnattached() { lock(gate) { if(input==null) { cancelled=true; ready.Set(); } } }
    JustBindingCaptureResultV2 Drain() {
        var result=new JustBindingCaptureResultV2 { Path=path };
        Stream writer=null; bool writing=false;
        try {
            ready.WaitOne();
            if (cancelled || input==null) { result.Error("Unattached capture",new InvalidOperationException("No child stream was attached.")); return result; }
            try { writer=supplied ? suppliedLog : new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.Read); writing=writer!=null; }
            catch(Exception error) { result.Error("Log open",error); }
            byte[] buffer=new byte[8192];
            while(true) {
                int count;
                try { count=input.Read(buffer,0,buffer.Length); }
                catch(Exception error) {
                    result.ReadError=error.ToString(); if(result.ReadError.Length>2048) result.ReadError=result.ReadError.Substring(0,2048); result.Error("Pipe read",error);
                    try { input.Dispose(); } catch(Exception closeError) { result.Error("Pipe close after read error",closeError); }
                    break;
                }
                if(count==0) { result.EofObserved=true; break; }
                result.TotalBytes+=count;
                int copy=Math.Min(count,result.Prefix.Length-result.PrefixLength);
                if(copy>0) { Buffer.BlockCopy(buffer,0,result.Prefix,result.PrefixLength,copy); result.PrefixLength+=copy; }
                result.PrefixTruncated=result.TotalBytes>result.PrefixLength;
                if(writing) {
                    try { writer.Write(buffer,0,count); result.WrittenBytes+=count; }
                    catch(Exception error) {
                        result.Error("Log write",error); writing=false;
                        try { writer.Dispose(); } catch(Exception closeError) { result.Error("Log close after write error",closeError); }
                        writer=null;
                    }
                }
            }
            if(writer!=null) {
                try { writer.Flush(); } catch(Exception error) { result.Error("Log flush",error); writing=false; }
                try { writer.Dispose(); } catch(Exception error) { result.Error("Log dispose",error); writing=false; }
                writer=null;
            }
            result.RawFileComplete=result.EofObserved && writing && result.WrittenBytes==result.TotalBytes;
        } catch(Exception error) { result.Error("Capture observer",error); }
        finally {
            if(writer!=null) { try { writer.Dispose(); } catch(Exception error) { result.Error("Final log dispose",error); } }
        }
        return result;
    }
    public void Dispose() { ready.Dispose(); }
    [DllImport("kernel32.dll")] public static extern uint GetOEMCP();
    [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr handle,out uint exitCode);
    public static JustBindingExitResultV2 ObserveExit(IntPtr handle) {
        uint wait=WaitForSingleObject(handle,0xFFFFFFFFu);
        var result=new JustBindingExitResultV2 { WaitResult=wait };
        if(wait!=0) { result.Win32Error=Marshal.GetLastWin32Error(); return result; }
        uint code;
        if(!GetExitCodeProcess(handle,out code)) { result.Win32Error=Marshal.GetLastWin32Error(); return result; }
        result.ExitObserved=true; result.ExitCode=unchecked((int)code); return result;
    }
}
"@ -ErrorAction Stop
}
function Connect-BindingNativeCapture($Capture,$Stream) { $Capture.Attach($Stream) }
function Read-BindingNativeLog([string]$Path,$Capture) {
    # The full raw file is retained and hashed separately; summaries stay bounded.
    $prefix=[byte[]]$Capture.Prefix; $length=[int]$Capture.PrefixLength; $offset=0
    $encoding=[Text.UTF8Encoding]::new($false,$true); $decoded=$null; $decodedBytes=$length
    if ($length -ge 2 -and $prefix[0] -eq 255 -and $prefix[1] -eq 254) { $encoding=[Text.Encoding]::Unicode; $offset=2 }
    elseif ($length -ge 2 -and $prefix[0] -eq 254 -and $prefix[1] -eq 255) { $encoding=[Text.Encoding]::BigEndianUnicode; $offset=2 }
    elseif ($length -ge 3 -and $prefix[0] -eq 239 -and $prefix[1] -eq 187 -and $prefix[2] -eq 191) { $offset=3 }
    if ($encoding -is [Text.UTF8Encoding]) {
        for ($trim=0; $trim -le $(if ($Capture.PrefixTruncated) {3} else {0}); ++$trim) {
            try { $decoded=$encoding.GetString($prefix,$offset,$length-$offset-$trim); $decodedBytes=$length-$trim; break } catch { }
        }
        if ($null -eq $decoded) { $encoding=[Text.Encoding]::GetEncoding([int][JustBindingNativeCaptureV2]::GetOEMCP()); $offset=0 }
    }
    if ($null -eq $decoded) { $decoded=$encoding.GetString($prefix,$offset,$length-$offset) }
    return [ordered]@{ path=$Path; sha256=$null; bytes=$null; rawFileExists=$false; rawFileComplete=[bool]$Capture.RawFileComplete; totalBytes=[long]$Capture.TotalBytes; writtenBytes=[long]$Capture.WrittenBytes; eofObserved=[bool]$Capture.EofObserved; summaryBytes=$length; summaryDecodedBytes=$decodedBytes; summaryTruncated=[bool]$Capture.PrefixTruncated; summaryEncoding=$encoding.WebName; text=$decoded; captureErrorCount=[int]$Capture.ErrorCount; captureErrors=@($Capture.Errors.ToArray()); readError=$Capture.ReadError }
}
function ConvertTo-BindingWin32Argument([string]$Value) {
    # ProcessStartInfo.ArgumentList is unavailable in native Windows PS5.
    # Quote one data argument using the Windows argv backslash/quote rules.
    $quoted=New-Object Text.StringBuilder
    [void]$quoted.Append([char]34); $slashes=0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) { ++$slashes; continue }
        if ($character -eq [char]34) {
            [void]$quoted.Append([string]::new([char]92,2*$slashes+1))
        } else { [void]$quoted.Append([string]::new([char]92,$slashes)) }
        [void]$quoted.Append($character); $slashes=0
    }
    [void]$quoted.Append([string]::new([char]92,2*$slashes))
    [void]$quoted.Append([char]34)
    return $quoted.ToString()
}

function Add-BindingNativeCaptureError($Diagnostic,[string]$Stage,[string]$Text) {
    ++$Diagnostic.captureErrorCount
    if ($Diagnostic.captureErrors.Count -lt 24) {
        $message=$Stage+': '+$Text
        if ($message.Length -gt 2048) { $message=$message.Substring(0,2048) }
        $Diagnostic.captureErrors+=@($message)
    }
}
function Get-BindingNativeRawEvidence([string]$Path) {
    $file=Get-Item -LiteralPath $Path -ErrorAction Stop
    return [ordered]@{ bytes=[long]$file.Length; sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant() }
}
function Observe-BindingNativeExit([IntPtr]$Handle) { return [JustBindingNativeCaptureV2]::ObserveExit($Handle) }
function Get-BindingFailureAdvice([string]$PrimaryError,[int]$FileWin32Error=-1,[string]$StartException='', [bool]$AuthorityValidated=$false,$NativeExitCode=$null) {
    # Classify the actual error; never replace or prefix its diagnostic text.
    $kind='ENGINE_FAILURE'; $guidance='Keep the actual diagnostic, report and any transaction backup before retrying.'
    if ($null -ne $NativeExitCode -and $NativeExitCode -eq 0 -and $AuthorityValidated) { return [ordered]@{ failureKind=''; guidance='' } }
    if ($PrimaryError.Contains('REAPER is running.')) {
        $kind='HOST_RUNNING'; $guidance='Save work and close REAPER and its plugin-host processes normally, then retry. The installer never terminates processes.'
    } elseif ($StartException) {
        $kind='ENGINE_START_EXCEPTION'; $guidance='The native installer engine could not start. Keep its start exception and diagnostic; closing a DAW is not a verified remedy.'
    } elseif ($PrimaryError -match '(?i)\bReparse (?:points are not supported|point refused|transaction entry refused|bundle root refused|entry appeared during availability preflight|path refused)') {
        $kind='REPARSE_POINT_REFUSED'; $guidance='The reported path contains an unsupported link or reparse point. Selected JUST bundles and their ancestors must be direct local paths. Do not delete unrelated plugins.'
    } elseif ($FileWin32Error -eq 32 -or $PrimaryError -match '(?i)\bWin32Error:\s*32\b|\bHRESULT:\s*0x80070020\b') {
        $kind='SELECTED_PLUGIN_SHARING_VIOLATION'; $guidance='The reported selected file is in use. Save work and close the application using that file normally, then retry. The installer never terminates processes.'
    } elseif ($FileWin32Error -eq 5 -or $PrimaryError -match '(?i)\bWin32Error:\s*5\b|\bHRESULT:\s*0x80070005\b') {
        $kind='SELECTED_PLUGIN_ACCESS_DENIED'; $guidance='The reported selected operation was denied. An open plugin window or file handle, or existing access rights or attributes, can prevent a bundle rename. Save work and release the application holding that path normally if it is in use; otherwise check elevation and existing permissions. Error 5 alone cannot distinguish these causes. The installer never closes hosts or changes ACLs.'
    } elseif (-not $AuthorityValidated) {
        $kind='AUTHORITY_REPORT_MISSING_OR_INVALID'; $guidance='Keep the native diagnostic and any transaction backup. Target changes and recovery could not be verified from an authoritative engine report.'
    } elseif ($PrimaryError.Contains('Selected plugin is unavailable for replacement.')) {
        $kind='SELECTED_PLUGIN_UNAVAILABLE'; $guidance='The reported selected path could not be opened for replacement. Keep its exact path and Win32 error before retrying.'
    }
    return [ordered]@{ failureKind=$kind; guidance=$guidance }
}
function Invoke-SetupEngine($Identity,$RecoveryContext=$null) {
    $native=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $authorityPath=$Identity.authoritativeReportPath
    if ($null -ne $RecoveryContext) {
        $authorityPath=Join-Path $PSScriptRoot 'install-entry-recovery.json'
        if (Test-Path -LiteralPath $authorityPath) { throw 'Entry recovery was already attempted; preserve its report and retained objects.' }
        $engineArguments=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$trusted,'-Action','Restore','-BackupDir',$RecoveryContext.backupDirectory,'-ReportPath',$authorityPath)
    } else {
        $engineArguments=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$trusted,'-Action','Install','-CandidateDir',(Join-Path $PSScriptRoot 'candidate'),'-Destination',$Identity.destination,'-BackupRoot',$Identity.backupRoot,'-Selected',($Identity.selected -join ','),'-SetupInvocationId',$Identity.setupInvocationId,'-ReportPath',$authorityPath)
    }
    $logDirectory=Full-InstallerPath (Join-Path ([IO.Path]::GetDirectoryName($bindingSafeReport)) ('engine-'+[Guid]::NewGuid().ToString('N')))
    foreach ($other in @($Identity.destination,$Identity.backupRoot,$Identity.authoritativeReportPath,$PSScriptRoot)) { Assert-InstallerSeparate $logDirectory $other }
    if (Test-Path -LiteralPath $logDirectory) { throw 'Native diagnostic directory must be new.' }
    $stdoutPath=Join-Path $logDirectory 'stdout.raw'; $stderrPath=Join-Path $logDirectory 'stderr.raw'
    $metadataPath=Join-Path $logDirectory 'native-engine.json'
    $diagnostic=[ordered]@{ schema=2; executable=$native; arguments=$engineArguments; workingDirectory=$PSScriptRoot; setupInvocationId=$Identity.setupInvocationId; helperProcessId=$PID; helperIs64Bit=[Environment]::Is64BitProcess; helperProcessorArchitectureEnvironment=$env:PROCESSOR_ARCHITECTURE; childProcessId=$null; actualModulePath=$null; imageObservationError=''; nativeExitCode=$null; childExitObserved=$false; startException=''; postStartException=''; captureErrorCount=0; captureErrors=@(); failureKind=''; primaryError=''; originalPrimaryError=''; guidance=''; availabilityFailure=$null; authorityReportPath=$Identity.authoritativeReportPath; authorityReportExists=$false; authorityValidated=$false; authorityStatus=''; stdout=$null; stderr=$null; metadataPath=$metadataPath; metadataWritten=$false; metadataSHA256=$null; targetMutationState='UNKNOWN_AUTHORITY_NOT_VALIDATED'; recoveryState='NO_RECOVERY_CLAIM_BY_WRAPPER'; utc=[DateTime]::UtcNow.ToString('o') }
    $script:bindingReport.nativeEngine=$diagnostic
    try { New-Item -ItemType Directory -Path $logDirectory -ErrorAction Stop | Out-Null }
    catch { Add-BindingNativeCaptureError $diagnostic 'Diagnostic directory' $_.Exception.ToString() }
    $process=New-Object Diagnostics.Process
    $process.StartInfo.FileName=$native
    $process.StartInfo.Arguments=(@($engineArguments | ForEach-Object { ConvertTo-BindingWin32Argument ([string]$_) }) -join ' ')
    $process.StartInfo.WorkingDirectory=$PSScriptRoot
    $process.StartInfo.UseShellExecute=$false; $process.StartInfo.CreateNoWindow=$true
    $process.StartInfo.RedirectStandardOutput=$true; $process.StartInfo.RedirectStandardError=$true
    $stdoutPump=$null; $stderrPump=$null; $stdoutReader=$null; $stderrReader=$null; $stdoutSource=$null; $stderrSource=$null
    $stdoutResult=$null; $stderrResult=$null; $result=$null; $started=$false; $processHandle=[IntPtr]::Zero
    try {
        Initialize-BindingNativeCapture
        $stdoutPump=[JustBindingNativeCaptureV2]::new($stdoutPath)
        $stderrPump=[JustBindingNativeCaptureV2]::new($stderrPath)
        if (-not $process.Start()) { throw 'Native engine process did not start.' }
        $started=$true; $diagnostic.childProcessId=$process.Id; $processHandle=$process.Handle
        $stdoutReader=$process.StandardOutput; $stderrReader=$process.StandardError; $stdoutSource=$stdoutReader.BaseStream; $stderrSource=$stderrReader.BaseStream
        Connect-BindingNativeCapture $stdoutPump $stdoutSource
        Connect-BindingNativeCapture $stderrPump $stderrSource
        try { $diagnostic.actualModulePath=$process.MainModule.FileName }
        catch { $diagnostic.imageObservationError=$_.Exception.Message }
        $exit=Observe-BindingNativeExit $processHandle
        if (-not $exit.ExitObserved) { throw "Native child wait failed: $($exit.Win32Error), wait result $($exit.WaitResult)." }
        $result=[int]$exit.ExitCode; $diagnostic.nativeExitCode=$result; $diagnostic.childExitObserved=$true
    } catch {
        if ($started) { $diagnostic.postStartException=$_.Exception.ToString(); Add-BindingNativeCaptureError $diagnostic 'Post-start observer' $diagnostic.postStartException }
        else { $diagnostic.startException=$_.Exception.ToString() }
    } finally {
        if ($started) {
            # A failed attachment/observer does not stop the already reserved workers.
            foreach ($pair in @(@{pump=$stdoutPump; source=$stdoutSource; reader=$stdoutReader; name='stdout'},@{pump=$stderrPump; source=$stderrSource; reader=$stderrReader; name='stderr'})) {
                try {
                    if ($null -ne $pair.pump -and -not $pair.pump.IsAttached) {
                        $reader=$pair.reader
                        if ($null -eq $reader) { $reader=if ($pair.name -eq 'stdout') { $process.StandardOutput } else { $process.StandardError } }
                        $stream=if ($null -ne $pair.source) { $pair.source } else { $reader.BaseStream }
                        $pair.pump.Attach($stream)
                    }
                } catch {
                    Add-BindingNativeCaptureError $diagnostic 'Fallback pipe attachment' $_.Exception.ToString()
                    try { if ($null -ne $reader) { $reader.Close() } elseif ($null -ne $pair.source) { $pair.source.Dispose() } } catch { Add-BindingNativeCaptureError $diagnostic 'Close unavailable own pipe' $_.Exception.ToString() }
                }
            }
            if (-not $diagnostic.childExitObserved) {
                try {
                    if ($processHandle -eq [IntPtr]::Zero) { $processHandle=$process.Handle }
                    $exit=[JustBindingNativeCaptureV2]::ObserveExit($processHandle)
                    if (-not $exit.ExitObserved) { throw "Native child fallback wait failed: $($exit.Win32Error)." }
                    $result=[int]$exit.ExitCode; $diagnostic.nativeExitCode=$result; $diagnostic.childExitObserved=$true
                } catch {
                    Add-BindingNativeCaptureError $diagnostic 'Native exit observation' $_.Exception.ToString()
                    try { $process.WaitForExit(); $result=[int]$process.ExitCode; $diagnostic.nativeExitCode=$result; $diagnostic.childExitObserved=$true }
                    catch { Add-BindingNativeCaptureError $diagnostic 'Fallback exit observation' $_.Exception.ToString() }
                }
            }
        }
        foreach ($pair in @(@{name='stdout';pump=$stdoutPump},@{name='stderr';pump=$stderrPump})) {
            if ($null -eq $pair.pump) { continue }
            try {
                $pair.pump.CancelUnattached()
                $capture=$pair.pump.Completion.GetAwaiter().GetResult()
                if ($pair.name -eq 'stdout') { $stdoutResult=$capture } else { $stderrResult=$capture }
            } catch { Add-BindingNativeCaptureError $diagnostic ($pair.name+' completion') $_.Exception.ToString() }
            try { $pair.pump.Dispose() } catch { Add-BindingNativeCaptureError $diagnostic ($pair.name+' observer dispose') $_.Exception.ToString() }
        }
        if (-not $started -or $diagnostic.childExitObserved) {
            try { $process.Dispose() } catch { Add-BindingNativeCaptureError $diagnostic 'Process observer dispose' $_.Exception.ToString() }
        }
    }
    foreach ($pair in @(@{name='stdout';path=$stdoutPath;capture=$stdoutResult},@{name='stderr';path=$stderrPath;capture=$stderrResult})) {
        if ($null -eq $pair.capture) { continue }
        foreach ($errorText in $pair.capture.Errors) { Add-BindingNativeCaptureError $diagnostic ($pair.name+' pump') $errorText }
        $diagnostic.captureErrorCount+=[Math]::Max(0,$pair.capture.ErrorCount-$pair.capture.Errors.Count)
        try { $log=Read-BindingNativeLog $pair.path $pair.capture }
        catch {
            Add-BindingNativeCaptureError $diagnostic ($pair.name+' bounded summary') $_.Exception.ToString()
            $log=[ordered]@{path=$pair.path;sha256=$null;bytes=$null;rawFileExists=$false;rawFileComplete=$false;totalBytes=$pair.capture.TotalBytes;writtenBytes=$pair.capture.WrittenBytes;eofObserved=$pair.capture.EofObserved;summaryBytes=$pair.capture.PrefixLength;summaryTruncated=$pair.capture.PrefixTruncated;summaryEncoding='utf-8-replacement-fallback';text=[Text.Encoding]::UTF8.GetString($pair.capture.Prefix,0,$pair.capture.PrefixLength);captureErrorCount=$pair.capture.ErrorCount}
        }
        try {
            $raw=Get-BindingNativeRawEvidence $pair.path
            $log.rawFileExists=$true; $log.bytes=$raw.bytes; $log.sha256=$raw.sha256
            $log.rawFileComplete=[bool]($pair.capture.RawFileComplete -and $raw.bytes -eq $pair.capture.TotalBytes)
        } catch { $log.rawFileComplete=$false; Add-BindingNativeCaptureError $diagnostic ($pair.name+' raw evidence') $_.Exception.ToString() }
        $diagnostic[$pair.name]=$log
    }
    $diagnostic.authorityReportPath=$authorityPath
    $diagnostic['purpose']=if ($null -eq $RecoveryContext) { 'INSTALL' } else { 'ENTRY_FAILURE_RECOVERY' }
    try { $diagnostic.authorityReportExists=[bool](Test-Path -LiteralPath $authorityPath -PathType Leaf -ErrorAction Stop) }
    catch { Add-BindingNativeCaptureError $diagnostic 'Authority presence observation' $_.Exception.ToString() }
    $installed=$null; $authorityError=''
    if ($diagnostic.authorityReportExists) {
        try {
            if ($null -eq $RecoveryContext) { $installed=Read-SetupAuthority $Identity }
            else {
                $installed=Get-Content -LiteralPath $authorityPath -Raw -Encoding UTF8|ConvertFrom-Json
                if ($installed.action -cne 'Restore' -or $installed.schema -ne 3 -or $installed.version -cne '0.1.0' -or $installed.setupInvocationId -cne $Identity.setupInvocationId -or $installed.transactionId -cne $RecoveryContext.transactionId -or $installed.engineSHA256 -cne $RecoveryContext.engineSHA256 -or $installed.backupDirectory -cne $RecoveryContext.backupDirectory) { throw 'Entry recovery report identity mismatch; no restoration claim.' }
            }
            $diagnostic.authorityValidated=$true; $diagnostic.authorityStatus=[string]$installed.status
            $diagnostic.targetMutationState='RECORDED_IN_VERIFIED_ENGINE_REPORT'
        } catch {
            # Rejected or partial JSON must never supply child diagnostics.
            $installed=$null
            $diagnostic.authorityValidated=$false
            $diagnostic['authorityStatus']=''
            $diagnostic.targetMutationState='UNKNOWN_AUTHORITY_NOT_VALIDATED'
            $authorityError=$_.Exception.ToString()
            Add-BindingNativeCaptureError $diagnostic 'Authority validation' $authorityError
        }
    } else { $diagnostic.targetMutationState='UNKNOWN_NO_AUTHORITY_REPORT' }
    $primary=if ($null -ne $installed -and $result -ne 0 -and $installed.error) { [string]$installed.error } elseif ($null -ne $diagnostic.stderr -and $diagnostic.stderr.text.Trim()) { $diagnostic.stderr.text.Trim() } elseif ($null -ne $diagnostic.stdout -and $diagnostic.stdout.text.Trim()) { $diagnostic.stdout.text.Trim() } elseif ($diagnostic.startException) { $diagnostic.startException } else { 'No child error output was captured.' }
    $diagnostic.originalPrimaryError=$primary
    $diagnostic.primaryError=([regex]::Replace($primary,'\s+',' ')).Trim()
    $fileWin32Error=-1
    if ($null -ne $installed -and $null -ne $installed.PSObject.Properties['availabilityChecks']) {
        $refusals=@($installed.availabilityChecks | Where-Object { $_.status -ceq 'REFUSED' })
        if ($refusals.Count -gt 0) {
            $diagnostic.availabilityFailure=$refusals[-1]
            if ($null -ne $refusals[-1].PSObject.Properties['win32Error']) { $fileWin32Error=[int]$refusals[-1].win32Error }
        }
    }
    $classification=Get-BindingFailureAdvice $diagnostic.primaryError $fileWin32Error $diagnostic.startException $diagnostic.authorityValidated $result
    $diagnostic.failureKind=$classification.failureKind; $diagnostic.guidance=$classification.guidance
    if ($classification.failureKind -ceq '') { $diagnostic.primaryError=''; $diagnostic.originalPrimaryError='' }
    try { Write-InstallerJson $diagnostic $metadataPath; $diagnostic.metadataWritten=$true }
    catch { Add-BindingNativeCaptureError $diagnostic 'Diagnostic metadata write' $_.Exception.ToString() }
    if ($diagnostic.metadataWritten) {
        try { $diagnostic.metadataSHA256=(Get-FileHash -LiteralPath $metadataPath -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant() }
        catch { Add-BindingNativeCaptureError $diagnostic 'Diagnostic metadata hash' $_.Exception.ToString() }
    }
    if (-not $started) { throw "$($diagnostic.primaryError) Native engine was not started. Diagnostic: $metadataPath. Target changes and recovery are not verified. Guidance: $($diagnostic.guidance)" }
    if (-not $diagnostic.childExitObserved) { throw "Native child exit was not observed. Preserve child PID $($diagnostic.childProcessId) and available diagnostics. Target changes and recovery are not verified." }
    if (-not $diagnostic.authorityValidated) { throw "$($diagnostic.primaryError) Native engine exit: $result. Its authority report was not created or validated. Diagnostic: $metadataPath. Target changes and recovery are not verified. $authorityError Guidance: $($diagnostic.guidance)" }
    $installed | Add-Member -NotePropertyName nativeEngine -NotePropertyValue $diagnostic -Force
    if ($null -ne $RecoveryContext) {
        if ($result -ne 0) { Write-Host "$($diagnostic.primaryError) Native recovery exit: $result. Diagnostic: $metadataPath. Guidance: $($diagnostic.guidance)" }
        return [ordered]@{ exitCode=[int]$result; report=$installed; reportPath=$authorityPath; nativeEngine=$diagnostic }
    }
    try { Write-SetupEvidence $installed $Identity; $script:bindingEngineReportPreserved=$true }
    catch { throw "$($diagnostic.primaryError) Native engine exit: $result. Verified engine status: $($installed.status). Core report publication failed: $($_.Exception.Message). Available authority: $($Identity.authoritativeReportPath)." }
    if ($result -ne 0) { Write-Host "$($diagnostic.primaryError) Native engine exit: $result. Diagnostic: $metadataPath. Failure kind: $($diagnostic.failureKind). Guidance: $($diagnostic.guidance)" }
    if ($diagnostic.captureErrorCount -gt 0) { Write-Host "Diagnostic capture is incomplete. The verified engine result is retained in $bindingSafeReport." }
    if ($null -ne $diagnostic.stdout -and $diagnostic.stdout.text) { Write-Host $diagnostic.stdout.text }
    return [int]$result
}
function Wait-SetupBindingGap($Identity,$Installed) {
    if ($bindingRequest.TestInstallBindingGapRole -cne 'A' -or -not $bindingRequest.TestInstallBindingGapRoot -or $Installed.status -ne 'SWAPPED_ENTRY_PENDING') { throw 'The controlled install gap requires role A and this pending private swap report.' }
    $gapRoot=Full-InstallerPath $bindingRequest.TestInstallBindingGapRoot
    $temp=Full-InstallerPath ([IO.Path]::GetTempPath())
    if ((Split-Path -Leaf $gapRoot) -cnotmatch '^JUST-install-binding-tests-[0-9a-f]{32}$' -or $temp -cne (Join-Path $gapRoot 'temp') -or -not $PSScriptRoot.StartsWith($temp+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'The install binding gap is limited to its dedicated temporary fixture.' }
    foreach ($path in @($PSScriptRoot,$Identity.authoritativeReportPath,$Identity.destination,$Identity.backupRoot,$Identity.publicReportPath,[string]$Installed.backup)) { if (-not (Full-InstallerPath $path).StartsWith($gapRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Every controlled install-gap path must remain in the isolated fixture.' } }
    $ready=Join-Path $gapRoot 'A-ready.json'; $release=Join-Path $gapRoot 'A-release.json'
    if ((Test-Path -LiteralPath $ready) -or (Test-Path -LiteralPath $release)) { throw 'The controlled gap requires new marker files; no blind repetition.' }
    Write-InstallerJson ([ordered]@{ schema=1; setupInvocationId=$Identity.setupInvocationId; transactionId=[string]$Installed.transactionId; backupDirectory=[string]$Installed.backup; authoritativeReportPath=$Identity.authoritativeReportPath; privateReportSHA256=(Get-FileHash -LiteralPath $Identity.authoritativeReportPath -Algorithm SHA256).Hash.ToLowerInvariant(); waitBoundSeconds=60 }) $ready
    $watch=[Diagnostics.Stopwatch]::StartNew()
    while ($watch.Elapsed.TotalSeconds -lt 60) {
        if (Test-Path -LiteralPath $release -PathType Leaf) {
            $null=Full-InstallerPath $release
            $decision=Get-Content -LiteralPath $release -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($decision.setupInvocationId -cne $Identity.setupInvocationId) { throw 'The controlled release belongs to another Setup invocation.' }
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'The controlled install binding gap timed out after 60 seconds; preserve this known transaction.'
}
function Resolve-BindingEntry {
    $actual=Full-InstallerPath $bindingRequest.EntryPath
    $process=Get-CimInstance Win32_Process -Filter "ProcessId=$($bindingRequest.EntryProcessId)"
    if ($null -eq $process -or (Full-InstallerPath ([string]$process.ExecutablePath)) -ine $actual) { throw 'Actual uninstaller process/module does not match the request.' }
    $stamp=Read-BindingTrailer $actual
    if ($stamp.schema -ne 1 -or $stamp.engineSHA256 -cne $bindingRequest.ExpectedEngineSHA256 -or $stamp.bindingSHA256 -notmatch '^[0-9a-f]{64}$') { throw 'Uninstaller transaction trailer identity mismatch.' }
    $backup=Full-InstallerPath ([string]$stamp.backupDirectory)
    Protect-BindingReport $backup
    $original=Join-Path $backup 'JUST-uninstall.exe'
    if ([string]$stamp.originalUninstallerPath -cne $original -or [string]$stamp.transactionId -cne (Split-Path -Leaf $backup) -or (Full-InstallerPath $bindingRequest.ClaimedDirectory) -ine $backup) { throw 'Uninstaller original transaction directory mismatch; external destination/transaction overrides are refused.' }
    # NSIS normally self-copies to its restricted TEMP/~nsu<hex>.tmp/Un.exe.
    # Only that native layout or the exact original module is supported. The
    # actual process path, raw command line and original full bytes are checked.
    # This is accidental-mis-binding protection, not administrator authentication.
    $launchMode='ORIGINAL_DIRECT'
    if ($actual -ine $original) {
        $temp=Full-InstallerPath ([IO.Path]::GetTempPath())
        $shadowParent=Split-Path -Parent $actual
        if ((Split-Path -Leaf $actual) -cne 'Un.exe' -or (Split-Path -Leaf $shadowParent) -cnotmatch '^~nsu[0-9A-Fa-f]+\.tmp$' -or (Split-Path -Parent $shadowParent) -ine $temp) { throw 'Moved uninstaller refused: actual module is neither its original entry nor a supported NSIS shadow copy.' }
        $expectedTail=' _?='+$backup+'\'
        if (-not ([string]$process.CommandLine).EndsWith($expectedTail,[StringComparison]::OrdinalIgnoreCase)) { throw 'NSIS shadow-copy original-directory command line mismatch.' }
        $launchMode='NSIS_NATIVE_SHADOW_COPY'
    }
    if (-not (Test-Path -LiteralPath $original -PathType Leaf) -or (Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash -ine (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash) { throw 'Original generated uninstaller is missing or differs from the actual running module.' }
    # A safe early refusal log is fixed by the original generated EXE. The
    # caller's requested report remains unwritable until full context/binding
    # validation succeeds. Missing/corrupt journals cannot overwrite a target.
    $stampDestination=Full-InstallerPath ([string]$stamp.destination)
    $earlyDirectory=Full-InstallerPath ([string]$stamp.earlyErrorDirectory)
    $earlyPath=Join-Path $earlyDirectory ('uninstall-early-error-'+$stamp.setupInvocationId+'-'+$bindingRequest.EntryProcessId+'.json')
    Assert-InstallerSeparate $earlyPath $backup; Assert-InstallerSeparate $earlyPath $stampDestination
    $script:bindingEarlyErrorPath=$earlyPath
    $bindingReport.setupInvocationId=$stamp.setupInvocationId
    $bindingReport.earlyErrorReportPath=$bindingEarlyErrorPath
    $context=Get-BindingContext $backup
    Protect-BindingReport $context.destination
    if ($context.schema -eq 3) {
        # A context-proven retained tree must also exclude the fallback error
        # report. Disable that fallback before any overlap assertion can throw.
        $script:bindingEarlyErrorPath=''
        Protect-BindingReport $context.swapRoot
        Assert-InstallerSeparate $earlyPath $context.swapRoot
        if ($bindingRequest.TransferPath) { Assert-InstallerSeparate (Full-InstallerPath $bindingRequest.TransferPath) $context.swapRoot }
        $script:bindingEarlyErrorPath=$earlyPath
    }
    if ($context.engineSHA256 -cne $bindingRequest.ExpectedEngineSHA256 -or $context.setupInvocationId -cne $stamp.setupInvocationId -or $context.setupInvocationId -cnotmatch '^[0-9a-f]{32}$' -or $context.destination -cne $stampDestination) { throw 'Transaction engine/Setup invocation identity differs from the generated entry.' }
    $fixed=Join-Path $backup 'transaction-binding.json'
    Assert-InstallerRestoreBinding $context $fixed ([string]$stamp.bindingSHA256)
    $script:bindingReportWritable=$true
    $bindingReport.backup=$backup; $bindingReport.backupDirectory=$backup; $bindingReport.transactionId=$context.transactionId; $bindingReport.setupInvocationId=$context.setupInvocationId; $bindingReport.destination=$context.destination; $bindingReport.selected=@($context.selected); $bindingReport.engineSHA256=$context.engineSHA256; $bindingReport.bindingSHA256=$stamp.bindingSHA256; $bindingReport.uninstallerPath=$original; $bindingReport.actualModulePath=$actual; $bindingReport.uninstallerSHA256=(Get-FileHash -LiteralPath $actual -Algorithm SHA256).Hash.ToLowerInvariant(); $bindingReport.launchMode=$launchMode
    return [ordered]@{ context=$context; bindingPath=$fixed; bindingSHA256=[string]$stamp.bindingSHA256 }
}
try {
    if (-not [Environment]::Is64BitProcess) { throw 'Native x64 PowerShell is required.' }
    # A GUI-launched native child can inherit a foreign PowerShell module
    # search path. Use only its Windows-native modules in this process and
    # inherited engine children; no user or machine environment is changed.
    $nativeModuleRoot=[IO.Path]::Combine($PSHOME,'Modules')
    $env:PSModulePath=$nativeModuleRoot
    foreach ($nativeModuleName in @('Microsoft.PowerShell.Utility','Microsoft.PowerShell.Management','CimCmdlets')) {
        Import-Module -Name ([IO.Path]::Combine($nativeModuleRoot,$nativeModuleName,$nativeModuleName+'.psd1')) -Force -ErrorAction Stop
    }
    if ($bindingRequest.RequestPath) {
        $requestFile=Assert-BindingBasicPath $bindingRequest.RequestPath
        if ($requestFile -ine (Join-Path $PSScriptRoot 'binding-request.ini')) { throw 'Only the fixed request in the resource extraction directory is supported.' }
        $allowed=@('ManifestSHA256','InstallReportPath','ExpectedDestination','ExpectedSelected','TransferPath','EntryPath','ClaimedDirectory','EntryProcessId','ReportPath','TestRestoreBoundarySlug','IsolationTestRoot','BackupRoot','SetupInvocationId','TestInstallBindingGapRoot','TestInstallBindingGapRole')
        $seen=@{}; $section=$false
        foreach ($line in [IO.File]::ReadAllLines($requestFile)) {
            if ($line -ceq '[Request]') { if ($section) { throw 'Duplicate request section.' }; $section=$true; continue }
            if ($line -eq '') { continue }
            if (-not $section -or $line -notmatch '^([A-Za-z][A-Za-z0-9]*)=(.*)$' -or $Matches[1] -cnotin $allowed -or $seen.ContainsKey($Matches[1])) { throw 'Malformed or unsupported binding request.' }
            $key=$Matches[1]; $value=$Matches[2]; $seen[$key]=$true
            $bindingRequest[$key]=if ($key -eq 'EntryProcessId') { if ($value -eq '') { 0 } else { [int]$value } } else { $value }
        }
        if ($seen.Count -ne $allowed.Count) { throw 'Incomplete fixed binding request.' }
        $bindingRequest.EnginePath=Join-Path $PSScriptRoot 'Installer-Engine.ps1'
    }
    $preparedInvocation=[Guid]::NewGuid().ToString('N')
    $requested=if ($bindingRequest.Operation -eq 'PrepareInstall' -and -not $bindingRequest.ReportPath) { Join-Path $env:ProgramData ('JUST\Windows-preview-logs\installer-'+$preparedInvocation+'.json') } else { $bindingRequest.ReportPath }
    $bindingSafeReport=Assert-BindingBasicPath $requested
    # Refuse overwriting the actual entry or any files in its claimed backup.
    if ($bindingRequest.ClaimedDirectory) {
        $claimed=Assert-BindingBasicPath $bindingRequest.ClaimedDirectory
        if ($bindingSafeReport.Equals($claimed,[StringComparison]::OrdinalIgnoreCase) -or $bindingSafeReport.StartsWith($claimed+'\',[StringComparison]::OrdinalIgnoreCase)) { $bindingSafeReport=''; throw 'Report must remain outside the transaction directory.' }
    }
    $trusted=Assert-BindingBasicPath $bindingRequest.EnginePath
    if ($bindingRequest.ExpectedEngineSHA256 -notmatch '^[0-9a-f]{64}$' -or (Get-FileHash -LiteralPath $trusted -Algorithm SHA256).Hash -ine $bindingRequest.ExpectedEngineSHA256) { throw 'Embedded trusted engine SHA256 verification failed.' }
    . $trusted -LibraryOnly
    $bindingSafeReport=Full-InstallerPath $bindingSafeReport
    if ($bindingRequest.Operation -eq 'PrepareInstall') {
        $privateReport=Join-Path $PSScriptRoot 'install-authority.json'
        $invocationFile=Join-Path $PSScriptRoot 'setup-invocation.json'
        if ((Full-InstallerPath $bindingRequest.InstallReportPath) -cne $privateReport -or (Test-Path -LiteralPath $privateReport) -or (Test-Path -LiteralPath $invocationFile)) { throw 'A fresh private Setup resource directory and authority report are required.' }
        $destination=Full-InstallerPath $bindingRequest.ExpectedDestination; $backups=Full-InstallerPath $bindingRequest.BackupRoot
        foreach ($path in @($destination,$backups)) { Assert-InstallerSeparate $PSScriptRoot $path; Protect-BindingReport $path }
        $selection=@(if ($bindingRequest.ExpectedSelected -eq 'all') { @($script:Slugs) } else { @($bindingRequest.ExpectedSelected.Split(',')) })
        $seen=@{}; foreach ($slug in $selection) { if ($slug -cnotin $script:Slugs -or $seen.ContainsKey($slug)) { throw 'Invalid Setup ordered selection.' }; $seen[$slug]=$true }
        if ($selection.Count -lt 1) { throw 'Empty Setup selection.' }
        $script:bindingReportWritable=$true
        $identity=[ordered]@{ schema=1; setupInvocationId=$preparedInvocation; authoritativeReportPath=$privateReport; engineSHA256=$bindingRequest.ExpectedEngineSHA256; candidateManifestSHA256=$bindingRequest.ManifestSHA256; destination=$destination; backupRoot=$backups; selected=$selection; publicReportPath=$bindingSafeReport; preparedUtc=[DateTime]::UtcNow.ToString('o') }
        Write-InstallerJson $identity $invocationFile
        [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'invocation.ini'),("[Invocation]`r`nid=$($identity.setupInvocationId)`r`nreportPath=$bindingSafeReport`r`n"),[Text.Encoding]::Unicode)
        exit 0
    }
    if ($bindingRequest.Operation -eq 'InvokeInstall') { $setupIdentity=Read-SetupInvocation; $result=Invoke-SetupEngine $setupIdentity; exit $result }
    if ($bindingRequest.Operation -in @('PublishInstall','WaitBindingGap','Create','Finalize','RollbackInstall')) { $setupIdentity=Read-SetupInvocation; $installed=Read-SetupAuthority $setupIdentity }
    if ($bindingRequest.Operation -eq 'PublishInstall') { Write-SetupEvidence $installed $setupIdentity; exit 0 }
    if ($bindingRequest.Operation -eq 'WaitBindingGap') { Wait-SetupBindingGap $setupIdentity $installed; exit 0 }
    if ($bindingRequest.Operation -eq 'RollbackInstall') {
        # Recovery authority comes only from this Setup's private report and
        # immutable context. A shared external REPORT never authorizes a move.
        if ($installed.status -notin @('SWAPPED_ENTRY_PENDING','INSTALLED_ENTRY_PENDING','INSTALLER_ENTRY_FAILED','INSTALLED')) { throw 'No precisely identified swapped installation is available for entry-failure recovery.' }
        $backup=Full-InstallerPath ([string]$installed.backup)
        $snapshot=Read-InstallerJournalSnapshot (Join-Path $backup 'installer-transaction.json')
        $bindingContext=Get-InstallerStableContext $snapshot.journal $backup
        foreach ($other in @($backup,$bindingContext.destination,$bindingContext.swapRoot)) { Protect-BindingReport $other }
        if ($bindingContext.schema -ne 3 -or $bindingContext.setupInvocationId -cne $setupIdentity.setupInvocationId -or $bindingContext.engineSHA256 -cne $setupIdentity.engineSHA256 -or $bindingContext.candidateManifestSHA256 -cne $setupIdentity.candidateManifestSHA256 -or $bindingContext.destination -cne $setupIdentity.destination -or ($bindingContext.selected -join ',') -cne ($setupIdentity.selected -join ',') -or $installed.transactionId -cne $bindingContext.transactionId) { throw 'Entry recovery private transaction identity mismatch.' }
        # Reuse the existing concurrent native stdout/stderr pumps, including
        # their logger-I/O fallback and observed child exit. A missing recovery
        # report never replaces the captured original child diagnostic.
        $recoveryCall=Invoke-SetupEngine $setupIdentity $bindingContext
        $recoveryPath=$recoveryCall.reportPath; $recoveryExit=$recoveryCall.exitCode; $recovery=$recoveryCall.report
        $installed | Add-Member -NotePropertyName entryRecovery -NotePropertyValue ([ordered]@{ reportPath=$recoveryPath; reportSHA256=(Get-FileHash -LiteralPath $recoveryPath).Hash.ToLowerInvariant(); exitCode=$recoveryExit; status=$recovery.status; originalFilesRestored=($recoveryExit -eq 0 -and $recovery.status -in @('ROLLED_BACK','RESTORED')); recoveryReport=$recovery }) -Force
        $installed.status=if ($installed.entryRecovery.originalFilesRestored) { 'ROLLED_BACK_AFTER_ENTRY_FAILURE' } else { 'ROLLBACK_BLOCKED' }
        Write-SetupEvidence $installed $setupIdentity
        $script:bindingEngineReportPreserved=$true
        Write-Output "Entry recovery: $($installed.status). Original error and all retained backup/stage/park objects are recorded in $bindingSafeReport."
        exit $(if ($installed.entryRecovery.originalFilesRestored) { 0 } else { 1 })
    }
    if ($bindingRequest.Operation -in @('Create','Finalize')) {
        if ($installed.status -notin @('SWAPPED_ENTRY_PENDING','INSTALLED_ENTRY_PENDING') -or $installed.action -ne 'Install') { throw 'A precisely identified pending swap report is required; installation is not complete yet.' }
        $backup=Full-InstallerPath ([string]$installed.backup)
        $bindingSnapshot=Read-InstallerJournalSnapshot (Join-Path $backup 'installer-transaction.json')
        if ($bindingSnapshot.journal.status -cne 'SWAPPED_ENTRY_PENDING') { throw 'Create/Finalize require this invocation journal to be exactly SWAPPED_ENTRY_PENDING.' }
        $bindingContext=Get-InstallerStableContext $bindingSnapshot.journal $backup
        Lock-InstallerDestination $bindingContext.destination
        Protect-BindingReport $backup; Protect-BindingReport $bindingContext.destination
        Protect-BindingReport $bindingContext.swapRoot
        if ($bindingContext.setupInvocationId -cne $setupIdentity.setupInvocationId -or $bindingContext.engineSHA256 -cne $bindingRequest.ExpectedEngineSHA256 -or $bindingContext.candidateManifestSHA256 -cne $bindingRequest.ManifestSHA256 -or $bindingContext.destination -ine (Full-InstallerPath $bindingRequest.ExpectedDestination)) { throw 'Installed Setup invocation, manifest, engine or destination identity mismatch.' }
        $expected=@(if ($bindingRequest.ExpectedSelected -eq 'all') { @($script:Slugs) } else { @($bindingRequest.ExpectedSelected.Split(',')) })
        if (($bindingContext.selected -join ',') -cne ($expected -join ',') -or ($installed.selected -join ',') -cne ($bindingContext.selected -join ',') -or [string]$installed.transactionId -cne $bindingContext.transactionId) { throw 'Installed selection/transaction identity mismatch.' }
        Assert-BindingInstalled $bindingContext
        if ((Get-FileHash -LiteralPath $bindingContext.journalPath -Algorithm SHA256).Hash -ine $bindingSnapshot.sha256) { throw 'Journal changed after the install authority snapshot.' }
        $fixed=Join-Path $backup 'transaction-binding.json'; $entry=Join-Path $backup 'JUST-uninstall.exe'
        if ($bindingRequest.Operation -eq 'Create') {
            if ((Test-Path -LiteralPath $fixed) -or (Test-Path -LiteralPath $entry)) { throw 'A new transaction must not already contain an uninstall binding/entry.' }
            $installed.status='INSTALLED_ENTRY_PENDING'
            $installed | Add-Member -NotePropertyName installerCompletion -NotePropertyValue 'UNINSTALLER_PENDING' -Force
            Write-SetupEvidence $installed $setupIdentity
            Write-InstallerJson $bindingContext $fixed
            Write-BindingTransfer $bindingContext
        } else {
            if ((Full-InstallerPath $bindingRequest.EntryPath) -cne $entry -or -not (Test-Path -LiteralPath $entry -PathType Leaf)) { throw 'WriteUninstaller did not create the fixed entry in this transaction.' }
            $digest=(Get-FileHash -LiteralPath $fixed -Algorithm SHA256).Hash.ToLowerInvariant()
            Assert-InstallerRestoreBinding $bindingContext $fixed $digest
            $stamp=[ordered]@{ schema=1; originalUninstallerPath=$entry; backupDirectory=$backup; destination=$bindingContext.destination; transactionId=$bindingContext.transactionId; setupInvocationId=$bindingContext.setupInvocationId; bindingSHA256=$digest; engineSHA256=$bindingContext.engineSHA256; earlyErrorDirectory=(Split-Path -Parent $bindingSafeReport) }
            $data=[Text.Encoding]::UTF8.GetBytes(($stamp | ConvertTo-Json -Compress))
            if ($data.Length -gt 8192) { throw 'Transaction trailer is too large.' }
            $stream=[IO.File]::Open($entry,[IO.FileMode]::Append,[IO.FileAccess]::Write,[IO.FileShare]::Read)
            try { $stream.Write($data,0,$data.Length); $length=[BitConverter]::GetBytes([uint32]$data.Length); $stream.Write($length,0,$length.Length); $stream.Write($bindingTrailerMagic,0,$bindingTrailerMagic.Length) } finally { $stream.Dispose() }
            $readback=Read-BindingTrailer $entry
            if ((Get-InstallerJsonSHA256 $readback) -cne (Get-InstallerJsonSHA256 $stamp)) { throw 'Generated entry trailer readback mismatch.' }
            $completion=[ordered]@{ completionId=[Guid]::NewGuid().ToString('N'); uninstallerPath=$entry; uninstallerSHA256=(Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash.ToLowerInvariant(); bindingPath=$fixed; bindingSHA256=$digest }
            Complete-InstallerLiveTransaction $bindingContext $completion
            $installed.status='INSTALLED'; $installed.installerCompletion='READY'
            foreach ($pair in @(@('uninstallerPath',$entry),@('uninstallerSHA256',(Get-FileHash -LiteralPath $entry -Algorithm SHA256).Hash.ToLowerInvariant()),@('bindingPath',$fixed),@('bindingSHA256',$digest))) { $installed | Add-Member -NotePropertyName $pair[0] -NotePropertyValue $pair[1] -Force }
            Write-SetupEvidence $installed $setupIdentity
        }
        Write-Output "$($bindingRequest.Operation): $entry"
        exit 0
    }
    $resolved=Resolve-BindingEntry; $bindingContext=$resolved.context
    if ($bindingRequest.Operation -eq 'Restore') {
        $bindingReport.RestoreCalled=$true; $bindingReport.status='RESTORE_STARTED'; Write-BindingReport
        $native=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $args=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$trusted,'-Action','Restore','-BackupDir',$bindingContext.backupDirectory,'-BindingPath',$resolved.bindingPath,'-ExpectedBindingSHA256',$resolved.bindingSHA256,'-ReportPath',$bindingSafeReport)
        if ($bindingRequest.TestRestoreBoundarySlug) { $args+=@('-TestRestoreBoundarySlug',$bindingRequest.TestRestoreBoundarySlug,'-IsolationTestRoot',$bindingRequest.IsolationTestRoot) }
        $bindingEngineReportPreserved=$true
        $nativePreference=$ErrorActionPreference
        try { $ErrorActionPreference='Continue'; & $native @args; $result=$LASTEXITCODE }
        finally { $ErrorActionPreference=$nativePreference }
        $engineReport=Get-Content -LiteralPath $bindingSafeReport -Raw -Encoding UTF8 | ConvertFrom-Json
        foreach ($key in @('RestoreCalled','uninstallerPath','actualModulePath','uninstallerSHA256','launchMode','bindingSHA256')) { $engineReport | Add-Member -NotePropertyName $key -NotePropertyValue $bindingReport[$key] -Force }
        Write-InstallerJson $engineReport $bindingSafeReport
        exit $result
    }
    $bindingReport.status=if ($bindingRequest.Operation -eq 'Cancel') { 'CANCELLED' } else { 'READY_TO_CONFIRM' }
    Write-BindingTransfer $bindingContext; Write-BindingReport
    exit 0
} catch {
    $bindingReport.status=if ($bindingRequest.Operation -in @('Create','Finalize')) { 'INSTALLER_ENTRY_FAILED' } else { 'REFUSED' }
    if ($bindingRequest.Operation -in @('PrepareInstall','InvokeInstall','PublishInstall','WaitBindingGap','Create','Finalize','RollbackInstall')) { $bindingReport.action='Install'; $bindingReport.setupInvocationId=$bindingRequest.SetupInvocationId }
    $bindingReport.error=$_.Exception.Message
    # Failed entry generation is not a complete successful Setup. The known
    # transaction and all backups remain for an explicit scoped recovery.
    if ($null -ne $bindingContext) { $bindingReport.backup=$bindingContext.backupDirectory; $bindingReport.backupDirectory=$bindingContext.backupDirectory; $bindingReport.destination=$bindingContext.destination; $bindingReport.selected=@($bindingContext.selected); $bindingReport.transactionId=$bindingContext.transactionId; $bindingReport.engineSHA256=$bindingContext.engineSHA256; $bindingReport.candidateManifestSHA256=$bindingContext.candidateManifestSHA256 }
    if ($bindingRequest.Operation -in @('Create','Finalize') -and (Get-Variable -Name setupIdentity -ErrorAction SilentlyContinue) -and (Get-Variable -Name installed -ErrorAction SilentlyContinue)) {
        # Persist the original entry failure alongside the verified private
        # swap authority before the separate normal recovery operation.
        try {
            $installed | Add-Member -NotePropertyName installerEntryFailure -NotePropertyValue ([ordered]@{ operation=$bindingRequest.Operation; error=$bindingReport.error; previousStatus=$installed.status; utc=[DateTime]::UtcNow.ToString('o') }) -Force
            $installed.status='INSTALLER_ENTRY_FAILED'
            Write-SetupEvidence $installed $setupIdentity
            $script:bindingEngineReportPreserved=$true
        } catch { Write-Warning 'Could not persist entry-failure metadata; prior private authority and native error output are preserved.' }
    }
    try { Write-BindingReport } catch { Write-Warning 'Could not write the external binding report.' }
    Write-Error "$($bindingReport.status): $($bindingReport.error)" -ErrorAction Continue
    exit 1
} finally {
    if (Get-Command -Name Unlock-InstallerDestination -CommandType Function -ErrorAction SilentlyContinue) { Unlock-InstallerDestination }
}
