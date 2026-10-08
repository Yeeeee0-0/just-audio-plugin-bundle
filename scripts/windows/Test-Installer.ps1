# Runs the actual NSIS executable only against fresh temporary directories.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CandidateDir,
    [string]$InstallerPath,
    [string]$EvidenceDir
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitProcess) { throw 'Native Windows x64 required.' }
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $InstallerPath) { $InstallerPath=Join-Path $root 'build\installer\JUST-0.1.0-Windows-x64-preview-setup.exe' }
if (-not $EvidenceDir) { $EvidenceDir=Join-Path $root 'build\windows-evidence\installer' }
$installer=(Resolve-Path -LiteralPath $InstallerPath).Path
$candidate=(Resolve-Path -LiteralPath $CandidateDir).Path
$evidence=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('JUST-installer-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$script:Checks=New-Object Collections.Generic.List[string]
$slugs=@('eq','reverb','delay','tremolo','compressor','limiter','gate','flanger','fake_stereo','distortion')
$engine=Join-Path $PSScriptRoot 'Installer-Engine.ps1'
$powershell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'

function Check-Installer([bool]$Value,[string]$Description) { if (-not $Value) { throw "FAIL: $Description" }; $script:Checks.Add($Description); Write-Host "PASS: $Description" }
function Snapshot-Installer([string]$Path) {
    $result=New-Object Collections.Generic.List[string]
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return @('$file='+(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash) }
    if (Test-Path -LiteralPath $Path -PathType Container) {
        $base=(Get-Item -LiteralPath $Path).FullName.TrimEnd('\')+'\'
        foreach ($file in @(Get-ChildItem -LiteralPath $Path -Recurse -Force -File | Sort-Object FullName)) { $result.Add($file.FullName.Substring($base.Length).Replace('\','/')+'='+(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) }
    }
    return @($result.ToArray())
}
function Same-InstallerSnapshot($A,$B) { return (($A -join "`n") -ceq ($B -join "`n")) }
function New-InstallerSentinel([string]$Path,[string]$Text) { New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null; [IO.File]::WriteAllText($Path,$Text) }
function Quote-InstallerArgument([string]$Value) { if ($Value.Contains('"')) { throw 'Unexpected quote in test argument.' }; return '"'+$Value+'"' }
function Invoke-InstallerExe([string]$Name,[string]$Dest,[string]$Backups,[string]$Selection='') {
    $report=Join-Path $evidence ($Name+'.json')
    $arguments='/S /BACKUPROOT='+(Quote-InstallerArgument $Backups)+' /REPORT='+(Quote-InstallerArgument $report)
    if ($Selection) { $arguments+=' /SELECT='+$Selection }
    # NSIS /D must be the last argument and consumes its remainder unquoted.
    $arguments+=' /D='+$Dest
    $process=Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru
    Check-Installer ($process.ExitCode -eq 0) "$Name actual NSIS process exit 0"
    Check-Installer (Test-Path -LiteralPath $report) "$Name writes an engine report"
    $record=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Check-Installer ($record.status -eq 'INSTALLED') "$Name transaction INSTALLED"
    return $record
}
function Invoke-InstallerEngine([string]$Name,[string[]]$EngineArguments,[int]$ExpectedExit,[string]$EnginePath=$engine) {
    $arguments=@('-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$EnginePath)+$EngineArguments
    $line=($arguments | ForEach-Object { Quote-InstallerArgument $_ }) -join ' '
    $process=Start-Process -FilePath $powershell -ArgumentList $line -Wait -PassThru -RedirectStandardOutput (Join-Path $evidence ($Name+'.stdout.log')) -RedirectStandardError (Join-Path $evidence ($Name+'.stderr.log'))
    Check-Installer ($process.ExitCode -eq $ExpectedExit) "$Name engine exit $ExpectedExit"
}
$status='FAILED'; $failure=''
try {
    # Sentinel files simulate local private data, but are entirely test-created.
    $private=Join-Path $testRoot 'private-sentinels'
    New-InstallerSentinel (Join-Path $private 'JUST\Presets\v1\keep.justpreset') 'FAKE-USER-PRESET-UNCHANGED'
    New-InstallerSentinel (Join-Path $private 'REAPER\reaper.ini') 'FAKE-HOST-CONFIG-UNCHANGED'
    New-InstallerSentinel (Join-Path $private 'REAPER\license.rk') 'FAKE-LICENSE-UNCHANGED'
    New-InstallerSentinel (Join-Path $private 'REAPER\project.rpp') 'FAKE-PROJECT-UNCHANGED'
    $privateBefore=Snapshot-Installer $private
    $allTarget=Join-Path $testRoot 'all\VST3'; $allBackup=Join-Path $testRoot 'all\backups'
    New-InstallerSentinel (Join-Path $allTarget 'OtherVendor.vst3\keep.txt') 'OTHER-VENDOR-UNCHANGED'
    $all=Invoke-InstallerExe 'default-all' $allTarget $allBackup
    Check-Installer (@($all.selected).Count -eq 10) 'Default actual installer selects ten plugins'
    foreach ($slug in $slugs) { Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $allTarget "Just_$slug.vst3")) (Snapshot-Installer (Join-Path $candidate "VST3\Just_$slug.vst3"))) "Default $slug payload exact SHA256 set" }
    Check-Installer ((Get-Content -LiteralPath (Join-Path $allTarget 'OtherVendor.vst3\keep.txt') -Raw) -eq 'OTHER-VENDOR-UNCHANGED') 'Default installer preserves another vendor bundle'

    $oneTarget=Join-Path $testRoot 'one\VST3'; $oneBackup=Join-Path $testRoot 'one\backups'; $before=@{}
    foreach ($slug in $slugs) { New-InstallerSentinel (Join-Path $oneTarget "Just_$slug.vst3\old-version-sentinel.bin") "PREVIOUS-$slug"; $before[$slug]=Snapshot-Installer (Join-Path $oneTarget "Just_$slug.vst3") }
    New-InstallerSentinel (Join-Path $oneTarget 'unrelated.txt') 'UNRELATED-UNCHANGED'
    $one=Invoke-InstallerExe 'selected-eq' $oneTarget $oneBackup 'eq'
    Check-Installer (@($one.selected).Count -eq 1 -and $one.selected[0] -eq 'eq') 'Actual installer records only the selected EQ component'
    Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $oneTarget 'Just_eq.vst3')) (Snapshot-Installer (Join-Path $candidate 'VST3\Just_eq.vst3'))) 'Selected EQ installed exactly'
    foreach ($slug in $slugs | Where-Object { $_ -ne 'eq' }) { Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $oneTarget "Just_$slug.vst3")) $before[$slug]) "Unselected $slug sentinel unchanged" }
    Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $one.backup 'previous\Just_eq.vst3')) $before['eq']) 'Upgrade backup exactly preserves previous selected EQ'
    Check-Installer (@(Get-ChildItem -LiteralPath (Join-Path $one.backup 'previous')).Count -eq 1) 'Backup contains only selected existing plugin'
    $restoreReport=Join-Path $evidence 'selected-eq-restored.json'
    Invoke-InstallerEngine 'selected-eq-restore' @('-Action','Restore','-BackupDir',$one.backup,'-ReportPath',$restoreReport) 0 (Join-Path $one.backup 'Installer-Engine.ps1')
    foreach ($slug in $slugs) { Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $oneTarget "Just_$slug.vst3")) $before[$slug]) "Explicit restore preserves original $slug" }
    Check-Installer ((Get-Content -LiteralPath (Join-Path $oneTarget 'unrelated.txt') -Raw) -eq 'UNRELATED-UNCHANGED') 'Unrelated target file unchanged after installation and restore'

    $isolatedCandidate=Join-Path $testRoot 'failure\candidate'
    New-Item -ItemType Directory -Path (Split-Path -Parent $isolatedCandidate) -Force | Out-Null
    Copy-Item -LiteralPath $candidate -Destination $isolatedCandidate -Recurse
    $failTarget=Join-Path $testRoot 'failure\VST3'; $failBackup=Join-Path $testRoot 'failure\backups'; $failureBefore=@{}
    foreach ($slug in @('eq','reverb')) { New-InstallerSentinel (Join-Path $failTarget "Just_$slug.vst3\old.bin") "ROLLBACK-$slug"; $failureBefore[$slug]=Snapshot-Installer (Join-Path $failTarget "Just_$slug.vst3") }
    $failReport=Join-Path $testRoot 'failure\rollback-report.json'
    Invoke-InstallerEngine 'injected-copy-failure' @('-CandidateDir',$isolatedCandidate,'-Destination',$failTarget,'-BackupRoot',$failBackup,'-Selected','eq,reverb','-ReportPath',$failReport,'-TestFailAfter','1','-IsolationTestRoot',$testRoot) 1
    $rollback=Get-Content -LiteralPath $failReport -Raw | ConvertFrom-Json
    Check-Installer ($rollback.status -eq 'ROLLED_BACK') 'Injected failure automatically rolls back'
    foreach ($slug in @('eq','reverb')) { Check-Installer (Same-InstallerSnapshot (Snapshot-Installer (Join-Path $failTarget "Just_$slug.vst3")) $failureBefore[$slug]) "Failed transaction preserves original $slug" }
    Copy-Item -LiteralPath $failReport -Destination (Join-Path $evidence 'rollback-report.json')

    $tamper=Join-Path $isolatedCandidate 'VST3\Just_reverb.vst3\Contents\Resources\JustUI\icons\reverb.png'
    [IO.File]::AppendAllText($tamper,'INTEGRITY-TEST')
    $refusedTarget=Join-Path $testRoot 'refused\VST3'; $refusedBackups=Join-Path $testRoot 'refused\backups'; $refusedReport=Join-Path $evidence 'tampered-unselected.json'
    Invoke-InstallerEngine 'tampered-unselected' @('-CandidateDir',$isolatedCandidate,'-Destination',$refusedTarget,'-BackupRoot',$refusedBackups,'-Selected','eq','-ReportPath',$refusedReport) 1
    Check-Installer (-not (Test-Path -LiteralPath $refusedTarget)) 'Tampering an unselected candidate plugin refuses before destination creation'
    $refused=Get-Content -LiteralPath $refusedReport -Raw | ConvertFrom-Json
    Check-Installer ($refused.status -eq 'REFUSED') 'Corrupt candidate has an explicit refused result'
    Check-Installer (Same-InstallerSnapshot (Snapshot-Installer $private) $privateBefore) 'Test presets, host configuration, license and project remain byte-identical'
    $status='PASSED'
} catch { $failure=$_.Exception.Message; Write-Warning $failure }
@{ schema=1; status=$status; actualInstaller=(Split-Path -Leaf $installer); installerSHA256=(Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant(); checks=$script:Checks.ToArray(); checkCount=$script:Checks.Count; failure=$failure; isolatedDirectory=$testRoot; realReaper='NOT_RUN'; visualAudioAcceptance='NOT_RUN'; scope='Actual NSIS silent installation plus isolated backup/restore/failure tests'; utc=[DateTime]::UtcNow.ToString('o') } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'installer-tests.json') -Encoding UTF8
Write-Output "Installer test evidence: $evidence"
if ($status -ne 'PASSED') { throw "Installer tests failed; isolated files retained at $testRoot" }
