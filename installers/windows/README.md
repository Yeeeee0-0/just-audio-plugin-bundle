# JUST 0.1.0 Windows preview installer

This source builds an **unsigned x64 VST3 test installer**. A successful build
or isolated installation test does not establish visual/audio acceptance in
the user's REAPER. The Windows computer must perform that separate check.

The installer offers ten independent components, all selected by default:
EQ, Reverb, Delay, Tremolo, Compressor, Limiter, Gate, Flanger, Wider and
Distortion. Wider retains its fixed `fake_stereo` module/file identity.
The displayed version is `0.1.0`, vendor `Yee Huang`.

## Build using the already installed NSIS

Run in Windows x64 PowerShell after `Stage-Candidate.ps1` has produced its
validated native candidate directory:

```powershell
./scripts/windows/Build-Installer.ps1 -CandidateDir 'C:\path\JUST-0.1.0-Windows-x64-candidate-TIMESTAMP'
./scripts/windows/Test-Installer.ps1 -CandidateDir 'C:\path\JUST-0.1.0-Windows-x64-candidate-TIMESTAMP'
```

No software is downloaded. `Build-Installer.ps1` discovers the preinstalled
NSIS 3 compiler through PATH or its standard Program Files location;
`-MakeNsisPath` accepts an explicit already installed compiler.

Default build outputs:

- `build/installer/JUST-0.1.0-Windows-x64-preview-setup.exe`
- `build/installer/SHA256SUMS.txt`
- `build/installer/installer-build.json`
- `build/installer/makensis.log`
- `build/installer/PREVIEW-NOTICE.txt`

Actual installer test results go to
`build/windows-evidence/installer/installer-tests.json` and adjacent per-case
reports/logs. `-OutputDir`, `-InstallerPath` and `-EvidenceDir` can override
those locations. Tests use a newly generated directory under Windows TEMP
and retain it if inspection is needed. They do not load REAPER.

## Interactive installation

Save current work and close REAPER normally. Run the verified setup executable,
choose the desired components, and select the x64 VST3 destination. The default
is the system's 64-bit Common Files `VST3` directory. The setup requests
administrator elevation for that machine-wide location.

Only selected `Just_<slug>.vst3` bundles in that destination are modified.
Unselected plugins and unrelated files are retained. The engine never reads,
backs up or edits user presets, REAPER configuration, licenses or projects.
It does not terminate running processes or install tooling.

Default plugin backup transactions are under
`%ProgramData%\JUST\Windows-preview-backups`.
The latest installer report is
`%ProgramData%\JUST\Windows-preview-logs\installer-last.json`.
Every backup has a unique timestamp/GUID directory and its own transaction
journal. Save the report and backup until real host acceptance is complete.

## CI / silent installation

`/S` is case-sensitive. `/D=` must be the final argument and is unquoted,
following NSIS's command-line convention. Paths containing spaces after `/D=`
are allowed. `/SELECT=` is for silent runs; interactive runs use the component
checkboxes.

```powershell
$exe = '.\build\installer\JUST-0.1.0-Windows-x64-preview-setup.exe'
Start-Process $exe -Wait -ArgumentList '/S /SELECT=eq,delay /BACKUPROOT="C:\test\backups" /REPORT="C:\test\report.json" /D=C:\test\VST3'
```

Omit `/SELECT=` to install all ten, or use `all`. Supported slugs are
`eq,reverb,delay,tremolo,compressor,limiter,gate,flanger,fake_stereo,distortion`.
Unknown, duplicate and empty selections are refused by the engine. Do not
place candidate, destination or backup paths inside one another. Reports must
also stay outside those trees. Local drive paths are required; reparse points,
UNC paths, drive roots and ambiguous Windows path components are refused.

## Verified backups and restoration

The engine checks the manifest's **entire ten-plugin SHA256 tree**, even when
only one plugin is selected. It also checks each binary's AMD64 PE header and
`0.1.0` / `Yee Huang` version resources. It verifies selected old bundles after
copying them into the backup, verifies staged replacements, and journals each
replacement before changing the target.

If installation fails, it attempts to restore selected touched bundles from
those verified backups. If a target acquired unknown external modifications,
rollback stops and reports `ROLLBACK_BLOCKED` instead of discarding them.
A power loss or forcibly terminated installer leaves a journal for explicit
restoration. Backups contain selected plugin files and installation metadata
only; no user presets or REAPER private files.

Close REAPER normally and run the copy of the engine saved in the backup from
64-bit PowerShell. Use an elevated PowerShell session when restoring a
machine-wide Common Files installation:

```powershell
& 'C:\path\backup\Installer-Engine.ps1' -Action Restore `
  -BackupDir 'C:\path\backup' -ReportPath 'C:\path\restore-report.json'
```

A successful restore reports `RESTORED`. It verifies every required backup and
checks targets against the known transaction before changing any files.
No general-purpose uninstaller or registry uninstall entry is installed;
restoration acts on this transaction's selected components only.

## What the isolated test script proves

The script runs the **actual built NSIS executable** for default selection and
for EQ-only selection. It compares installed files with the source candidate,
checks nine unselected old-plugin sentinels, checks the selected prior-version
backup and explicit restoration, and checks unrelated files. It separately
runs the same packaged engine with a failure injected after the first selected
replacement, verifies automatic rollback, and proves that tampering an
unselected candidate causes refusal before destination creation. Failure
injection is accepted only when all paths are inside an explicit fresh
Windows TEMP test root.

Test-created preset/configuration/license/project sentinels remain byte-for-byte
unchanged. These are synthetic fixtures; no real private files are read.
Neither these tests nor the manifest claim a user REAPER session, visual
matching, automation acceptance or audible DSP acceptance.

NSIS's official [scripting reference](https://nsis.sourceforge.io/Docs/Chapter4.html)
defines the 64-bit Common Files destination, all-user folder context and silent
`/D=` command-line behavior used here.
