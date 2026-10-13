# Bounded helper fixtures for destination scope and truthful failure guidance.
# Run on Windows; this does not install a payload, start a DAW or touch real VST3.
[CmdletBinding()]
param([string]$EvidencePath)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitProcess) { throw 'Run these fixtures in native Windows x64 PowerShell.' }
$fixtureRoot=Join-Path ([IO.Path]::GetTempPath()) ('JUST-target-scope-'+[Guid]::NewGuid().ToString('N'))
$fixtureEngine=Join-Path $PSScriptRoot 'Installer-Engine.ps1'
$fixtureBinding=Join-Path $PSScriptRoot 'Installer-Binding.ps1'
$fixtureChecks=New-Object Collections.Generic.List[object]
$fixtureResult=[ordered]@{ schema=1; status='RUNNING'; scope='Windows helper fixtures only; no actual Setup/Restore, DAW, DSP or user-data acceptance'; root=$fixtureRoot; engineSHA256=(Get-FileHash -LiteralPath $fixtureEngine -Algorithm SHA256).Hash.ToLowerInvariant(); bindingSHA256=(Get-FileHash -LiteralPath $fixtureBinding -Algorithm SHA256).Hash.ToLowerInvariant(); checks=@(); error=''; utc=[DateTime]::UtcNow.ToString('o') }
function Add-FixtureCheck([string]$Name,[bool]$Passed) {
    $fixtureChecks.Add([ordered]@{ name=$Name; passed=$Passed })
    if (-not $Passed) { throw "Fixture assertion failed: $Name" }
}
function Expect-FixtureRefusal([string]$Name,[scriptblock]$Action,[string]$ExpectedPattern) {
    $refused=$false
    try { $null=& $Action }
    catch { if ($_.Exception.Message -notmatch $ExpectedPattern) { throw }; $refused=$true }
    Add-FixtureCheck $Name $refused
}
try {
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    . $fixtureEngine -LibraryOnly
    # Import only the trusted, pure classification function. Do not execute the
    # Binding entry point or its engine/installer/restore operations.
    $tokens=$null; $parseErrors=$null
    $bindingAst=[System.Management.Automation.Language.Parser]::ParseFile($fixtureBinding,[ref]$tokens,[ref]$parseErrors)
    if (@($parseErrors).Count) { throw 'Binding source has PowerShell parse errors.' }
    $definitions=@($bindingAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq 'Get-BindingFailureAdvice' },$false))
    if ($definitions.Count -ne 1) { throw 'Exactly one failure classification helper is required.' }
    . ([scriptblock]::Create($definitions[0].Extent.Text))

    $fixtureTarget=Join-Path $fixtureRoot 'VST3'
    $fixtureExternal=Join-Path $fixtureRoot 'external-sentinel'
    $fixtureSelected=Join-Path $fixtureTarget 'Just_flanger.vst3'
    $fixtureVendor=Join-Path $fixtureTarget 'OtherVendor'
    foreach ($folder in @($fixtureTarget,$fixtureExternal,$fixtureSelected,$fixtureVendor)) { New-Item -ItemType Directory -Path $folder | Out-Null }
    [IO.File]::WriteAllText((Join-Path $fixtureExternal 'keep.txt'),'synthetic external sentinel')
    [IO.File]::WriteAllText((Join-Path $fixtureSelected 'synthetic.dll'),'synthetic selected payload, not executable')
    $fixtureExternalBefore=Get-InstallerState $fixtureExternal
    $fixtureSelectedBefore=Get-InstallerState $fixtureSelected
    $fixtureVendorLink=Join-Path $fixtureVendor 'data-link'
    New-Item -ItemType Junction -Path $fixtureVendorLink -Target $fixtureExternal | Out-Null

    # This fixture reproduces the old rejection, then checks the new bounded
    # container policy and the unchanged selected-tree availability/state guards.
    Expect-FixtureRefusal 'Old full-container scan encounters unrelated junction' { Assert-InstallerTree $fixtureTarget } 'Reparse point refused'
    Assert-InstallerContainer $fixtureTarget
    Assert-InstallerTreeAvailability $fixtureSelected 'FIXTURE_SELECTED_ONLY'
    Add-FixtureCheck 'Container allows unrelated vendor junction and selected bundle remains identical' (Equal-InstallerState (Get-InstallerState $fixtureSelected) $fixtureSelectedBefore)
    Add-FixtureCheck 'Other vendor junction remains present' (((Get-Item -LiteralPath $fixtureVendorLink -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)

    $fixtureRootLink=Join-Path $fixtureTarget 'Just_delay.vst3'
    New-Item -ItemType Junction -Path $fixtureRootLink -Target $fixtureExternal | Out-Null
    Expect-FixtureRefusal 'Selected bundle root junction is refused' { Get-InstallerState $fixtureRootLink } 'Reparse'
    $fixtureChildBundle=Join-Path $fixtureTarget 'Just_gate.vst3'
    New-Item -ItemType Directory -Path $fixtureChildBundle | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $fixtureChildBundle 'resource-link') -Target $fixtureExternal | Out-Null
    Expect-FixtureRefusal 'Selected bundle descendant junction is refused before traversal' { Get-InstallerState $fixtureChildBundle } 'Reparse transaction entry refused'
    $fixtureAncestorLink=Join-Path $fixtureRoot 'VST3-alias'
    New-Item -ItemType Junction -Path $fixtureAncestorLink -Target $fixtureTarget | Out-Null
    Expect-FixtureRefusal 'Container ancestor/root junction remains refused' { Assert-InstallerContainer (Join-Path $fixtureAncestorLink 'Just_flanger.vst3') } 'Reparse'
    $fixtureBackup=Join-Path $fixtureRoot 'own-backup'
    New-Item -ItemType Directory -Path $fixtureBackup | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $fixtureBackup 'bad-link') -Target $fixtureExternal | Out-Null
    Expect-FixtureRefusal 'Owned backup retains full-tree reparse rejection' { Assert-InstallerTree $fixtureBackup } 'Reparse point refused'
    Add-FixtureCheck 'External sentinel is unchanged' (Equal-InstallerState (Get-InstallerState $fixtureExternal) $fixtureExternalBefore)

    $cases=@(
        @{ name='Host running'; text='REFUSED: REAPER is running. Save work and close it normally, then retry.'; code=-1; authority=$false; expected='HOST_RUNNING' },
        @{ name='Actual selected sharing error'; text='Selected plugin is unavailable for replacement. Path: X:\fixture\file.dll. Win32Error: 32. HRESULT: 0x80070020.'; code=32; authority=$true; expected='SELECTED_PLUGIN_SHARING_VIOLATION' },
        @{ name='Actual selected access denial'; text='Selected plugin is unavailable for replacement. Path: X:\fixture\file.dll. Win32Error: 5. HRESULT: 0x80070005.'; code=5; authority=$true; expected='SELECTED_PLUGIN_ACCESS_DENIED' },
        @{ name='Actual reparse refusal'; text='Reparse transaction entry refused: X:\fixture\resource-link'; code=-1; authority=$true; expected='REPARSE_POINT_REFUSED' },
        @{ name='Directory called reparse is not itself a reparse diagnosis'; text='Selected plugin is unavailable for replacement. Path: X:\reparse\file.dll. Win32Error: 32. HRESULT: 0x80070020.'; code=32; authority=$true; expected='SELECTED_PLUGIN_SHARING_VIOLATION' },
        @{ name='Other verified failure'; text='Target changed after backup; refused: Just_flanger.vst3'; code=-1; authority=$true; expected='ENGINE_FAILURE' },
        @{ name='Missing authority'; text='Native engine did not create its report.'; code=-1; authority=$false; expected='AUTHORITY_REPORT_MISSING_OR_INVALID' }
    )
    foreach ($case in $cases) {
        $original=[string]$case.text
        $classification=Get-BindingFailureAdvice $original $case.code '' $case.authority 1
        Add-FixtureCheck ($case.name+' classification') ($classification.failureKind -ceq $case.expected)
        if ($case.expected -notin @('HOST_RUNNING','SELECTED_PLUGIN_SHARING_VIOLATION')) {
            Add-FixtureCheck ($case.name+' has no blanket close-DAW remedy') ($classification.guidance -notmatch '(?i)close (?:any DAW|REAPER|the application)')
        }
    }
    $success=Get-BindingFailureAdvice '' -1 '' $true 0
    Add-FixtureCheck 'Verified success has no failure guidance' ($success.failureKind -ceq '' -and $success.guidance -ceq '')
    $fixtureResult.status='PASS_HELPER_FIXTURES_NOT_SETUP_RUNTIME_ACCEPTANCE'
} catch {
    $fixtureResult.status='FAILED'; $fixtureResult.error=$_.Exception.ToString()
} finally {
    $fixtureResult.checks=$fixtureChecks.ToArray()
    if (-not $EvidencePath) { $EvidencePath=Join-Path $fixtureRoot 'target-scope-fixtures.json' }
    $fixtureResult | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $EvidencePath -Encoding UTF8
    $fixtureResult | ConvertTo-Json -Depth 12
    # Retain only this newly generated synthetic fixture for inspection.
    # No recursive deletion, real plugin folder or private profile is involved.
}
if ($fixtureResult.status -ceq 'FAILED') { exit 1 }
