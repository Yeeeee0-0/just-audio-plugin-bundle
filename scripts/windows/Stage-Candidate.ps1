[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BuildDir, [Parameter(Mandatory=$true)][string]$EvidenceDir)
. (Join-Path $PSScriptRoot 'Common.ps1')
Require-WindowsX64
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$evidence = (Resolve-Path -LiteralPath $EvidenceDir).Path
$validation = Get-Content -LiteralPath (Join-Path $evidence 'candidate-validation.json') -Raw | ConvertFrom-Json
if (@($validation).Count -ne 10 -or @($validation | Where-Object { $_.validatorExit -ne 0 -or $_.failed -ne 0 -or $_.machine -ne 'AMD64' }).Count) { throw 'A passing validation record for all ten actual candidates is required.' }
[xml]$junit = Get-Content -LiteralPath (Join-Path $evidence 'ctest.xml') -Raw
if ($junit.testsuite.GetAttribute('failures') -ne '0' -or $junit.testsuite.GetAttribute('errors') -notin @('','0')) { throw 'CTest JUnit reports failures/errors.' }
foreach ($slug in Get-JustSlugs) {
    $test = @($junit.testsuite.testcase | Where-Object { $_.name -eq "windows-editor-$slug" })
    if ($test.Count -ne 1 -or $test[0].GetAttribute('status') -ne 'run') { throw "Native editor/runtime test evidence missing: $slug" }
}
$stage = Join-Path $root ('build\candidates\JUST-0.1.0-Windows-x64-candidate-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path (Join-Path $stage 'VST3') -Force | Out-Null
$items = @()
foreach ($slug in Get-JustSlugs) {
    $name = "Just_$slug.vst3"
    $bundle = Join-Path $build "VST3\Release\$name"
    $binary = Join-Path $bundle "Contents\x86_64-win\$name"
    $info = (Get-Item -LiteralPath $binary).VersionInfo
    if ($info.FileVersion -ne '0.1.0' -or $info.ProductVersion -ne '0.1.0' -or $info.CompanyName -ne 'Yee Huang') { throw "Version/vendor resource mismatch: $binary" }
    $hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    $record = @($validation | Where-Object { $_.slug -eq $slug })
    if ($record.Count -ne 1 -or $record[0].sha256 -ne $hash) { throw "Candidate changed after validation: $slug" }
    Copy-Item -LiteralPath $bundle -Destination (Join-Path $stage 'VST3') -Recurse
    $items += @{ slug=$slug; bundle="VST3/$name"; binary="VST3/$name/Contents/x86_64-win/$name"; sha256=$hash; version=$info.FileVersion; vendor=$info.CompanyName }
}
# Raw diagnostic evidence remains at EvidenceDir; review/redact separately before sharing.
Copy-Item -LiteralPath (Join-Path $root 'scripts\windows') -Destination (Join-Path $stage 'scripts') -Recurse
Copy-Item -LiteralPath (Join-Path $root 'docs\windows\ACCEPTANCE-zh.md') -Destination $stage
Write-JustJson @{ status='WINDOWS_NATIVE_AUTOMATED_CANDIDATE_NOT_REAPER_ACCEPTED'; baseline='b99abd3be4ad89b21e3a0c3caf1f12904503f168'; version='0.1.0'; vendor='Yee Huang'; architecture='x86_64'; format='VST3'; signed=$false; reaper='NOT_RUN'; manualVisualAudio='NOT_RUN'; plugins=$items; hashes=(Get-JustTreeHashes (Join-Path $stage 'VST3')) } (Join-Path $stage 'candidate-manifest.json')
$archiveTools = Find-JustTools
Invoke-Checked $archiveTools.Python ($archiveTools.PythonArgs + @((Join-Path $PSScriptRoot 'zip-candidate.py'), $stage, ($stage+'.zip')))
Write-Host "Staged unsigned Windows native candidate: $stage.zip"
Write-Host 'Actual REAPER visual/audio acceptance is still required; this is not a release approval.'
