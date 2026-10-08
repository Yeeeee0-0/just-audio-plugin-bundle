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
$manifest=Get-Content -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.status -ne 'WINDOWS_NATIVE_AUTOMATED_CANDIDATE_NOT_REAPER_ACCEPTED' -or @($manifest.plugins).Count -ne 10 -or $manifest.version -ne '0.1.0') { throw 'Expected the staged native ten-plugin candidate.' }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$exe=Join-Path $output 'JUST-0.1.0-Windows-x64-preview-setup.exe'
$nsisVersion=& $MakeNsisPath /VERSION
if ($LASTEXITCODE -ne 0 -or "$nsisVersion" -notmatch 'v?3\.') { throw 'NSIS 3 version probe failed.' }
$arguments=@('/V3',"/DCANDIDATE_DIR=$candidate","/DOUTPUT_FILE=$exe","/DENGINE_SCRIPT=$(Join-Path $PSScriptRoot 'Installer-Engine.ps1')","/DNOTICE_FILE=$(Join-Path $root 'installers\windows\PREVIEW-NOTICE.txt')",(Join-Path $root 'installers\windows\JustPreview.nsi'))
$old=$ErrorActionPreference
try { $ErrorActionPreference='Continue'; & $MakeNsisPath @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'makensis.log'); $code=$LASTEXITCODE }
finally { $ErrorActionPreference=$old }
if ($code -ne 0 -or -not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "NSIS build failed ($code)." }
$hash=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($exe))" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ASCII
@{ schema=1; status='BUILT_NOT_INSTALLER_TESTED'; version='0.1.0'; vendor='Yee Huang'; architecture='x86_64'; format='VST3'; signed=$false; userReaperValidation='NOT_RUN'; installer=[IO.Path]::GetFileName($exe); sha256=$hash; nsisVersion="$nsisVersion"; candidateManifestSHA256=(Get-FileHash -LiteralPath (Join-Path $candidate 'candidate-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant(); selectedByDefault='all ten'; utc=[DateTime]::UtcNow.ToString('o') } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'installer-build.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'installers\windows\PREVIEW-NOTICE.txt') -Destination $output -Force
Write-Output "Built unsigned preview installer: $exe"
