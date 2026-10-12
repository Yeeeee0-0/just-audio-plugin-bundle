# Actual NSIS uninstaller tests. Every fixture is created below this worker's D TEMP.
# PrepareGui reuses the new Test-Installer default-all transaction; the root performs GUI cancel.
# Complete verifies that cancellation preserved the prepared state, then invokes actual EXEs /S.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CandidateDir,
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [Parameter(Mandatory=$true)][string]$EvidenceDir,
    [ValidateSet('PrepareGui','Complete','ContinueAfterFixtureOrder','VerifyCompletedEvidence')][string]$Phase='Complete',
    [string]$PreparedAllReportPath,
    [string]$GuiCancelEvidencePath,
    [string]$PriorEvidenceDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'Native Windows x64 PowerShell required.' }
if (Get-Process reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'A host is running; preserve it and stop.' }
$work=Get-Volume -DriveLetter D
if ($work.DriveType -ne 'Fixed' -or $work.FileSystem -ne 'NTFS' -or $work.FileSystemLabel -ne ([string][char]0x5DE5+[char]0x4F5C) -or $work.SizeRemaining -lt 20GB) { throw 'Verified fixed D Work volume with sufficient space required.' }
if ((Get-Volume -DriveLetter C).SizeRemaining -lt 10GB) { throw 'System drive space precondition failed.' }
$candidate=(Resolve-Path -LiteralPath $CandidateDir).Path
$installer=(Resolve-Path -LiteralPath $InstallerPath).Path
$evidence=[IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
$workerTemp=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
if (-not $workerTemp.StartsWith('D:\JUST-work\',[StringComparison]::OrdinalIgnoreCase) -or -not $evidence.StartsWith('D:\JUST-work\',[StringComparison]::OrdinalIgnoreCase)) { throw 'All new temporary and evidence paths must be on D Work.' }
if ($Phase -in @('ContinueAfterFixtureOrder','VerifyCompletedEvidence')) {
    if (-not $PriorEvidenceDir) { throw 'Exact prior evidence is required for this bounded phase.' }
    $priorEvidenceFull=[IO.Path]::GetFullPath($PriorEvidenceDir).TrimEnd('\')
    if ($evidence -ieq $workerTemp -or $evidence.StartsWith($workerTemp+'\',[StringComparison]::OrdinalIgnoreCase) -or $evidence -ieq $priorEvidenceFull -or $evidence.StartsWith($priorEvidenceFull+'\',[StringComparison]::OrdinalIgnoreCase) -or $priorEvidenceFull.StartsWith($evidence+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'New evidence must remain separate from prior evidence and all worker test transactions.' }
}
if ($Phase -eq 'VerifyCompletedEvidence' -and (Test-Path -LiteralPath $evidence)) { throw 'Completed evidence audit requires a fresh directory; no overwrite or repetition.' }
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$baselinePath=Join-Path $evidence 'uninstaller-gui-baseline.json'
$finalPath=Join-Path $evidence 'uninstaller-tests.json'
$slugs=@('eq','reverb','delay','tremolo','compressor','limiter','gate','flanger','fake_stereo','distortion')
$script:Checks=New-Object Collections.Generic.List[string]
$script:Cases=New-Object Collections.Generic.List[object]
$script:Continuation=$null
$installerSHA=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateManifest=Get-Content -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$maxRelativeLength=(@($candidateManifest.hashes.PSObject.Properties.Name | ForEach-Object { $_.Length } | Measure-Object -Maximum)).Maximum

function Check([bool]$Condition,[string]$Description) {
    if (-not $Condition) { throw "FAIL: $Description" }
    $script:Checks.Add($Description); Write-Host "PASS: $Description"
}
function Write-NewJson($Value,[string]$Path) {
    if (Test-Path -LiteralPath $Path) { throw 'Evidence already exists; no blind repetition.' }
    [IO.File]::WriteAllText($Path,($Value | ConvertTo-Json -Depth 80),[Text.UTF8Encoding]::new($false))
}
function Full([string]$Path) { return [IO.Path]::GetFullPath($Path).TrimEnd('\') }
function Inside([string]$Path,[string]$Root) { return (Full $Path).StartsWith((Full $Root)+'\',[StringComparison]::OrdinalIgnoreCase) }
function Assert-TestPath([string]$Path,[string]$Root) {
    if (-not (Inside $Path $Root)) { throw 'Synthetic path escaped its explicit temporary root.' }
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Optional-Hash([string]$Path) { if (Test-Path -LiteralPath $Path -PathType Leaf) { return Hash $Path };return $null }
function Preserve-RawReport([string]$Source,[string]$Name,[string]$Root) {
    Assert-TestPath $Source $Root
    $output=Join-Path $evidence ($Name+'.json')
    if (Test-Path -LiteralPath $output) { throw 'Raw report evidence already exists.' }
    [IO.File]::Copy($Source,$output,$false)
    return [ordered]@{Path=$output;SHA256=(Hash $output);OriginalPath=$Source;OriginalSHA256=(Hash $Source);AllSynthetic=$true}
}
function Same($A,$B) { return (($A | ConvertTo-Json -Depth 80 -Compress) -ceq ($B | ConvertTo-Json -Depth 80 -Compress)) }
function Canonical-RootState($State) {
    # Only filesystem snapshots are unordered sets/maps. Journal selections
    # and item arrays remain ordered and continue to use the original Same.
    $canonicalFiles=@($State.Files | ForEach-Object { [ordered]@{Name=$_.Name;SHA256=$_.SHA256} } | Sort-Object { $_['Name'] },{ $_['SHA256'] })
    return [ordered]@{Exists=$State.Exists;RootType=$State.RootType;Directories=@($State.Directories | Sort-Object);Files=$canonicalFiles;Reparse=@($State.Reparse | Sort-Object)}
}
function Same-RootState($A,$B) { return (Same (Canonical-RootState $A) (Canonical-RootState $B)) }
function Tree([string]$Path) {
    # Capture existence, root type, empty directories, files and reparse nodes without following links.
    $state=[ordered]@{Exists=$false;RootType='Absent';Directories=@();Files=@();Reparse=@()}
    if (-not (Test-Path -LiteralPath $Path)) { return $state }
    $rootItem=Get-Item -LiteralPath $Path -Force
    $state.Exists=$true
    if ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        $state.RootType='Reparse';$state.Reparse=@('$root');return $state
    }
    if (-not $rootItem.PSIsContainer) { $state.RootType='File';$state.Files=@([ordered]@{Name='$root';SHA256=(Hash $Path)});return $state }
    $state.RootType='Directory';$base=$rootItem.FullName.TrimEnd('\')+'\'
    $dirs=New-Object Collections.Generic.List[string];$files=New-Object Collections.Generic.List[object];$links=New-Object Collections.Generic.List[string]
    $pending=New-Object Collections.Generic.Stack[string];$pending.Push($rootItem.FullName)
    while ($pending.Count -gt 0) {
        foreach ($item in @(Get-ChildItem -LiteralPath $pending.Pop() -Force | Sort-Object Name)) {
            $name=$item.FullName.Substring($base.Length).Replace('\','/').ToLowerInvariant()
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { $links.Add($name);continue }
            if ($item.PSIsContainer) { $dirs.Add($name);$pending.Push($item.FullName) }
            else { $files.Add([ordered]@{Name=$name;SHA256=(Hash $item.FullName)}) }
        }
    }
    $state.Directories=@($dirs.ToArray() | Sort-Object);$state.Files=@($files.ToArray() | Sort-Object { $_['Name'] });$state.Reparse=@($links.ToArray() | Sort-Object)
    return $state
}
function NonSelected($State,[string[]]$Selected) {
    $prefixes=@($Selected | ForEach-Object { 'just_'+$_+'.vst3' })
    $keep={param($Name) $first=($Name -split '/')[0];return $first -notin $prefixes}
    return [ordered]@{Exists=$State.Exists;RootType=$State.RootType;Directories=@($State.Directories | Where-Object { & $keep $_ });Files=@($State.Files | Where-Object { & $keep $_.Name });Reparse=@($State.Reparse | Where-Object { & $keep $_ })}
}
function Evidence-State($Case) {
    $journalPath=Join-Path $Case.Backup 'installer-transaction.json'
    $journal=Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $stable=Get-Content -LiteralPath $journalPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $stable.PSObject.Properties.Remove('status')
    return [ordered]@{
        Target=(Tree $Case.Target);Previous=(Tree (Join-Path $Case.Backup 'previous'));
        EntireTransaction=(Tree $Case.Backup);JournalSHA256=(Hash $journalPath);JournalStatus=$journal.status;
        StableJournal=$stable;BindingSHA256=(Optional-Hash (Join-Path $Case.Backup 'transaction-binding.json'));
        UninstallerSHA256=(Hash $Case.Uninstaller);Fixtures=(Tree $Case.Fixtures);
        OtherCaseTarget=(Tree (Join-Path $Case.Root 'other-target'))
    }
}
function Quote([string]$Value) { if ($Value.Contains('"')) { throw 'Unexpected quote in process argument.' };return '"'+$Value+'"' }
function Invoke-Exe([string]$Exe,[string]$Arguments,[string]$Temp,[string]$Name,[switch]$CaptureShadow) {
    if (Get-Process reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'Host appeared; do not launch a test.' }
    $beforeTemp=$env:TEMP;$beforeTmp=$env:TMP;$beforeSHA=Hash $Exe
    Write-NewJson ([ordered]@{Name=$Name;Executable=$Exe;ExecutableSHA256=$beforeSHA;Arguments=$Arguments;TEMP=$Temp;CaptureOwnedShadow=[bool]$CaptureShadow;Utc=[DateTime]::UtcNow.ToString('o')}) (Join-Path $evidence ('invocation-'+$Name+'.json'))
    $stdout=Join-Path $evidence ($Name+'.stdout.log');$stderr=Join-Path $evidence ($Name+'.stderr.log')
    if ((Test-Path -LiteralPath $stdout) -or (Test-Path -LiteralPath $stderr)) { throw 'Process stream evidence already exists; do not overwrite.' }
    $p=$null;$shadow=$null;$shadowPath='';$shadowSHA='';$shadowPid=0
    try {
        $env:TEMP=$Temp;$env:TMP=$Temp
        $p=Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $null=$p.Handle
        $watch=[Diagnostics.Stopwatch]::StartNew()
        if ($CaptureShadow) {
            # /D keeps NSIS native self-copy behavior. Capture only this process's
            # direct Un.exe child in its exact synthetic TEMP; a parent exit 0
            # alone does not establish the actual uninstaller's refusal code.
            while ($watch.Elapsed.TotalSeconds -lt 60) {
                if ($null -eq $shadow) {
                    foreach ($owned in @(Get-CimInstance Win32_Process -Filter ("ParentProcessId="+$p.Id+" AND Name='Un.exe'"))) {
                        $module=Full ([string]$owned.ExecutablePath)
                        if (-not (Inside $module $Temp) -or (Split-Path -Leaf (Split-Path -Parent $module)) -cnotmatch '^~nsu[0-9A-Fa-f]+\.tmp$') { throw 'Owned NSIS shadow module escaped the exact synthetic TEMP layout.' }
                        try { $shadow=Get-Process -Id ([int]$owned.ProcessId) -ErrorAction Stop;$null=$shadow.Handle;$shadowPid=$shadow.Id;$shadowPath=$module;$shadowSHA=Hash $module } catch { throw 'Actual NSIS shadow child exited before its exit handle and bytes could be captured; result is unverified.' }
                        if ($shadowSHA -ne $beforeSHA) { throw 'Actual owned NSIS shadow bytes differ from the invoked original uninstaller.' }
                    }
                }
                $p.Refresh()
                if ($null -ne $shadow) { $shadow.Refresh();if ($p.HasExited -and $shadow.HasExited) { break } }
                Start-Sleep -Milliseconds 25
            }
            if ($null -eq $shadow -or -not $p.HasExited -or -not $shadow.HasExited) { throw 'Actual owned NSIS shadow exit was not captured; preserve running processes and evidence, do not kill or claim a result.' }
            $shadow.WaitForExit();$p.WaitForExit();$actualExit=$shadow.ExitCode
        } else { $p.WaitForExit();$actualExit=$p.ExitCode }
        $afterSHA=Hash $Exe
        if ($beforeSHA -ne $afterSHA) { throw 'Actual test EXE changed during its invocation.' }
        return [ordered]@{Name=$Name;Executable=$Exe;ExecutableSHA256=$beforeSHA;ExecutableSHA256After=$afterSHA;Arguments=$Arguments;TEMP=$Temp;ExitCode=$actualExit;ParentProcessId=$p.Id;ParentExitCode=$p.ExitCode;ShadowCaptured=[bool]$CaptureShadow;ShadowProcessId=$shadowPid;ShadowModulePath=$shadowPath;ShadowModuleSHA256=$shadowSHA;ShadowExitCode=if($CaptureShadow){$actualExit}else{$null};Stdout=$stdout;StdoutSHA256=(Hash $stdout);Stderr=$stderr;StderrSHA256=(Hash $stderr);Utc=[DateTime]::UtcNow.ToString('o');Mode='Actual EXE silent; no GUI claim'}
    } catch {
        Write-NewJson ([ordered]@{Name=$Name;Status='INVOCATION_NOT_VERIFIED';Error=$_.Exception.Message;Executable=$Exe;ExecutableSHA256=$beforeSHA;Arguments=$Arguments;TEMP=$Temp;ParentProcessId=if($null -ne $p){$p.Id}else{0};ShadowProcessId=$shadowPid;ShadowModulePath=$shadowPath;ShadowModuleSHA256=$shadowSHA;ProcessesNotTerminated=$true;Utc=[DateTime]::UtcNow.ToString('o')}) (Join-Path $evidence ('invocation-'+$Name+'-incomplete.json'))
        throw
    } finally { $env:TEMP=$beforeTemp;$env:TMP=$beforeTmp }
}
function New-Sentinel([string]$Path,[string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path,$Text,[Text.UTF8Encoding]::new($false))
}
function New-Case([string]$Name,[string[]]$Selected,[bool]$Old=$false) {
    # Logical names stay in evidence. Short independent physical case paths
    # keep candidate extraction, replacement stages and atomic JSON below the
    # native Windows PowerShell path budget, including the longest resource.
    $caseRoot=Join-Path $script:TestRoot ('c-'+[Guid]::NewGuid().ToString('N').Substring(0,12))
    if (Test-Path -LiteralPath $caseRoot) { throw 'Fresh case path already exists.' }
    foreach ($sub in @('temp','reports','fixtures')) { New-Item -ItemType Directory -Path (Join-Path $caseRoot $sub) | Out-Null }
    $case=[pscustomobject]@{Name=$Name;Root=$caseRoot;Target=(Join-Path $caseRoot 'VST3');BackupRoot=(Join-Path $caseRoot 'backups');Backup='';Uninstaller='';Temp=(Join-Path $caseRoot 'temp');Reports=(Join-Path $caseRoot 'reports');EarlyErrorDirectory=(Join-Path $caseRoot 'reports');SetupInvocationId='';Fixtures=(Join-Path $caseRoot 'fixtures');Selected=$Selected;Original=$null;InstallProcess=$null}
    if ($Name -eq 'delete-boundary-unknown-empty') {
        # The only production hook requires the exact GUID test root and its own temp child.
        $case.Temp=Join-Path $script:TestRoot 'temp'
        New-Item -ItemType Directory -Path $case.Temp -Force | Out-Null
    }
    if ($Old) {
        foreach ($slug in $slugs) {
            New-Sentinel (Join-Path $case.Target "Just_$slug.vst3\old-version-sentinel.bin") "SYNTHETIC-PREVIOUS-$slug"
            New-Item -ItemType Directory -Path (Join-Path $case.Target "Just_$slug.vst3\known-old-empty-directory") | Out-Null
        }
    }
    New-Sentinel (Join-Path $case.Target 'OtherVendor.vst3\keep.bin') 'SYNTHETIC-OTHER-VENDOR-UNCHANGED'
    New-Sentinel (Join-Path $case.Target 'unrelated.bin') 'SYNTHETIC-UNRELATED-UNCHANGED'
    foreach ($fixtureName in @('preset.fixture','configuration.fixture','license.fixture','project.fixture')) { New-Sentinel (Join-Path $case.Fixtures $fixtureName) ('SYNTHETIC-ONLY-'+$fixtureName) }
    $case.Original=Tree $case.Target
    $stageMax=$case.Target.Length+1+50+1+$maxRelativeLength
    $journalTempMax=$case.BackupRoot.Length+1+59+1+26+37
    Check ($stageMax -le 259 -and $journalTempMax -le 259) "$Name independent fixture supports full replacement-stage and atomic-journal paths"
    Write-NewJson ([ordered]@{Name=$Name;Selected=$Selected;Target=$case.Target;FullState=$case.Original;Fixtures=(Tree $case.Fixtures);AllSynthetic=$true}) (Join-Path $evidence ('snapshot-'+$Name+'-pre-install.json'))
    $installReport=Join-Path $case.Reports 'install.json'
    $args='/S /BACKUPROOT='+(Quote $case.BackupRoot)+' /REPORT='+(Quote $installReport)+' /SELECT='+($Selected -join ',')+' /D='+$case.Target
    $case.InstallProcess=Invoke-Exe $installer $args $case.Temp ($Name+'-install')
    Write-NewJson $case.InstallProcess (Join-Path $evidence ('process-'+$Name+'-install.json'))
    Write-NewJson ([ordered]@{Target=(Tree $case.Target);BackupRoot=(Tree $case.BackupRoot);Fixtures=(Tree $case.Fixtures)}) (Join-Path $evidence ('snapshot-'+$Name+'-post-install.json'))
    Check ($case.InstallProcess.ExitCode -eq 0) "$Name actual new Setup exits zero"
    Check (Test-Path -LiteralPath $installReport -PathType Leaf) "$Name installation report exists"
    $record=Get-Content -LiteralPath $installReport -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($record.status -eq 'INSTALLED') "$Name actual new transaction installed"
    $case.SetupInvocationId=[string]$record.setupInvocationId
    Check ($record.installerCompletion -eq 'READY') "$Name actual Setup completed its bound uninstaller entry"
    Check ((@($record.selected) -join ',') -ceq ($Selected -join ',')) "$Name exact selected ordered set"
    $case.Backup=Full $record.backup;$case.Uninstaller=Join-Path $case.Backup 'JUST-uninstall.exe'
    Assert-TestPath $case.Backup $caseRoot
    Check ((Test-Path -LiteralPath $case.Uninstaller -PathType Leaf) -and (Test-Path -LiteralPath (Join-Path $case.Backup 'transaction-binding.json') -PathType Leaf)) "$Name actual per-transaction uninstaller and context created"
    Check ($record.uninstallerSHA256 -eq (Hash $case.Uninstaller) -and $record.bindingSHA256 -eq (Hash (Join-Path $case.Backup 'transaction-binding.json'))) "$Name installed report records the actual generated EXE and binding SHA256"
    $journal=Get-Content -LiteralPath (Join-Path $case.Backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($journal.status -ceq 'INSTALLED') "$Name successful entry retains exact INSTALLED journal gate"
    Check ((@($journal.items | ForEach-Object { $_.slug }) -join ',') -ceq ($Selected -join ',')) "$Name stable transaction selected set"
    $binding=Get-Content -LiteralPath (Join-Path $case.Backup 'transaction-binding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($record.setupInvocationId -cmatch '^[0-9a-f]{32}$' -and $record.setupInvocationId -ceq $journal.setupInvocationId -and $record.setupInvocationId -ceq $binding.setupInvocationId) "$Name actual Setup invocation nonce agrees with journal and bound context"
    foreach ($slug in $Selected) { Check (Same (Tree (Join-Path $case.Target ('Just_'+$slug+'.vst3'))) (Tree (Join-Path $candidate ('VST3\Just_'+$slug+'.vst3')))) "$Name installed $slug exact candidate directories/types/files" }
    Check (Same-RootState (NonSelected (Tree $case.Target) $Selected) (NonSelected $case.Original $Selected)) "$Name installation preserves every unselected and unrelated item"
    return $case
}
function Save-Case($Record) {
    $out=Join-Path $evidence ('case-'+$Record.Name+'.json')
    Write-NewJson $Record $out;$script:Cases.Add([ordered]@{Name=$Record.Name;Status=$Record.Status;Evidence=$out;EvidenceSHA256=(Hash $out)})
}
function Run-Uninstall($Case,[string]$Name,[string]$Extra='',[string]$Executable='',[switch]$CallerOverride,[switch]$CaptureShadow) {
    if (-not $Executable) { $Executable=$Case.Uninstaller }
    $report=Join-Path $Case.Reports ($Name+'.json')
    if (Test-Path -LiteralPath $report) { throw 'Uninstall report already exists; no repeat.' }
    $args='/S /REPORT='+(Quote $report)
    if ($Extra) { $args+=' '+$Extra }
    # Normal silent tests execute the exact original module directly, so its
    # exit code belongs to the actual uninstaller rather than the NSIS parent.
    # The explicit caller-override tests supply their own final NSIS argument.
    if (-not $CallerOverride) { $args+=' _?='+(Full (Split-Path -Parent $Executable)) }
    try { $process=Invoke-Exe $Executable $args $Case.Temp $Name -CaptureShadow:$CaptureShadow }
    finally {
        # Preserve an immediate full-state observation even if the child exit
        # cannot be established or an invocation guard throws. No retry/cleanup.
        Write-NewJson (Evidence-State $Case) (Join-Path $evidence ('snapshot-'+$Name+'-after-invocation-attempt.json'))
    }
    Write-NewJson $process (Join-Path $evidence ('process-'+$Name+'.json'))
    $actualPid=if($CaptureShadow){$process.ShadowProcessId}else{$process.ParentProcessId}
    $early=Join-Path $Case.EarlyErrorDirectory ('uninstall-early-error-'+$Case.SetupInvocationId+'-'+$actualPid+'.json')
    $resultPath='';$source='NONE_UNVERIFIED_DIAGNOSTICS';$record=$null;$sha=$null
    if (Test-Path -LiteralPath $report -PathType Leaf) { $resultPath=$report;$source='REQUESTED_EXPLICIT' }
    elseif (Test-Path -LiteralPath $early -PathType Leaf) { $resultPath=$early;$source='FIXED_TRUSTED_EARLY_ERROR' }
    if ($resultPath) {
        $record=Get-Content -LiteralPath $resultPath -Raw -Encoding UTF8 | ConvertFrom-Json;$sha=Hash $resultPath
        Check ($resultPath -eq $report -or $record.earlyErrorReportPath -eq $early) "$Name actual JSON is requested external report or original-entry fixed safe fallback"
    }
    return [ordered]@{Process=$process;Report=$record;ReportPath=$resultPath;ReportSHA256=$sha;ReportSource=$source;RequestedReportPath=$report;RequestedReportCreated=(Test-Path -LiteralPath $report -PathType Leaf);RestoreCalled=if($null -ne $record){$record.RestoreCalled}else{'UNVERIFIED: no trusted JSON diagnostics'}}
}
function Success($Case,[string]$Name,[bool]$Idempotent=$false) {
    $before=Evidence-State $Case;Write-NewJson $before (Join-Path $evidence ('snapshot-'+$Name+'-before.json'))
    $run=Run-Uninstall $Case $Name;$after=Evidence-State $Case;Write-NewJson $after (Join-Path $evidence ('snapshot-'+$Name+'-after.json'))
    Check ($run.Process.ExitCode -eq 0 -and $run.Report.status -eq 'RESTORED') "$Name actual EXE restores successfully"
    Check ([bool]$run.Report.RestoreCalled) "$Name actual EXE invoked its trusted bound restore engine"
    Check (Same-RootState $after.Target $Case.Original) "$Name exact original target directories/types/files restored"
    Check (Same $before.Previous $after.Previous) "$Name old backups retained exactly"
    Check (Same $before.Fixtures $after.Fixtures) "$Name synthetic settings/license/project fixtures unchanged"
    Check ($before.BindingSHA256 -eq $after.BindingSHA256 -and $before.UninstallerSHA256 -eq $after.UninstallerSHA256) "$Name binding and actual uninstaller retained"
    Check (Same $before.StableJournal $after.StableJournal) "$Name stable journal identity unchanged"
    Check ($after.JournalStatus -eq 'RESTORED') "$Name retained journal records restored"
    Save-Case ([ordered]@{Name=$Name;Status='PASSED';ActualEXE=$run;Install=$Case.InstallProcess;Selected=$Case.Selected;Idempotent=$Idempotent;Before=$before;After=$after;ExpectedOriginal=$Case.Original;RealTargetUsed=$false})
}
function Refusal($Case,[string]$Name,[string]$Extra='',[string]$Executable='',[switch]$Boundary,[switch]$CallerOverride,[switch]$CaptureShadow) {
    $before=Evidence-State $Case;Write-NewJson $before (Join-Path $evidence ('snapshot-'+$Name+'-before.json'))
    $run=Run-Uninstall $Case $Name $Extra $Executable -CallerOverride:$CallerOverride -CaptureShadow:$CaptureShadow
    $after=Evidence-State $Case;Write-NewJson $after (Join-Path $evidence ('snapshot-'+$Name+'-after.json'))
    Check ($run.Process.ExitCode -ne 0) "$Name actual executing uninstaller has observed nonzero refusal exit"
    if ($null -ne $run.Report) {
        Check ($run.Report.status -eq 'REFUSED') "$Name actual trusted report records REFUSED"
        if ($CallerOverride) { Check (-not [bool]$run.Report.RestoreCalled) "$Name actual trusted report confirms mismatched caller never invokes restore" }
    } else {
        Check ($Name -in @('refuse-moved-exe','refuse-foreign-original-directory','refuse-foreign-destination','refuse-moved-exe-original-directory')) "$Name entry cannot authorize a report path; capture exit/streams and do not invent JSON status or RestoreCalled"
    }
    if ($Boundary) {
        $unknown=Join-Path $Case.Target ('Just_'+$Case.Selected[0]+'.vst3\.resume25-boundary-unknown-empty')
        Check (Test-Path -LiteralPath $unknown -PathType Container) "$Name controlled unknown empty directory retained"
        $expected=Tree $Case.Target
        $expected.Directories=@($expected.Directories | Where-Object { $_ -ne ('just_'+$Case.Selected[0]+'.vst3/.resume25-boundary-unknown-empty') })
        Check (Same $before.Target $expected) "$Name deletion boundary preserves every pre-existing file and directory"
        Check ($null -ne $run.Report.PSObject.Properties['testRestoreBoundary']) "$Name records the actual deletion-boundary mutation"
    } else { Check (Same $before.Target $after.Target) "$Name full preflight refusal preserves every selected and unselected item" }
    Check (Same $before.EntireTransaction $after.EntireTransaction) "$Name rejected transaction and old backups unchanged"
    Check (Same $before.Fixtures $after.Fixtures) "$Name synthetic private-like fixtures unchanged"
    Check (Same $before.OtherCaseTarget $after.OtherCaseTarget) "$Name foreign synthetic target unchanged"
    Save-Case ([ordered]@{Name=$Name;Status='PASSED';ActualEXE=$run;Selected=$Case.Selected;Before=$before;After=$after;MutationAtDeletionBoundary=[bool]$Boundary;RealTargetUsed=$false;MissingTrustedDiagnostics=($null -eq $run.Report)})
}
function Remove-Synthetic([string]$Path,[string]$CaseRoot) {
    Assert-TestPath $Path $CaseRoot
    if (-not (Test-Path -LiteralPath $Path)) { throw 'Expected synthetic mutation path missing.' }
    Remove-Item -LiteralPath $Path -Recurse -Force
}
function Unsafe-Requested-Report($Case,[string]$Name) {
    # The deliberately untrusted journal cannot authorize any requested report
    # write. Do not parse the requested sentinel as JSON or demand a JSON result.
    $requested=Join-Path $Case.Target 'OtherVendor.vst3\keep.bin'
    $before=[ordered]@{Target=(Tree $Case.Target);EntireTransaction=(Tree $Case.Backup);Previous=(Tree (Join-Path $Case.Backup 'previous'));Fixtures=(Tree $Case.Fixtures);RequestedSentinelSHA256=(Hash $requested);UninstallerSHA256=(Hash $Case.Uninstaller)}
    Write-NewJson $before (Join-Path $evidence ('snapshot-'+$Name+'-before.json'))
    $args='/S /REPORT='+(Quote $requested)+' _?='+$Case.Backup
    try { $process=Invoke-Exe $Case.Uninstaller $args $Case.Temp $Name }
    finally {
        $after=[ordered]@{Target=(Tree $Case.Target);EntireTransaction=(Tree $Case.Backup);Previous=(Tree (Join-Path $Case.Backup 'previous'));Fixtures=(Tree $Case.Fixtures);RequestedSentinelSHA256=(Hash $requested);UninstallerSHA256=(Hash $Case.Uninstaller)}
        Write-NewJson $after (Join-Path $evidence ('snapshot-'+$Name+'-after.json'))
    }
    Write-NewJson $process (Join-Path $evidence ('process-'+$Name+'.json'))
    $safeEarly=Join-Path $Case.EarlyErrorDirectory ('uninstall-early-error-'+$Case.SetupInvocationId+'-'+$process.ParentProcessId+'.json')
    Check (Test-Path -LiteralPath $safeEarly -PathType Leaf) "$Name actual entry writes only its original fixed independent refusal log"
    $safeRecord=Get-Content -LiteralPath $safeEarly -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($safeRecord.status -eq 'REFUSED' -and -not [bool]$safeRecord.RestoreCalled) "$Name safe independent report records refusal before restore"
    $safeEvidence=Preserve-RawReport $safeEarly ($Name+'-safe-early-error') $Case.Root
    Check ($process.ExitCode -ne 0) "$Name actual original uninstaller refuses with observed nonzero exit"
    Check (Same $before $after) "$Name unsafe requested report cannot overwrite target sentinel or change any target/backup fixture"
    Save-Case ([ordered]@{Name=$Name;Status='PASSED';ActualEXE=$process;ObservedOutcome='NONZERO_REFUSAL_WITH_NO_UNTRUSTED_REQUESTED_REPORT_WRITE';RequestedReport=$requested;RequestedReportIsPreexistingSyntheticTargetSentinel=$true;RequestedReportReadAsJSON=$false;SafeIndependentStreamsCaptured=$true;SafeIndependentErrorReport=$safeEvidence;Before=$before;After=$after;RealTargetUsed=$false;AllSynthetic=$true})
}
function Long-Supported-Paths {
    $name='long-supported-structured-install';$root=Join-Path $workerTemp ('j-'+[Guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $root) { throw 'Fresh long-path fixture root already exists.' }
    function Padded-Child([string]$Base,[string]$Label,[int]$Length) {
        $start=Join-Path $Base $Label
        if ($start.Length -gt $Length) { throw 'Fixture base cannot meet supported path budget; do not label this a product failure.' }
        return $start+('x'*($Length-$start.Length))
    }
    $target=Padded-Child $root 'destination-' 136;$backupRoot=Padded-Child $root 'backup-' 134;$temp=Padded-Child $root 'temp-' 120
    $reports=Join-Path $root 'reports';$fixtures=Join-Path $root 'fixtures'
    foreach ($path in @($temp,$reports,$fixtures)) { New-Item -ItemType Directory -Path $path -Force | Out-Null }
    $longPrefix=Join-Path $reports 'readable-install-';$installReport=$longPrefix+('x'*(218-$longPrefix.Length-5))+'.json'
    $stageBudget=$target.Length+1+50+1+$maxRelativeLength;$journalBudget=$backupRoot.Length+1+59+1+26+37
    Check ($stageBudget -le 259 -and $journalBudget -le 259 -and ($installReport.Length+37) -le 259) 'Long path fixture includes supported replacement-stage, atomic journal and atomic readable-report budgets'
    New-Sentinel (Join-Path $target 'OtherVendor.vst3\keep.bin') 'SYNTHETIC-LONG-PATH-OTHER-VENDOR'
    New-Sentinel (Join-Path $fixtures 'configuration.fixture') 'SYNTHETIC-LONG-PATH-CONFIGURATION'
    $case=[pscustomobject]@{Name=$name;Root=$root;Target=$target;BackupRoot=$backupRoot;Backup='';Uninstaller='';Temp=$temp;Reports=$reports;EarlyErrorDirectory=$reports;SetupInvocationId='';Fixtures=$fixtures;Selected=$slugs;Original=(Tree $target);InstallProcess=$null}
    Write-NewJson ([ordered]@{Root=$root;Target=$target;BackupRoot=$backupRoot;TEMP=$temp;RequestedReport=$installReport;BeforeTarget=$case.Original;BeforeFixtures=(Tree $fixtures);PathLengths=[ordered]@{TEMP=$temp.Length;Target=$target.Length;BackupRoot=$backupRoot.Length;RequestedReport=$installReport.Length;ReplacementFileMaximum=$stageBudget;AtomicJournalMaximum=$journalBudget;AtomicReportMaximum=($installReport.Length+37)};AllSynthetic=$true}) (Join-Path $evidence 'snapshot-long-supported-pre-install.json')
    $arguments='/S /BACKUPROOT='+(Quote $backupRoot)+' /REPORT='+(Quote $installReport)+' /SELECT='+($slugs -join ',')+' /D='+$target
    $case.InstallProcess=Invoke-Exe $installer $arguments $temp ($name+'-install')
    Write-NewJson $case.InstallProcess (Join-Path $evidence ('process-'+$name+'-install.json'))
    Write-NewJson ([ordered]@{Target=(Tree $target);BackupRoot=(Tree $backupRoot);Fixtures=(Tree $fixtures)}) (Join-Path $evidence 'snapshot-long-supported-post-install.json')
    Check ($case.InstallProcess.ExitCode -eq 0 -and (Test-Path -LiteralPath $installReport -PathType Leaf)) 'Actual Setup accepts supported long paths through the structured install request'
    $record=Get-Content -LiteralPath $installReport -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($record.status -eq 'INSTALLED' -and $record.installerCompletion -eq 'READY' -and (@($record.selected) -join ',') -ceq ($slugs -join ',')) 'Actual long-path Setup completes all ten selected bundles and its bound uninstaller'
    $case.Backup=Full $record.backup;Assert-TestPath $case.Backup $root;$case.Uninstaller=Join-Path $case.Backup 'JUST-uninstall.exe';$case.SetupInvocationId=[string]$record.setupInvocationId
    $journal=Get-Content -LiteralPath (Join-Path $case.Backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $binding=Get-Content -LiteralPath (Join-Path $case.Backup 'transaction-binding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($journal.status -ceq 'INSTALLED' -and $case.SetupInvocationId -cmatch '^[0-9a-f]{32}$' -and $case.SetupInvocationId -ceq $journal.setupInvocationId -and $case.SetupInvocationId -ceq $binding.setupInvocationId) 'Long-path Setup journal and generated binding use this exact successful invocation'
    $source=Full ([string]$journal.source);Assert-TestPath $source $root
    $legacyEngine=Join-Path (Split-Path -Parent $source) 'Installer-Engine.ps1'
    $legacyPowerShell=Join-Path $env:WINDIR 'sysnative\WindowsPowerShell\v1.0\powershell.exe'
    $legacy=(Quote $legacyPowerShell)+' -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File '+(Quote $legacyEngine)+' -CandidateDir '+(Quote $source)+' -Destination '+(Quote $target)+' -BackupRoot '+(Quote $backupRoot)+' -Selected '+(Quote ($slugs -join ','))+' -ReportPath '+(Quote $installReport)
    $candidateFileMaximum=$source.Length+6+$maxRelativeLength
    Check ($legacy.Length -gt 1024) 'Actual recorded long-path invocation would exceed the old NSIS 1024-character concatenated engine command'
    Check ($candidateFileMaximum -le 259 -and $legacyEngine.Length -le 259) 'Actual extracted candidate and trusted engine remain within native supported per-file paths'
    foreach ($slug in $slugs) { Check (Same (Tree (Join-Path $target ('Just_'+$slug+'.vst3'))) (Tree (Join-Path $candidate ('VST3\Just_'+$slug+'.vst3')))) "Long structured request preserves exact $slug candidate directories/types/bytes" }
    Check (Same (NonSelected (Tree $target) $slugs) $case.Original) 'Long-path installation preserves every unrelated fixture'
    $installEvidence=Preserve-RawReport $installReport 'long-supported-install-report' $root
    Save-Case ([ordered]@{Name=$name;Status='PASSED';ActualSetup=$case.InstallProcess;InstallReport=$installEvidence;Invocation=$case.SetupInvocationId;FullState=(Evidence-State $case);LegacyCommand=$legacy;LegacyCommandLength=$legacy.Length;LegacyCommandNotExecuted=$true;StructuredActualInvocation=$true;PathBudgets=[ordered]@{CandidateMaximum=$candidateFileMaximum;ReplacementStageMaximum=$stageBudget;AtomicJournalMaximum=$journalBudget;AtomicRequestedReportMaximum=($installReport.Length+37)};RealTargetUsed=$false;AllSynthetic=$true})
    Success $case 'long-supported-actual-exe-restore'
}
function Shared-Report-Binding {
    # A stops after its successful engine has published the readable external
    # report, before Create. B deliberately reuses that path and target. Each
    # actual Setup must then bind its own private authoritative invocation.
    # This hook's required descriptive GUID basename is placed directly below
    # worker TEMP, so its candidate/staging paths remain supported by PS5.
    $root=Join-Path $workerTemp ('JUST-install-binding-tests-'+[Guid]::NewGuid().ToString('N'))
    foreach ($sub in @('temp','reports','reports\A','reports\B','fixtures')) { New-Item -ItemType Directory -Path (Join-Path $root $sub) -Force | Out-Null }
    $target=Join-Path $root 'VST3';$backupRoot=Join-Path $root 'backups';$temp=Join-Path $root 'temp';$shared=Join-Path $root 'reports\shared-install-report.json'
    foreach ($slug in $slugs) {
        New-Sentinel (Join-Path $target ('Just_'+$slug+'.vst3\old-version-sentinel.bin')) ('SYNTHETIC-SHARED-REPORT-PREVIOUS-'+$slug)
        New-Item -ItemType Directory -Path (Join-Path $target ('Just_'+$slug+'.vst3\known-old-empty-directory')) | Out-Null
    }
    New-Sentinel (Join-Path $target 'OtherVendor.vst3\keep.bin') 'SYNTHETIC-SHARED-REPORT-OTHER-VENDOR'
    New-Sentinel (Join-Path $root 'fixtures\configuration.fixture') 'SYNTHETIC-SHARED-REPORT-CONFIGURATION'
    $original=Tree $target;$fixtures=Tree (Join-Path $root 'fixtures')
    Write-NewJson ([ordered]@{Target=$target;Original=$original;Fixtures=$fixtures;Root=$root;AllSynthetic=$true}) (Join-Path $evidence 'snapshot-shared-report-initial.json')
    $aCase=[pscustomobject]@{Name='shared-report-A';Root=$root;Target=$target;BackupRoot=$backupRoot;Backup='';Uninstaller='';Temp=$temp;Reports=(Join-Path $root 'reports\A');EarlyErrorDirectory=(Join-Path $root 'reports');SetupInvocationId='';Fixtures=(Join-Path $root 'fixtures');Selected=@('eq');Original=$original;InstallProcess=$null}
    $bCase=[pscustomobject]@{Name='shared-report-B';Root=$root;Target=$target;BackupRoot=$backupRoot;Backup='';Uninstaller='';Temp=$temp;Reports=(Join-Path $root 'reports\B');EarlyErrorDirectory=(Join-Path $root 'reports');SetupInvocationId='';Fixtures=(Join-Path $root 'fixtures');Selected=@('eq');Original=$null;InstallProcess=$null}
    $common='/S /BACKUPROOT='+(Quote $backupRoot)+' /REPORT='+(Quote $shared)+' /SELECT=eq'
    $aArguments=$common+' /TESTINSTALLBINDINGGAPROOT='+(Quote $root)+' /TESTINSTALLBINDINGGAPROLE=A /D='+$target
    $beforeTemp=$env:TEMP;$beforeTmp=$env:TMP;$aProcess=$null;$aComplete=$false
    Write-NewJson ([ordered]@{Name='shared-report-A-install';Executable=$installer;ExecutableSHA256=$installerSHA;Arguments=$aArguments;TEMP=$temp;Utc=[DateTime]::UtcNow.ToString('o');AllSynthetic=$true}) (Join-Path $evidence 'invocation-shared-report-A-install.json')
    try {
        if (Get-Process reaper,reaper_host32,reaper_host64 -ErrorAction SilentlyContinue) { throw 'Host appeared; preserve it and do not launch the shared-report test.' }
        try { $env:TEMP=$temp;$env:TMP=$temp;$aProcess=Start-Process -FilePath $installer -ArgumentList $aArguments -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $evidence 'shared-report-A-install.stdout.log') -RedirectStandardError (Join-Path $evidence 'shared-report-A-install.stderr.log');$null=$aProcess.Handle }
        finally { $env:TEMP=$beforeTemp;$env:TMP=$beforeTmp }
        $ready=Join-Path $root 'A-ready.json';$release=Join-Path $root 'A-release.json';$watch=[Diagnostics.Stopwatch]::StartNew()
        while (-not (Test-Path -LiteralPath $ready -PathType Leaf) -and $watch.Elapsed.TotalSeconds -lt 50) {
            $aProcess.Refresh();if ($aProcess.HasExited) { throw 'Actual Setup A exited before its controlled binding-gap marker.' }
            Start-Sleep -Milliseconds 50
        }
        Check (Test-Path -LiteralPath $ready -PathType Leaf) 'Shared report A reaches actual post-engine/pre-Create controlled gap'
        $mark=Get-Content -LiteralPath $ready -Raw -Encoding UTF8 | ConvertFrom-Json
        $readyEvidence=Preserve-RawReport $ready 'shared-report-A-ready' $root
        $idA=[string]$mark.setupInvocationId
        $aCase.SetupInvocationId=$idA
        Check ($idA -cmatch '^[0-9a-f]{32}$') 'Shared report A expected invocation nonce was independently captured before B'
        $authority=Full ([string]$mark.authoritativeReportPath);Assert-TestPath $authority $root
        Check ((Hash $authority) -ceq [string]$mark.privateReportSHA256) 'Shared report A private authority SHA matches its paused actual marker'
        $aAuthorityEvidence=Preserve-RawReport $authority 'shared-report-A-authoritative-pre-B' $root
        $aAuthority=Get-Content -LiteralPath $authority -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($aAuthority.status -eq 'INSTALLED' -and $aAuthority.action -eq 'Install' -and $aAuthority.setupInvocationId -ceq $idA -and $aAuthority.transactionId -ceq $mark.transactionId) 'Shared report A private authoritative engine result has its own successful invocation and transaction'
        $aCase.Backup=Full $aAuthority.backup;Assert-TestPath $aCase.Backup $root;$aCase.Uninstaller=Join-Path $aCase.Backup 'JUST-uninstall.exe'
        $aJournal=Get-Content -LiteralPath (Join-Path $aCase.Backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($aJournal.status -ceq 'INSTALLED') 'Shared report A actual pre-Create journal meets the exact INSTALLED gate'
        Check ($aJournal.setupInvocationId -ceq $idA -and $aJournal.transactionId -ceq $mark.transactionId) 'Shared report A paused engine journal agrees with A before B starts'
        $beforeB=[ordered]@{Target=(Tree $target);ATransaction=(Tree $aCase.Backup);Fixtures=(Tree $aCase.Fixtures)}
        Write-NewJson $beforeB (Join-Path $evidence 'snapshot-shared-report-before-B.json')
        $bCase.Original=$beforeB.Target
        $aExternalEvidence=Preserve-RawReport $shared 'shared-report-readable-A-before-B' $root
        $aExternal=Get-Content -LiteralPath $shared -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($aExternal.setupInvocationId -ceq $idA) 'Shared readable report first records A at the controlled gap'
        $bArguments=$common+' /D='+$target
        $bCase.InstallProcess=Invoke-Exe $installer $bArguments $temp 'shared-report-B-install'
        Write-NewJson $bCase.InstallProcess (Join-Path $evidence 'process-shared-report-B-install.json')
        $bExternalEvidence=Preserve-RawReport $shared 'shared-report-readable-B-before-A-release' $root
        $bReport=Get-Content -LiteralPath $shared -Raw -Encoding UTF8 | ConvertFrom-Json;$idB=[string]$bReport.setupInvocationId
        $bCase.SetupInvocationId=$idB
        Check ($bCase.InstallProcess.ExitCode -eq 0 -and $bReport.status -eq 'INSTALLED' -and $bReport.installerCompletion -eq 'READY') 'Shared report B actual Setup completes while A is paused before Create'
        Check ($idB -cmatch '^[0-9a-f]{32}$' -and $idB -cne $idA) 'Shared report B has a distinct actual invocation nonce'
        $bCase.Backup=Full $bReport.backup;Assert-TestPath $bCase.Backup $root;$bCase.Uninstaller=Join-Path $bCase.Backup 'JUST-uninstall.exe'
        Check ($bCase.Backup -ine $aCase.Backup) 'Shared report A and B own distinct unique transaction directories'
        $bJournal=Get-Content -LiteralPath (Join-Path $bCase.Backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($bJournal.status -ceq 'INSTALLED') 'Shared report B completed entry retains the exact successful journal gate'
        $bBinding=Get-Content -LiteralPath (Join-Path $bCase.Backup 'transaction-binding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($bReport.transactionId -ceq $bJournal.transactionId -and $bReport.transactionId -ceq $bBinding.transactionId -and $idB -ceq $bJournal.setupInvocationId -and $idB -ceq $bBinding.setupInvocationId) 'Shared report B binding belongs to B private authority and its own journal'
        Check ($bReport.uninstallerSHA256 -ceq (Hash $bCase.Uninstaller) -and $bReport.bindingSHA256 -ceq (Hash (Join-Path $bCase.Backup 'transaction-binding.json'))) 'Shared report B generated actual uninstaller and context match recorded SHA256'
        $afterB=[ordered]@{Target=(Tree $target);ATransaction=(Tree $aCase.Backup);BTransaction=(Tree $bCase.Backup);Fixtures=(Tree $aCase.Fixtures)}
        Write-NewJson $afterB (Join-Path $evidence 'snapshot-shared-report-after-B-before-A-release.json')
        Check (Same $beforeB.Target $afterB.Target) 'Shared report B same candidate leaves target payload byte/directory identity unchanged'
        Check (Same $beforeB.ATransaction $afterB.ATransaction) 'Shared report B cannot modify A paused transaction or original backup'
        Check (Same $beforeB.Fixtures $afterB.Fixtures) 'Shared report interleaving leaves synthetic unrelated fixtures unchanged'
        Check ((Hash $authority) -ceq [string]$mark.privateReportSHA256) 'Shared report B overwrites only readable report and leaves A private authority exact'
        Write-NewJson ([ordered]@{setupInvocationId=$idA}) $release
        $releaseEvidence=Preserve-RawReport $release 'shared-report-A-release' $root
        if (-not $aProcess.WaitForExit(60000)) { throw 'Owned Setup A did not finish after its bounded release; preserve it and evidence, never terminate it.' }
        $aComplete=$true
        $aCase.InstallProcess=[ordered]@{Name='shared-report-A-install';Executable=$installer;ExecutableSHA256=$installerSHA;ExecutableSHA256After=(Hash $installer);Arguments=$aArguments;TEMP=$temp;ExitCode=$aProcess.ExitCode;ParentProcessId=$aProcess.Id;Utc=[DateTime]::UtcNow.ToString('o');Mode='Actual EXE controlled post-engine/pre-Create gap; no GUI claim'}
        Write-NewJson $aCase.InstallProcess (Join-Path $evidence 'process-shared-report-A-install.json')
        $aFinalEvidence=Preserve-RawReport $shared 'shared-report-readable-A-after-release' $root
        $aFinal=Get-Content -LiteralPath $shared -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($aCase.InstallProcess.ExitCode -eq 0 -and $aFinal.status -eq 'INSTALLED' -and $aFinal.installerCompletion -eq 'READY') 'Shared report A actual Setup completes after B replaced the readable report'
        Check ($aFinal.setupInvocationId -ceq $idA -and $aFinal.transactionId -ceq $mark.transactionId -and (Full $aFinal.backup) -ieq $aCase.Backup) 'Shared report A completion binds its own pre-B invocation and transaction rather than B'
        $aBinding=Get-Content -LiteralPath (Join-Path $aCase.Backup 'transaction-binding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        $aJournalAfter=Get-Content -LiteralPath (Join-Path $aCase.Backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        Check ($aJournalAfter.status -ceq 'INSTALLED') 'Shared report A Create and Finalize retain the exact successful journal gate'
        Check ($aBinding.setupInvocationId -ceq $idA -and $aBinding.transactionId -ceq $mark.transactionId -and $aJournalAfter.setupInvocationId -ceq $idA) 'Shared report A actual generated binding and journal retain A unique stable invocation'
        Check ($aFinal.uninstallerSHA256 -ceq (Hash $aCase.Uninstaller) -and $aFinal.bindingSHA256 -ceq (Hash (Join-Path $aCase.Backup 'transaction-binding.json'))) 'Shared report A generated actual uninstaller and context match recorded SHA256'
        Check ($bBinding.setupInvocationId -cne $aBinding.setupInvocationId -and $bBinding.transactionId -cne $aBinding.transactionId) 'Shared readable report reuse cannot cross-bind the two successful actual Setup invocations'
        $finalState=[ordered]@{Target=(Tree $target);A=(Evidence-State $aCase);B=(Evidence-State $bCase)}
        Write-NewJson $finalState (Join-Path $evidence 'snapshot-shared-report-after-A-finalize.json')
        Save-Case ([ordered]@{Name='shared-report-controlled-A-B-interleaving';Status='PASSED';SetupA=$aCase.InstallProcess;SetupB=$bCase.InstallProcess;ExpectedInvocationA=$idA;ExpectedInvocationB=$idB;TransactionA=$mark.transactionId;TransactionB=$bReport.transactionId;AuthoritativeA=$aAuthorityEvidence;ReadyMarker=$readyEvidence;ReleaseMarker=$releaseEvidence;ReadableA=$aExternalEvidence;ReadableB=$bExternalEvidence;ReadableFinalA=$aFinalEvidence;BeforeB=$beforeB;AfterB=$afterB;AfterFinalization=$finalState;RealTargetUsed=$false;AllSynthetic=$true})
        Success $bCase 'shared-report-B-exe-restore'
        Success $aCase 'shared-report-A-exe-restore'
    } catch {
        Write-NewJson ([ordered]@{Status='FAILED';Error=$_.Exception.Message;Target=(Tree $target);BackupRoot=(Tree $backupRoot);Fixtures=(Tree (Join-Path $root 'fixtures'));SharedReportSHA256=(Optional-Hash $shared);ReadyMarkerSHA256=(Optional-Hash (Join-Path $root 'A-ready.json'));ReleaseMarkerSHA256=(Optional-Hash (Join-Path $root 'A-release.json'));SetupAProcessId=if($null -ne $aProcess){$aProcess.Id}else{0};SetupACompletionCaptured=$aComplete;ProcessesNotTerminated=$true;AllSynthetic=$true;Utc=[DateTime]::UtcNow.ToString('o')}) (Join-Path $evidence 'snapshot-shared-report-first-failure.json')
        throw
    } finally { $env:TEMP=$beforeTemp;$env:TMP=$beforeTmp }
}

function Resume-FixtureOrder {
    $oldSourceSHA='6f5d2b4e9e95eefd3a4c0257be93cbe5491207f16e389d8d8d8041da292f06e6'
    $oldInstallerSHA='18fcb91ddf2535ca97133153375ea6d80011d8dcbb4621f5afce629ba2da0b7e'
    if (-not $PriorEvidenceDir) { throw 'This bounded phase requires its exact prior evidence directory.' }
    $priorDir=(Resolve-Path -LiteralPath $PriorEvidenceDir).Path
    if (-not $priorDir.StartsWith('D:\JUST-work\',[StringComparison]::OrdinalIgnoreCase) -or $priorDir -ieq $evidence) { throw 'Use a different fresh D Work evidence directory.' }
    if (Test-Path -LiteralPath $baselinePath) { throw 'Continuation baseline already exists; no repetition.' }
    $oldSourcePath=Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))) 'Test-Uninstaller-executed-6f5d2b4e.ps1'
    $expectedFiles=[ordered]@{
        'uninstaller-tests.json'='966bbb6d1b70f6160e4e2be070cd6170224401ee46bb46716e275d78ae4a1da6'
        'uninstaller-gui-baseline.json'='f9c66ee759244881917e5200760d45a01ce0564d57226e4d9f9e8fda77dc01bd'
        'snapshot-project.fixture-pre-install.json'='a0ddff57e5e127bb17680d3c1aaf974430c47705a1c2d304229da2550b5a694b'
        'snapshot-project.fixture-post-install.json'='963c41982366437ced69e73c3ac20f9b2e2b436bbad4497ce52443a0cf9fd90f'
        'process-project.fixture-install.json'='6baeada3e490752a2d48e80e051af130dc018839fbfc071d553d2998a7d79558'
    }
    $expectedCases=[ordered]@{
        'actual-gui-normal-cancel'='9f543afec67894cb0c91c5ff8b93449db2061c0b3abeb445211c2c62bc0c42c5'
        'default-all-exe-restore'='76e51bce33a4a6115bc0c2a8d8dea251eb146d0e3498066782629ee73c546c99'
        'default-all-repeat-exe'='ee68f26628515908588217a429dde9b155299d9cb2ca44ad0acd89cacefba440'
        'shared-report-controlled-A-B-interleaving'='5c638aa8abac9b1ddaec4d754efbd1545873515165c4e4a31c94eae5f4c5b8a9'
        'shared-report-B-exe-restore'='f439820557d3c0107252284076dae9ab1165c4c49e2a9bfae4aadd4349962ff9'
        'shared-report-A-exe-restore'='955aaef87c2fe62302767fbc01bc9d044a4967dfb0df5994a7c7fe86085be5f1'
        'long-supported-structured-install'='ffe2360097311f51306de3da3425ab972f52845494584189c50bab2adb224ad4'
        'long-supported-actual-exe-restore'='fee828bc5e4cb7b7a091786756cd40241b9259829186160f04f9f9607b137057'
    }
    $actualFiles=[ordered]@{}
    foreach ($fileName in $expectedFiles.Keys) { $actualFiles[$fileName]=Optional-Hash (Join-Path $priorDir $fileName) }
    Write-NewJson ([ordered]@{Phase=$Phase;PriorEvidenceDir=$priorDir;ExpectedSourceSHA256=$oldSourceSHA;PreservedSource=$oldSourcePath;PreservedSourceSHA256=(Optional-Hash $oldSourcePath);ExpectedInstallerSHA256=$oldInstallerSHA;ActualInstallerSHA256=$installerSHA;ExpectedFiles=$expectedFiles;ActualFiles=$actualFiles;NoEXELaunched=$true}) (Join-Path $evidence 'continuation-identity-preflight.json')
    if ($installerSHA -cne $oldInstallerSHA -or (Hash $oldSourcePath) -cne $oldSourceSHA) { throw 'Bounded continuation requires the exact tested Setup and preserved executed source.' }
    foreach ($fileName in $expectedFiles.Keys) {
        if ($actualFiles[$fileName] -cne $expectedFiles[$fileName]) { throw "Prior byte evidence identity changed: $fileName" }
        [IO.File]::Copy((Join-Path $priorDir $fileName),(Join-Path $evidence ('prior-'+$fileName)),$false)
    }
    [IO.File]::Copy((Join-Path $priorDir 'uninstaller-gui-baseline.json'),$baselinePath,$false)
    $prior=Get-Content -LiteralPath (Join-Path $priorDir 'uninstaller-tests.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $base=Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($prior.Status -cne 'FAILED' -or $prior.FirstFailureCase -cne 'eq-only-upgrade' -or $prior.Failure -cne 'FAIL: project.fixture installation preserves every unselected and unrelated item' -or $prior.TestScriptSHA256 -cne $oldSourceSHA -or $prior.InstallerSHA256 -cne $oldInstallerSHA -or $prior.CaseCount -ne 8 -or @($prior.Cases).Count -ne 8 -or $prior.CheckCount -ne 104 -or @($prior.Checks).Count -ne 104 -or $base.TestScriptSHA256 -cne $oldSourceSHA -or $base.InstallerSHA256 -cne $oldInstallerSHA) { throw 'This phase is limited to the exact observed fixture-order first failure.' }
    $script:TestRoot=Full $prior.IsolatedDirectory
    if ((Full $prior.WorkerTEMP) -ine $workerTemp -or (Full $base.TestRoot) -ine $script:TestRoot -or -not (Inside $script:TestRoot $workerTemp) -or (Split-Path -Leaf $script:TestRoot) -cnotmatch '^JUST-uninstaller-tests-[0-9a-f]{32}$') { throw 'Prior isolated root/worker TEMP mismatch.' }
    $caseIndex=0
    foreach ($caseName in $expectedCases.Keys) {
        $pointer=$prior.Cases[$caseIndex]
        $sourceCase=Join-Path $priorDir ('case-'+$caseName+'.json')
        if ($pointer.Name -cne $caseName -or $pointer.Status -cne 'PASSED' -or (Full $pointer.Evidence) -ine $sourceCase -or $pointer.EvidenceSHA256 -cne $expectedCases[$caseName] -or (Hash $sourceCase) -cne $expectedCases[$caseName]) { throw "Prior completed case identity mismatch: $caseName" }
        $record=Get-Content -LiteralPath $sourceCase -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($record.Name -cne $caseName -or $record.Status -cne 'PASSED' -or [bool]$record.RealTargetUsed) { throw 'Prior case is not the exact passed synthetic case.' }
        $imported=Join-Path $evidence ('case-'+$caseName+'.json')
        [IO.File]::Copy($sourceCase,$imported,$false)
        $script:Cases.Add([ordered]@{Name=$caseName;Status='PASSED';Evidence=$imported;EvidenceSHA256=(Hash $imported);ImportedOriginalEvidence=$sourceCase;ImportedOriginalSHA256=$expectedCases[$caseName];ImportedWithoutRerun=$true})
        $caseIndex++
    }
    if ($prior.Checks[91] -cne 'long-supported-actual-exe-restore retained journal records restored' -or $prior.Checks[92] -cne 'project.fixture independent fixture supports full replacement-stage and atomic-journal paths') { throw 'Prior completed/partial check boundary differs.' }
    foreach ($description in @($prior.Checks)[0..91]) { $script:Checks.Add([string]$description) }
    $script:Continuation=[ordered]@{Phase=$Phase;PriorEvidenceDir=$priorDir;PriorReportSHA256=$expectedFiles['uninstaller-tests.json'];PriorTestSourceSHA256=$oldSourceSHA;CurrentTestSourceSHA256=(Hash $PSCommandPath);SourceVersionDifferenceExplicitlyAuthorized=$true;ImportedCaseCount=8;ImportedCompletedCheckCount=92;PriorCheckCount=104;ExcludedPartialChecks=@($prior.Checks)[92..103];Prior104Checks=@($prior.Checks);RerunCompletedCases=$false;ReinstalledExistingEQ=$false;Canonicalization='Only filesystem snapshot directory/reparse sets and Name+SHA256 file maps; journal arrays unchanged'}
    Write-NewJson $script:Continuation (Join-Path $evidence 'continuation-prior-checks.json')
    $pre=Get-Content -LiteralPath (Join-Path $priorDir 'snapshot-project.fixture-pre-install.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $post=Get-Content -LiteralPath (Join-Path $priorDir 'snapshot-project.fixture-post-install.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $process=Get-Content -LiteralPath (Join-Path $priorDir 'process-project.fixture-install.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $target=Full $pre.Target;$caseRoot=Split-Path -Parent $target
    Assert-TestPath $target $script:TestRoot
    if ((Split-Path -Leaf $caseRoot) -cnotmatch '^c-[0-9a-f]{12}$' -or (Split-Path -Parent $caseRoot) -ine $script:TestRoot -or (Split-Path -Leaf $target) -cne 'VST3' -or (@($pre.Selected) -join ',') -cne 'eq') { throw 'Existing failed fixture is not the exact independent EQ-only case.' }
    $reports=Join-Path $caseRoot 'reports';$temp=Join-Path $caseRoot 'temp';$installReport=Join-Path $reports 'install.json'
    $installed=Get-Content -LiteralPath $installReport -Raw -Encoding UTF8 | ConvertFrom-Json
    $backup=Full $installed.backup
    Assert-TestPath $backup (Join-Path $caseRoot 'backups')
    $case=[pscustomobject]@{Name='eq-only-upgrade';Root=$caseRoot;Target=$target;BackupRoot=(Join-Path $caseRoot 'backups');Backup=$backup;Uninstaller=(Join-Path $backup 'JUST-uninstall.exe');Temp=$temp;Reports=$reports;EarlyErrorDirectory=$reports;SetupInvocationId=[string]$installed.setupInvocationId;Fixtures=(Join-Path $caseRoot 'fixtures');Selected=@('eq');Original=$pre.FullState;InstallProcess=$process}
    $current=[ordered]@{Target=(Tree $target);BackupRoot=(Tree $case.BackupRoot);Fixtures=(Tree $case.Fixtures);InstallReportSHA256=(Hash $installReport);Process=$process}
    Write-NewJson $current (Join-Path $evidence 'snapshot-reused-eq-before-any-undo.json')
    Check ($process.ExitCode -eq 0 -and $process.ParentProcessId -gt 0 -and $process.ExecutableSHA256 -ceq $oldInstallerSHA -and $process.ExecutableSHA256After -ceq $oldInstallerSHA -and (Full $process.TEMP) -ieq $temp) 'Existing EQ install has the exact previously observed native Setup identity/exit'
    Check ($installed.status -ceq 'INSTALLED' -and $installed.installerCompletion -ceq 'READY' -and (@($installed.selected) -join ',') -ceq 'eq' -and $installed.candidateManifestSHA256 -ceq (Hash (Join-Path $candidate 'candidate-manifest.json'))) 'Existing EQ successful install report is still the exact candidate and ordered selection'
    Check (Same-RootState $current.Target $post.Target) 'Existing EQ target exactly matches prior post-install files/directories/types before undo'
    Check (Same-RootState $current.BackupRoot $post.BackupRoot) 'Existing EQ complete transaction backup bytes/directories exactly match prior post-install'
    Check ((Same-RootState $current.Fixtures $post.Fixtures) -and (Same-RootState $pre.Fixtures $post.Fixtures)) 'Existing EQ synthetic fixtures remain exact before any undo'
    Check (Same-RootState (NonSelected $current.Target @('eq')) (NonSelected $pre.FullState @('eq'))) 'Existing EQ install preserves every unselected and unrelated item by canonical full state'
    Check ($installed.uninstallerSHA256 -ceq (Hash $case.Uninstaller) -and $installed.bindingSHA256 -ceq (Hash (Join-Path $backup 'transaction-binding.json'))) 'Existing EQ exact generated entry and context agree with completed install report'
    $journal=Get-Content -LiteralPath (Join-Path $backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $binding=Get-Content -LiteralPath (Join-Path $backup 'transaction-binding.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($journal.status -ceq 'INSTALLED' -and $journal.destination -ceq $target -and (@($journal.selected) -join ',') -ceq 'eq' -and $journal.setupInvocationId -ceq $installed.setupInvocationId -and $binding.setupInvocationId -ceq $installed.setupInvocationId) 'Existing EQ installed journal/context still belong to this exact original Setup invocation'
    return $case
}
function Assert-FortyActualCases {
    $expected=@('actual-gui-normal-cancel','default-all-exe-restore','default-all-repeat-exe','shared-report-controlled-A-B-interleaving','shared-report-B-exe-restore','shared-report-A-exe-restore','long-supported-structured-install','long-supported-actual-exe-restore','eq-only-upgrade','mixed-two-upgrade','refuse-byte-modification','refuse-new-file','refuse-unknown-empty-directory','refuse-empty-bundle-root','refuse-root-type-change','refuse-unknown-reparse','refuse-old-backup-missing','refuse-old-backup-corrupt','refuse-binding-missing','refuse-binding-other-transaction','refuse-journal-destination-change','refuse-moved-exe','refuse-foreign-original-directory','refuse-foreign-destination','refuse-moved-exe-original-directory','saved-engine-poison','missing-fresh-target','missing-old-target','known-partial-installed-target','post-restore-initial-exe','post-restore-new-user-contents','post-restored-same-candidate-reinstalled-initial-restore','post-restored-same-candidate-reinstalled','post-restored-same-name-empty-root-initial-restore','post-restored-same-name-empty-root','post-restored-old-known-partial-initial-restore','post-restored-old-known-partial','unsafe-report-missing-journal','unsafe-report-corrupt-journal','delete-boundary-unknown-empty')
    Check ($script:Cases.Count -eq 40 -and (@($script:Cases.ToArray() | ForEach-Object { $_.Name } | Sort-Object) -join ',') -ceq (@($expected | Sort-Object) -join ',')) 'All forty distinct required actual cases have evidence; no duplicates or omitted cases'
    foreach ($pointer in $script:Cases.ToArray()) {
        $record=Get-Content -LiteralPath $pointer.Evidence -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($pointer.Status -cne 'PASSED' -or $record.Status -cne 'PASSED' -or $pointer.Name -cne $record.Name -or $pointer.EvidenceSHA256 -cne (Hash $pointer.Evidence)) { throw 'Final case byte identity/status changed.' }
        if ($record.PSObject.Properties['ActualEXE']) {
            $native=if ($record.ActualEXE.PSObject.Properties['Process']) { $record.ActualEXE.Process } else { $record.ActualEXE }
            if ($native.ParentProcessId -le 0 -or ($native.ShadowCaptured -and $native.ShadowProcessId -le 0)) { throw 'Actual EXE case lacks an observed native process identity.' }
        } elseif ($record.PSObject.Properties['ActualSetup']) {
            if ($record.ActualSetup.ParentProcessId -le 0) { throw 'Actual Setup case lacks its native process identity.' }
        } elseif ($record.PSObject.Properties['SetupA']) {
            if ($record.SetupA.ParentProcessId -le 0 -or $record.SetupB.ParentProcessId -le 0) { throw 'Actual A/B case lacks both native process identities.' }
        } elseif ($record.PSObject.Properties['GuiEvidence']) {
            if ((Hash $record.GuiEvidence) -cne $record.GuiEvidenceSHA256) { throw 'Actual GUI evidence changed.' }
            $gui=Get-Content -LiteralPath $record.GuiEvidence -Raw -Encoding UTF8 | ConvertFrom-Json
            if (-not $gui.normalCancel -or $gui.ownedPid -le 0 -or $gui.processExitCode -ne 1) { throw 'Actual GUI case lacks observed native cancellation.' }
        } else { throw 'Case lacks actual native process evidence.' }
    }
    Check ($Phase -notin @('ContinueAfterFixtureOrder','VerifyCompletedEvidence') -or ($script:Continuation.ImportedCaseCount -eq 8 -and $script:Cases.Count-8 -eq 32)) 'Bounded continuation includes exactly eight preserved prior cases and thirty-two remaining actual cases'
}

function Copy-CompletedEvidenceBytes([string]$Source,[string]$Destination,[string]$ExpectedSHA256) {
    # One byte snapshot is both hashed and copied. No case JSON, report, or path
    # stored inside the evidence is rewritten by the final read-only audit.
    if (Test-Path -LiteralPath $Destination) { throw 'Audit byte-copy destination already exists.' }
    $bytes=[IO.File]::ReadAllBytes($Source)
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $actual=([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    if ($actual -cne $ExpectedSHA256) { throw ('Completed evidence byte identity changed: '+(Split-Path -Leaf $Source)) }
    [IO.File]::WriteAllBytes($Destination,$bytes)
    if ((Hash $Destination) -cne $ExpectedSHA256) { throw 'Audit copy differs from its original byte snapshot.' }
    return [ordered]@{OriginalPath=$Source;OriginalSHA256=$actual;Path=$Destination;SHA256=$actual;CopiedByteExactly=$true}
}

function Verify-CompletedEvidence {
    # This entry has no fixture/Setup/uninstaller calls. Native work remains
    # attributed to its preserved 6f (eight) and 22f (thirty-two) test sources.
    $priorSHA='70629c47c7be7f6fec2605f2d303c8a443deebecbc725a3e52c0b4bd86581769'
    $nativeSHA='22f9215541dddc97d6677e3bf4ce014c133177be97ff3d15ae2cd69b0161a773'
    $legacySHA='6f5d2b4e9e95eefd3a4c0257be93cbe5491207f16e389d8d8d8041da292f06e6'
    $setupSHA='18fcb91ddf2535ca97133153375ea6d80011d8dcbb4621f5afce629ba2da0b7e'
    $runRoot=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
    $nativeArchive=Join-Path $runRoot 'Test-Uninstaller-executed-22f92155.ps1'
    $legacyArchive=Join-Path $runRoot 'Test-Uninstaller-executed-6f5d2b4e.ps1'
    $priorDir=(Resolve-Path -LiteralPath $PriorEvidenceDir).Path
    $priorPath=Join-Path $priorDir 'uninstaller-tests.json'
    $copiedPrior=Join-Path $evidence 'prior-failed-uninstaller-tests-v3.json'
    $audit=[ordered]@{ReadOnly=$true;PriorReport=$priorPath;PriorReportSHA256=$priorSHA;CopiedPriorReport=$copiedPrior;CopiedPriorReportSHA256=$null;PriorStatus='FAILED';PriorCaseCount=40;PriorCheckCount=705;AuditScript=$PSCommandPath;AuditSourceSHA256=(Hash $PSCommandPath);NativePriorSourceSHA256=$legacySHA;NativeContinuationSourceSHA256=$nativeSHA;PriorEightNativeCases=8;ContinuationNativeCases=32;ReexecutedCases=0;EXEsExecuted=$false;ExistingCaseFilesCopiedByteExactly=0;NewReadOnlyChecks=0;OriginalFailureCharacters=121745;OriginalFailureCase=$null;OriginalFailureReason='Final native PowerShell summary used default ANSI decoding for UTF-8-no-BOM JSON; explicit UTF-8 evidence reads are required.';CopiedArtifacts=@()}
    $prior=$null;$auditStatus='FAILED';$auditFailure='';$script:TestRoot=''
    try {
        if ((Full $priorDir) -ine (Full (Join-Path $runRoot 'uninstaller-tests-after-fixture-order-fix')) -or $installerSHA -cne $setupSHA -or (Hash $nativeArchive) -cne $nativeSHA -or (Hash $legacyArchive) -cne $legacySHA) { throw 'Read-only audit requires the exact completed evidence directory, tested Setup, and preserved native sources.' }
        $copied=Copy-CompletedEvidenceBytes $priorPath $copiedPrior $priorSHA
        $audit.CopiedPriorReportSHA256=$copied.SHA256
        $prior=Get-Content -LiteralPath $copiedPrior -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($prior.Schema -cne 'JUST.resume25.actual-uninstaller-tests.v3' -or $prior.Status -cne 'FAILED' -or $prior.Phase -cne 'ContinueAfterFixtureOrder' -or $prior.TestScriptSHA256 -cne $nativeSHA -or $prior.InstallerSHA256 -cne $setupSHA -or $prior.CaseCount -ne 40 -or @($prior.Cases).Count -ne 40 -or $prior.CheckCount -ne 705 -or @($prior.Checks).Count -ne 705 -or ([string]$prior.Failure).Length -ne 121745) { throw 'Only the exact completed 40-case/705-check UTF-8 summary failure may be audited.' }
        $audit.OriginalFailureCase=[string]$prior.FirstFailureCase
        $continuation=$prior.Continuation
        if ($continuation.Phase -cne 'ContinueAfterFixtureOrder' -or $continuation.PriorTestSourceSHA256 -cne $legacySHA -or $continuation.CurrentTestSourceSHA256 -cne $nativeSHA -or $continuation.ImportedCaseCount -ne 8 -or $continuation.ImportedCompletedCheckCount -ne 92 -or $continuation.PriorCheckCount -ne 104 -or [bool]$continuation.RerunCompletedCases -or [bool]$continuation.ReinstalledExistingEQ) { throw 'Native eight/32 source attribution or bounded continuation identity differs.' }
        $artifactSHAs=[ordered]@{
            'uninstaller-gui-baseline.json'='f9c66ee759244881917e5200760d45a01ce0564d57226e4d9f9e8fda77dc01bd'
            'continuation-identity-preflight.json'='0c7c6cd50e81766d63ebee8d7c7b9d813f3afeda0c9d933d151c5a4460138dbc'
            'continuation-prior-checks.json'='01fef7daaefd17859b92644734b8ea64e9ec3204b9d4de1b0f766627572fefd5'
        }
        foreach ($artifactName in $artifactSHAs.Keys) { $audit.CopiedArtifacts+=Copy-CompletedEvidenceBytes (Join-Path $priorDir $artifactName) (Join-Path $evidence $artifactName) $artifactSHAs[$artifactName] }
        $expectedCases=[ordered]@{
            'actual-gui-normal-cancel'='9f543afec67894cb0c91c5ff8b93449db2061c0b3abeb445211c2c62bc0c42c5'
            'default-all-exe-restore'='76e51bce33a4a6115bc0c2a8d8dea251eb146d0e3498066782629ee73c546c99'
            'default-all-repeat-exe'='ee68f26628515908588217a429dde9b155299d9cb2ca44ad0acd89cacefba440'
            'shared-report-controlled-A-B-interleaving'='5c638aa8abac9b1ddaec4d754efbd1545873515165c4e4a31c94eae5f4c5b8a9'
            'shared-report-B-exe-restore'='f439820557d3c0107252284076dae9ab1165c4c49e2a9bfae4aadd4349962ff9'
            'shared-report-A-exe-restore'='955aaef87c2fe62302767fbc01bc9d044a4967dfb0df5994a7c7fe86085be5f1'
            'long-supported-structured-install'='ffe2360097311f51306de3da3425ab972f52845494584189c50bab2adb224ad4'
            'long-supported-actual-exe-restore'='fee828bc5e4cb7b7a091786756cd40241b9259829186160f04f9f9607b137057'
            'eq-only-upgrade'='ff6a50d479c19dbd079213f88aab2a88ba02199956adea32a45f074a9423ae2f'
            'mixed-two-upgrade'='ade38a0e2402f72901f719bbed24f261ee235889755fad124f7974c2da73e858'
            'refuse-byte-modification'='8801d9a62674565076b0271496fbb492406cd44cd3822754a9cbaa34567be0cd'
            'refuse-new-file'='12b9ef3e592cd2d9a10069ccfae1b3bad2742adac960725590fb055dd295e881'
            'refuse-unknown-empty-directory'='a00d9c31362c8301dad2f4a0d6643255141c35e23c060a9ec82c9b329a2ad123'
            'refuse-empty-bundle-root'='e3c4b7db116818720d1aa78842a3689e58d2e159cc5d6054069e29e070661942'
            'refuse-root-type-change'='56273a8f878b19584d5963511d42999a0d133fb077038e22eb705ad47dc8b48f'
            'refuse-unknown-reparse'='7bd13860c8c2d0449929a7360151d930785ef40df6f7cc06d8f4c9f4b0b3b109'
            'refuse-old-backup-missing'='cadf415aab3d8d11864aa60a1393a87ec00bd5bc50f44114f6149e16280708eb'
            'refuse-old-backup-corrupt'='4fa8be399d3909362904bf8aff76f4cb0aa7488d248314be2f651a25b4750d81'
            'refuse-binding-missing'='ddef998282f8b36fee1c7a75eaede6b0e98fe72e0181bd957526703b7b274f91'
            'refuse-binding-other-transaction'='de7318a2bb64615f987522b747ae93afca103a352b67e0cf675c2b5cedb0ab02'
            'refuse-journal-destination-change'='a7461edae590f02fe23da0a4a0c79d26f73c21701a609087392ad2a0095ef25f'
            'refuse-moved-exe'='9d98739d3e6f75f505fb5a758c6dad50684021d03e665b6d035a995f834fe81c'
            'refuse-foreign-original-directory'='bc109cb2b0a1cf1028fabf4e1222ab07d2e2726900b98ce6c11385e16123a0d2'
            'refuse-foreign-destination'='d57beb5a548071d6c2875e8199c9dbd4075333611702365b5cdd852719b18ab9'
            'refuse-moved-exe-original-directory'='a0e193a57f92f30291a1dd99f6c75f64847370c9c15c3e85f9aca4ce240ccfc2'
            'saved-engine-poison'='592c78cf0cdaa9f01e2bd029e77f6f42092c1b6ecee4bcf047e36726a7c8eab2'
            'missing-fresh-target'='25d0490023bfe45c3887f41cc1751f17db128850b01341e07e47fd537a67909f'
            'missing-old-target'='00f2e397310c86372d503172a123709deb9d1a97f026c2dd4ec786a067020f4f'
            'known-partial-installed-target'='a2ef2cdf71be979900ff3f605c10bfb14b04f70b2b392a3f080abc7a2dbbb841'
            'post-restore-initial-exe'='2d3f58d0dd823733e19b8be9cd157e12d844bcdc470c75ff8077d75bfad3997f'
            'post-restore-new-user-contents'='9d7ff0a7c74d90e655a7ef5f0aa162d190796688c0e752f9e904cb834a5548e8'
            'post-restored-same-candidate-reinstalled-initial-restore'='ebd7f9e44adf7c3f0652089c8bac1c058d29f9d0ebd6fc4e938c6b96daf377af'
            'post-restored-same-candidate-reinstalled'='fa8330449cb8be3ac3666215e61fae1c41ab465ae1a1d6555e879aa6cf9c4f32'
            'post-restored-same-name-empty-root-initial-restore'='4f499609b5cc3dbb7bf871be7f42a1ff88649568240a440ba9c3b4dfe2dadd71'
            'post-restored-same-name-empty-root'='430cb5061744a86de48e324f2b7df8a69101333678fac60b548f42a4a3591c71'
            'post-restored-old-known-partial-initial-restore'='21d8fc6257d74064178b5dd98caa34cd5ee871f22fde6e0077f295cdace839a7'
            'post-restored-old-known-partial'='8cf16a02c5d77ee4a5c1cabd7ca6d711b59fe06da0861c1c6365ef89c5f93b67'
            'unsafe-report-missing-journal'='7c63d0ca1ce323d73502f1e57a932360ea4ffa8176d93f8337da51c6f80e3d65'
            'unsafe-report-corrupt-journal'='eee8f75173cd029a15e75fcdf7b73ed402acdc586cbac334dea971fd0335b77e'
            'delete-boundary-unknown-empty'='fd277ff6e07203e7aabba0d43abb955b9092879cbb8d308a22630bb4914ccd3f'
        }
        $caseIndex=0
        foreach ($caseName in $expectedCases.Keys) {
            $pointer=$prior.Cases[$caseIndex]
            $sourceCase=Join-Path $priorDir ('case-'+$caseName+'.json')
            if ($pointer.Name -cne $caseName -or $pointer.Status -cne 'PASSED' -or (Full $pointer.Evidence) -ine (Full $sourceCase) -or $pointer.EvidenceSHA256 -cne $expectedCases[$caseName]) { throw ('Completed native case pointer changed: '+$caseName) }
            $imported=Join-Path $evidence ('case-'+$caseName+'.json')
            $rawCase=Copy-CompletedEvidenceBytes $sourceCase $imported $expectedCases[$caseName]
            $script:Cases.Add([ordered]@{Name=$caseName;Status='PASSED';Evidence=$imported;EvidenceSHA256=$rawCase.SHA256;OriginalCaseEvidence=$sourceCase;OriginalCaseEvidenceSHA256=$rawCase.OriginalSHA256;NativeExecutionSourceSHA256=if($caseIndex -lt 8){$legacySHA}else{$nativeSHA};ImportedWithoutRerun=$true})
            $caseIndex++
        }
        $audit.ExistingCaseFilesCopiedByteExactly=$caseIndex
        foreach ($description in @($prior.Checks)) { $script:Checks.Add([string]$description) }
        $script:Continuation=$prior.Continuation
        $script:TestRoot=[string]$prior.IsolatedDirectory
        Assert-FortyActualCases
        if ($script:Checks.Count -ne 707 -or $script:Cases.Count -ne 40) { throw 'Read-only audit must add exactly two final evidence gates to the preserved 705 checks.' }
        $audit.NewReadOnlyChecks=2
        $audit.NewReadOnlyCheckIndices=@(705,706)
        $auditStatus='PASSED'
    } catch {
        $auditFailure=[string]$_.Exception.Message
        if ($auditFailure.Length -gt 2000) { $auditFailure=$auditFailure.Substring(0,2000)+' [truncated; prior raw evidence retained]' }
    }
    $values=[ordered]@{}
    if ($null -ne $prior) { foreach ($property in $prior.PSObject.Properties) { $values[$property.Name]=$property.Value } }
    $values.Schema='JUST.resume25.actual-uninstaller-tests.v4'
    $values.Status=$auditStatus;$values.Failure=$auditFailure;$values.FirstFailureCase=if($auditStatus -eq 'FAILED'){'completed-evidence-audit'}else{''}
    $values.Phase=$Phase;$values.Continuation=$script:Continuation
    $values.ActualInstaller=$installer;$values.InstallerSHA256=$installerSHA
    $values.OriginalExecutionTestScriptPath=if($null -ne $prior){[string]$prior.TestScript}else{''}
    $values.TestScript=$nativeArchive;$values.ExecutedTestSourceArchive=$nativeArchive;$values.TestScriptSHA256=$nativeSHA
    $values.CompletedEvidenceVerification=$audit
    $values.Checks=$script:Checks.ToArray();$values.CheckCount=$script:Checks.Count
    $values.Cases=$script:Cases.ToArray();$values.CaseCount=$script:Cases.Count
    $values.RealVST3Used=$false;$values.RealReaper='NOT_RUN';$values.PrivateUserDataRead=$false;$values.AllFixturesSynthetic=$true
    $values.Utc=[DateTime]::UtcNow.ToString('o')
    Write-NewJson $values $finalPath
    if ($auditStatus -ne 'PASSED') { throw ('Read-only completed evidence audit failed; bounded details: '+$auditFailure) }
}

if ($Phase -eq 'VerifyCompletedEvidence') {
    Verify-CompletedEvidence
    return
}

if ($Phase -eq 'PrepareGui') {
    if (Test-Path -LiteralPath $baselinePath) { throw 'GUI baseline exists; preserve prior run.' }
    if (-not $PreparedAllReportPath) { throw 'Use exact new Test-Installer default-all.json; no redundant first-all installation.' }
    $prepared=(Resolve-Path -LiteralPath $PreparedAllReportPath).Path
    $all=Get-Content -LiteralPath $prepared -Raw -Encoding UTF8 | ConvertFrom-Json
    $installTestsPath=Join-Path (Split-Path -Parent $prepared) 'installer-tests.json'
    $installTests=Get-Content -LiteralPath $installTestsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($installTests.status -eq 'PASSED' -and $installTests.installerSHA256 -eq $installerSHA) 'Prepared transaction came from this new actual EXE tested by original installer cases'
    Check ($all.status -eq 'INSTALLED' -and (@($all.selected) -join ',') -ceq ($slugs -join ',')) 'Reuse actual newly installed default-all transaction'
    Check ($all.installerCompletion -eq 'READY') 'Prepared default-all transaction completed the actual bound uninstaller entry'
    Check ($all.setupInvocationId -cmatch '^[0-9a-f]{32}$') 'Prepared default-all transaction records its actual unique Setup invocation'
    $backup=Full $all.backup
    if (-not (Inside $backup $workerTemp)) { throw 'Prepared transaction outside this worker D TEMP.' }
    $journal=Get-Content -LiteralPath (Join-Path $backup 'installer-transaction.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $target=Full $journal.destination
    if (-not (Inside $target $workerTemp)) { throw 'Prepared target outside this worker D TEMP.' }
    $testRoot=Join-Path $workerTemp ('JUST-uninstaller-tests-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $guiCase=[pscustomobject]@{Name='reused-default-all';Root=(Split-Path -Parent (Split-Path -Parent $backup));Target=$target;Backup=$backup;Uninstaller=(Join-Path $backup 'JUST-uninstall.exe');Temp=(Join-Path $testRoot 'gui-temp');Reports=(Join-Path $testRoot 'gui-reports');EarlyErrorDirectory=(Split-Path -Parent $prepared);SetupInvocationId=[string]$all.setupInvocationId;Fixtures=(Join-Path $testRoot 'gui-fixtures');Selected=$slugs;Original=$null;InstallProcess=$null}
    foreach ($dir in @($guiCase.Temp,$guiCase.Reports,$guiCase.Fixtures)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    Check (Test-Path -LiteralPath $guiCase.Uninstaller -PathType Leaf) 'Prepared actual generated uninstaller exists'
    $state=Evidence-State $guiCase
    $guiCase.Original=NonSelected $state.Target $slugs
    Write-NewJson ([ordered]@{Schema='JUST.resume25.uninstaller-gui-baseline.v1';Utc=[DateTime]::UtcNow.ToString('o');Installer=$installer;InstallerSHA256=$installerSHA;TestScriptSHA256=(Hash $PSCommandPath);OriginalInstallerTestScriptSHA256=(Hash (Join-Path $PSScriptRoot 'Test-Installer.ps1'));OriginalInstallerChecks=$installTests.checkCount;OriginalInstallerEvidence=$installTestsPath;PreparedAllReport=$prepared;PreparedAllReportSHA256=(Hash $prepared);TestRoot=$testRoot;Case=$guiCase;Before=$state;Checks=$script:Checks.ToArray();GUIRequired='Root displays this actual uninstaller confirmation and normally cancels. No /S or restore before Complete.'}) $baselinePath
    Write-Output ('GUI preparation complete; actual uninstaller: '+$guiCase.Uninstaller)
    Write-Output ('Do not restore before recorded GUI cancellation; baseline: '+$baselinePath)
    return
}

if (Test-Path -LiteralPath $finalPath) { throw 'Final test evidence already exists; no blind rerun.' }
$status='FAILED';$failure='';$activeCase='GUI-cancellation';$script:TestRoot=''
try {
    if ($Phase -eq 'ContinueAfterFixtureOrder') {
        $activeCase='eq-only-upgrade'
        $eq=Resume-FixtureOrder
    } else {
    $base=Get-Content -LiteralPath $baselinePath -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ($base.InstallerSHA256 -eq $installerSHA) 'Completion uses the prepared new Setup identity'
    Check ($base.TestScriptSHA256 -eq (Hash $PSCommandPath)) 'Completion test script unchanged since GUI baseline'
    $script:TestRoot=Full $base.TestRoot
    Check (Inside $script:TestRoot $workerTemp) 'All new cases remain inside the original D worker TEMP'
    $allCase=$base.Case
    $afterCancel=Evidence-State $allCase
    Write-NewJson $afterCancel (Join-Path $evidence 'snapshot-actual-gui-normal-cancel-after.json')
    Check (Same $base.Before.Target $afterCancel.Target) 'Actual GUI cancellation leaves all10 target trees exactly unchanged'
    Check (Same $base.Before.EntireTransaction $afterCancel.EntireTransaction) 'Actual GUI cancellation leaves journal/binding/uninstaller/old backups unchanged'
    if (-not $GuiCancelEvidencePath) { throw 'Exact root-performed GUI cancellation evidence required.' }
    $gui=Get-Content -LiteralPath $GuiCancelEvidencePath -Raw -Encoding UTF8 | ConvertFrom-Json
    Check ([bool]$gui.normalCancel -and $gui.processExitCode -eq 1) 'Actual GUI confirmation normally cancelled with observed NSIS cancellation code'
    $guiSHA=if ($gui.PSObject.Properties['uninstallerSHA256']) { [string]$gui.uninstallerSHA256 } elseif ($gui.PSObject.Properties['actualEXESHA256']) { [string]$gui.actualEXESHA256 } else { '' }
    Check ($guiSHA -eq $base.Before.UninstallerSHA256) 'Cancelled GUI belongs to the prepared actual generated uninstaller'
    Save-Case ([ordered]@{Name='actual-gui-normal-cancel';Status='PASSED';GuiEvidence=$GuiCancelEvidencePath;GuiEvidenceSHA256=(Hash $GuiCancelEvidencePath);Uninstaller=$allCase.Uninstaller;UninstallerSHA256=$guiSHA;Before=$base.Before;After=$afterCancel;RestoreCalled=$false;RealTargetUsed=$false})
    $activeCase='default-all-exe-restore';Success $allCase $activeCase
    $activeCase='default-all-repeat-exe';Success $allCase $activeCase $true
    $activeCase='shared-report-controlled-A-B-interleaving';Shared-Report-Binding
    $activeCase='long-supported-structured-install';Long-Supported-Paths

    $activeCase='eq-only-upgrade';$eq=New-Case $activeCase @('eq') $true
    }
    $activeCase='eq-only-upgrade';Success $eq $activeCase
    $trustedOtherBinding=Join-Path $eq.Backup 'transaction-binding.json'
    $activeCase='mixed-two-upgrade';$mixed=New-Case $activeCase @('eq','reverb') $true;Success $mixed $activeCase

    foreach ($mutation in @('byte-modification','new-file','unknown-empty-directory','empty-bundle-root','root-type-change','unknown-reparse')) {
        $activeCase='refuse-'+$mutation;$case=New-Case $activeCase @('eq','reverb') $true
        $second=Join-Path $case.Target 'Just_reverb.vst3'
        switch ($mutation) {
            'byte-modification' { [IO.File]::AppendAllText((Join-Path $second 'Contents\x86_64-win\Just_reverb.vst3'),'SYNTHETIC-UNKNOWN-BYTE-CHANGE') }
            'new-file' { New-Sentinel (Join-Path $second 'new-user-file.bin') 'SYNTHETIC-UNKNOWN-FILE' }
            'unknown-empty-directory' { New-Item -ItemType Directory -Path (Join-Path $second 'new-user-empty-directory') | Out-Null }
            'empty-bundle-root' { Remove-Synthetic $second $case.Root;New-Item -ItemType Directory -Path $second | Out-Null }
            'root-type-change' { Remove-Synthetic $second $case.Root;New-Sentinel $second 'SYNTHETIC-DIRECTORY-REPLACED-BY-FILE' }
            'unknown-reparse' { $linkTarget=Join-Path $case.Root 'junction-target';New-Item -ItemType Directory -Path $linkTarget | Out-Null;New-Item -ItemType Junction -Path (Join-Path $second 'new-user-junction') -Target $linkTarget | Out-Null }
        }
        Refusal $case $activeCase
    }
    foreach ($mutation in @('old-backup-missing','old-backup-corrupt','binding-missing','binding-other-transaction','journal-destination-change','moved-exe')) {
        $activeCase='refuse-'+$mutation;$case=New-Case $activeCase @('eq','reverb') $true;$exe=''
        switch ($mutation) {
            'old-backup-missing' { Remove-Synthetic (Join-Path $case.Backup 'previous\Just_reverb.vst3') $case.Root }
            'old-backup-corrupt' { [IO.File]::AppendAllText((Join-Path $case.Backup 'previous\Just_reverb.vst3\old-version-sentinel.bin'),'SYNTHETIC-CORRUPT-OLD-BACKUP') }
            'binding-missing' { Move-Item -LiteralPath (Join-Path $case.Backup 'transaction-binding.json') -Destination (Join-Path $case.Root 'retained-missing-context.json') }
            'binding-other-transaction' { Copy-Item -LiteralPath $trustedOtherBinding -Destination (Join-Path $case.Backup 'transaction-binding.json') -Force }
            'journal-destination-change' { $jp=Join-Path $case.Backup 'installer-transaction.json';$j=Get-Content -LiteralPath $jp -Raw -Encoding UTF8 | ConvertFrom-Json;$j.destination=Join-Path $case.Root 'other-target';New-Sentinel (Join-Path $j.destination 'keep.bin') 'SYNTHETIC-FOREIGN-TARGET';[IO.File]::WriteAllText($jp,($j|ConvertTo-Json -Depth 80),[Text.UTF8Encoding]::new($false)) }
            'moved-exe' { $moved=Join-Path $case.Root 'moved';New-Item -ItemType Directory -Path $moved | Out-Null;$exe=Join-Path $moved 'JUST-uninstall.exe';Copy-Item -LiteralPath $case.Uninstaller -Destination $exe }
        }
        Refusal $case $activeCase '' $exe
    }
    foreach ($override in @('foreign-original-directory','foreign-destination','moved-exe-original-directory')) {
        $activeCase='refuse-'+$override;$case=New-Case $activeCase @('eq','reverb') $true;$exe='';$capture=$false
        $foreign=Join-Path $case.Root 'other-target';New-Sentinel (Join-Path $foreign 'keep.bin') 'SYNTHETIC-FOREIGN-OVERRIDE-TARGET'
        switch ($override) {
            'foreign-original-directory' { $extra='_?='+$foreign }
            'foreign-destination' { $extra='/D='+$foreign;$capture=$true }
            'moved-exe-original-directory' {
                $moved=Join-Path $case.Root 'moved';New-Item -ItemType Directory -Path $moved | Out-Null
                $exe=Join-Path $moved 'JUST-uninstall.exe';Copy-Item -LiteralPath $case.Uninstaller -Destination $exe
                $extra='_?='+$case.Backup
            }
        }
        Refusal $case $activeCase $extra $exe -CallerOverride -CaptureShadow:$capture
    }
    $activeCase='saved-engine-poison';$case=New-Case $activeCase @('eq') $true
    $marker=Join-Path $case.Root 'saved-engine-executed.marker';$saved=Join-Path $case.Backup 'Installer-Engine.ps1'
    [IO.File]::WriteAllText($saved,('Set-Content -LiteralPath '+(Quote $marker)+" -Value 'THIS-MUST-NOT-EXECUTE'`nthrow 'MUTATED_SAVED_ENGINE_MUST_NOT_RUN'"),[Text.UTF8Encoding]::new($false))
    $before=Evidence-State $case;Write-NewJson $before (Join-Path $evidence ('snapshot-'+$activeCase+'-before.json'))
    $run=Run-Uninstall $case $activeCase;$after=Evidence-State $case;Write-NewJson $after (Join-Path $evidence ('snapshot-'+$activeCase+'-after.json'))
    Check (-not (Test-Path -LiteralPath $marker)) 'Saved engine mutation never executed by actual uninstaller'
    if ($run.Process.ExitCode -eq 0) {
        Check ($run.Report.status -eq 'RESTORED' -and (Same $after.Target $case.Original)) 'Trusted embedded engine ignores mutable saved copy and restores exact old contents'
        $mode='TRUSTED_EMBEDDED_ENGINE_USED'
    } else {
        Check ($run.Report.status -eq 'REFUSED' -and (Same $before.Target $after.Target) -and (Same $before.EntireTransaction $after.EntireTransaction)) 'Saved engine SHA refusal preserves transaction and all target bytes'
        $mode='SAVED_COPY_SHA_REFUSED_BEFORE_EXECUTION'
    }
    Check (Same $before.Previous $after.Previous) 'Saved engine mutation does not corrupt old backup contents'
    Check (Same $before.Fixtures $after.Fixtures) 'Saved engine mutation leaves synthetic fixtures unchanged'
    Save-Case ([ordered]@{Name=$activeCase;Status='PASSED';Behavior=$mode;ActualEXE=$run;Before=$before;After=$after;PoisonMarkerExists=$false;RealTargetUsed=$false})

    foreach ($kind in @('missing-fresh-target','missing-old-target','known-partial-installed-target')) {
        $activeCase=$kind;$old=($kind -ne 'missing-fresh-target');$case=New-Case $activeCase @('eq') $old;$bundle=Join-Path $case.Target 'Just_eq.vst3'
        if ($kind -eq 'known-partial-installed-target') {
            $known='Contents\Resources\JustUI\icons\eq.png'
            Check (Test-Path -LiteralPath (Join-Path $candidate ('VST3\Just_eq.vst3\'+$known)) -PathType Leaf) 'Known partial removal fixture exists in unchanged candidate payload'
            Remove-Synthetic (Join-Path $bundle $known) $case.Root
        }
        else { Remove-Synthetic $bundle $case.Root }
        Success $case $activeCase
    }
    $activeCase='post-restore-new-user-contents';$case=New-Case $activeCase @('eq','reverb');Success $case 'post-restore-initial-exe'
    New-Sentinel (Join-Path $case.Target 'Just_eq.vst3\later-user-content.bin') 'SYNTHETIC-USER-CONTENT-ADDED-AFTER-RESTORE'
    New-Item -ItemType Directory -Path (Join-Path $case.Target 'Just_eq.vst3\later-user-empty-directory') | Out-Null
    Refusal $case $activeCase
    foreach ($later in @('same-candidate-reinstalled','same-name-empty-root','old-known-partial')) {
        $activeCase='post-restored-'+$later;$case=New-Case $activeCase @('eq') ($later -eq 'old-known-partial')
        Success $case ($activeCase+'-initial-restore')
        $bundle=Join-Path $case.Target 'Just_eq.vst3'
        switch ($later) {
            'same-candidate-reinstalled' {
                Check (-not (Test-Path -LiteralPath $bundle)) 'Earlier first-install transaction is absent before later synthetic reinstall'
                Copy-Item -LiteralPath (Join-Path $candidate 'VST3\Just_eq.vst3') -Destination $case.Target -Recurse
                Check (Same (Tree $bundle) (Tree (Join-Path $candidate 'VST3\Just_eq.vst3'))) 'Later synthetic reinstall is the exact unchanged known candidate including every directory and file'
            }
            'same-name-empty-root' { New-Item -ItemType Directory -Path $bundle | Out-Null }
            'old-known-partial' { Remove-Synthetic (Join-Path $bundle 'old-version-sentinel.bin') $case.Root }
        }
        Refusal $case $activeCase
    }
    foreach ($damage in @('missing-journal','corrupt-journal')) {
        $activeCase='unsafe-report-'+$damage;$case=New-Case $activeCase @('eq') $true
        $journalPath=Join-Path $case.Backup 'installer-transaction.json'
        if ($damage -eq 'missing-journal') { Move-Item -LiteralPath $journalPath -Destination (Join-Path $case.Root 'retained-missing-journal.json') }
        else { [IO.File]::WriteAllText($journalPath,'SYNTHETIC-INVALID-JOURNAL-MUST-NOT-AUTHORIZE-REPORT',[Text.UTF8Encoding]::new($false)) }
        Unsafe-Requested-Report $case $activeCase
    }
    $activeCase='delete-boundary-unknown-empty';$case=New-Case $activeCase @('eq','reverb') $true
    $extra='/TESTRESTOREBOUNDARYSLUG=eq /ISOLATIONTESTROOT='+(Quote $script:TestRoot)
    Refusal $case $activeCase $extra '' -Boundary
    Assert-FortyActualCases
    $status='PASSED'
} catch { $failure=$_.Exception.Message;Write-Warning $failure }
Write-NewJson ([ordered]@{Schema='JUST.resume25.actual-uninstaller-tests.v3';Status=$status;Failure=$failure;FirstFailureCase=if($status -eq 'FAILED'){$activeCase}else{''};Phase=$Phase;Continuation=$script:Continuation;ActualInstaller=$installer;InstallerSHA256=$installerSHA;TestScript=$PSCommandPath;TestScriptSHA256=(Hash $PSCommandPath);Historical27CaseTestBaselineSHA256='ea0a72a8dbccde83d0ffce9cad0c27a200ca34b80f04a15e8152feba21735283';OriginalInstallerBaselineEvidence=$baselinePath;Original51ChecksRerunHere=$false;Checks=$script:Checks.ToArray();CheckCount=$script:Checks.Count;Cases=$script:Cases.ToArray();CaseCount=$script:Cases.Count;IsolatedDirectory=$script:TestRoot;WorkerTEMP=$workerTemp;AdditionalCasesMayUseIndependentGUIDDirectoriesDirectlyBelowWorkerTEMP=$true;RealVST3Used=$false;RealReaper='NOT_RUN';GUIClaim='Only separately recorded actual confirmation cancellation; silent cases are actual EXE /S, not GUI';SilentExitContract='PowerShell Wait may wait descendants; the original NSIS launcher exit is not the Restore exit. Normal cases run exact original module directly with final _?=; foreign /D captures actual owned shadow handle/bytes/exit.';PrivateUserDataRead=$false;AllFixturesSynthetic=$true;TemporaryTreesAndBackupsRetained=$true;Utc=[DateTime]::UtcNow.ToString('o')}) $finalPath
Write-Output ('Actual uninstaller evidence: '+$finalPath)
if ($status -ne 'PASSED') { throw 'Actual uninstaller checks failed; retain the first failing case and do not repeat blindly.' }
