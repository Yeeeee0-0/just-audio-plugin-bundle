[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CandidateDir,
    [string]$OutputDir,
    [string]$MakeNsisPath
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
if ([Environment]::OSVersion.Platform -ne 'Win32NT' -or -not [Environment]::Is64BitProcess) { throw 'Build this installer with native Windows x64 PowerShell.' }
$root=(Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$candidate=(Resolve-Path -LiteralPath $CandidateDir).Path
if (-not $OutputDir) { $OutputDir=Join-Path $root 'build\installer' }
$output=[IO.Path]::GetFullPath($OutputDir)
if (-not $MakeNsisPath) {
    $command=Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) { $MakeNsisPath=$command.Source }
    else {
        foreach ($path in @((Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe'),(Join-Path $env:ProgramFiles 'NSIS\makensis.exe'))) {
            if (Test-Path -LiteralPath $path -PathType Leaf) { $MakeNsisPath=$path; break }
        }
    }
}
if (-not $MakeNsisPath -or -not (Test-Path -LiteralPath $MakeNsisPath -PathType Leaf)) { throw 'Preinstalled official NSIS 3 is required. No automatic download or installation is performed.' }
$engine=Join-Path $PSScriptRoot 'Installer-Engine.ps1'
$binding=Join-Path $PSScriptRoot 'Installer-Binding.ps1'
$nsi=Join-Path $root 'installers\windows\JustPreview.nsi'
$notice=Join-Path $root 'installers\windows\PREVIEW-NOTICE.txt'
# Use the current source engine's complete candidate verification. The
# candidate's auxiliary archived scripts are not this installer's source.
$manifest=& { param($verifiedEngine,$verifiedCandidate); . $verifiedEngine -LibraryOnly; Read-InstallerCandidate $verifiedCandidate } $engine $candidate
$engineSHA256=(Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
$manifestSHA256=(Get-FileHash -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe=Join-Path $output 'JUST-0.1.0-Windows-x64-preview-setup.exe'
$nsisVersion=& $MakeNsisPath /VERSION
if ($LASTEXITCODE -ne 0 -or "$nsisVersion" -notmatch 'v?3\.') { throw 'NSIS 3 version probe failed.' }
$arguments=@('/V3',"/DCANDIDATE_DIR=$candidate","/DOUTPUT_FILE=$exe","/DENGINE_SCRIPT=$engine","/DBINDING_SCRIPT=$binding","/DENGINE_SHA256=$engineSHA256","/DMANIFEST_SHA256=$manifestSHA256","/DNOTICE_FILE=$notice",$nsi)
$old=$ErrorActionPreference
try { $ErrorActionPreference='Continue'; & $MakeNsisPath @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'makensis.log'); $code=$LASTEXITCODE }
finally { $ErrorActionPreference=$old }
if ($code -ne 0 -or -not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "NSIS build failed ($code)." }
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($exe))" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
$sourceHashes=[ordered]@{}
foreach ($relative in @('installers/windows/JustPreview.nsi','scripts/windows/Installer-Engine.ps1','scripts/windows/Installer-Binding.ps1','scripts/windows/Build-Installer.ps1','scripts/windows/Test-Installer.ps1','scripts/windows/Test-Uninstaller.ps1','scripts/windows/Test-InstallerTargetScope.ps1','installers/windows/README.md','installers/windows/PREVIEW-NOTICE.txt')) {
    $path=Join-Path $root $relative
    $sourceHashes[$relative]=if (Test-Path -LiteralPath $path -PathType Leaf) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() } else { 'NOT_PRESENT_NOT_INCLUDED' }
}
@{ schema=3; status='BUILT_NOT_INSTALLER_OR_UNINSTALLER_TESTED'; version='0.1.0'; vendor='Yee Huang'; architecture='x86_64'; format='VST3'; signed=$false; userReaperValidation='NOT_RUN'; installer=[IO.Path]::GetFileName($exe); sha256=$hash; nsisVersion="$nsisVersion"; candidateManifestSHA256=$manifestSHA256; candidateDirectory=$candidate; payloadVerification='FULL_TEN_PLUGIN_TREE_VERIFIED_BY_CURRENT_SOURCE_ENGINE'; payloadFileCount=@($manifest.hashes.PSObject.Properties).Count; payloadHashes=$manifest.hashes; plugins=$manifest.plugins; sourceSHA256=$sourceHashes; embeddedEngineSHA256=$engineSHA256; selectedByDefault='all ten'; uninstallerFile='JUST-uninstall.exe'; bindingFile='transaction-binding.json'; installAuthority='Per-Setup private install-authority.json and unique setupInvocationId; external REPORT is evidence only'; engineInvocation='Structured native PowerShell -File arguments from fixed UTF-16 resource request'; transactionBinding='Schema 3 full stable context, Setup invocation, owned nonce and whole-bundle stage/park/retired roles on the same destination volume outside the selected VST3 scan root, plus a per-transaction EXE trailer; writable metadata is not digital authentication'; hostReplacement='Hosts may remain open if selected whole bundles can be renamed; mapped old bundles are retained; hard locks refuse without closing hosts; normal host reopen activates the canonical installed version; no DSP hot switch'; completionPolicy='INSTALLED only after candidate swaps, backups and exact uninstall binding/EXE Finalize; every later entry failure invokes only this private Setup transaction recovery and returns failed Setup'; legacySchema2Recovery='Retained schema 2 transactions use their original packaged engine and original uninstall entry'; earlyErrorReport='Original installation report parent/uninstall-early-error-<setupInvocationId>-<actualPID>.json after independent path proof, otherwise actual stderr'; nsisTrailerReference='https://github.com/kichik/nsis/blob/master/Source/exehead/fileform.c#L235-L241'; nsisSelfCopyReference='https://github.com/kichik/nsis/blob/master/Source/exehead/Main.c#L304-L391'; utc=[DateTime]::UtcNow.ToString('o') } | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $output 'installer-build.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'installers\windows\PREVIEW-NOTICE.txt') -Destination $output -Force
Write-Output "Built unsigned preview installer: $exe"
