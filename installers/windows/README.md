# JUST 0.1.0 Windows preview installer

This source builds an **unsigned x64 VST3 test installer**. A successful build
or isolated installation test does not establish visual/audio acceptance in
the user's REAPER. The Windows computer must perform that separate check.

The installer offers ten independent components, all selected by default:
EQ, Reverb, Delay, Tremolo, Compressor, Limiter, Gate, Flanger, Wider and
Distortion. Wider retains its fixed `fake_stereo` module/file identity.
The displayed version is `0.1.0`, vendor `Yee Huang`.

## Build using the already installed NSIS

Run in Windows x64 PowerShell with an already validated native candidate.
The installer build can reuse that exact payload; it does not rebuild plugins:

```powershell
./scripts/windows/Build-Installer.ps1 -CandidateDir 'D:\work\JUST-0.1.0-Windows-x64-candidate-TIMESTAMP' -OutputDir 'D:\work\installer'
```

Prefer a verified fixed local work volume for source, outputs, evidence and
worker TEMP/TMP. Keep required Windows shared locations intact. The build
verifies the complete ten-plugin payload with the current source engine and
records actual NSI, engine, binding helper, build and test source hashes.
Candidate ZIP auxiliary scripts are payload provenance, not the new Setup's
installer source.

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

Save current work. Hosts may remain open if the selected whole bundles can be
renamed. Old loaded bundles are kept intact; existing instances keep their old
code. A hard lock refuses the move without closing the host. Close and reopen
the host normally to activate the installed version; there is no DSP hot switch.
Run the verified setup executable, choose components, and select the x64 VST3
destination. The default is the system's 64-bit Common Files `VST3` directory.
The setup requests administrator elevation for that machine-wide location.

Only selected `Just_<slug>.vst3` bundles in that destination are modified.
Unselected plugins and unrelated files are retained. The engine never reads,
backs up or edits user presets, REAPER configuration, licenses or projects.
It does not terminate running processes or install tooling.

Default plugin backup transactions are under
`%ProgramData%\JUST\Windows-preview-backups`.
The default installer report has a unique Setup GUID name under
`%ProgramData%\JUST\Windows-preview-logs\installer-<setupInvocationId>.json`.
Every backup has a unique timestamp/GUID directory and its own transaction
journal. Save the report and backup until real host acceptance is complete.

Each real Setup first generates a private invocation GUID and context inside
its own NSIS resource directory. The trusted engine receives that GUID and
writes its authoritative installation report into the same private directory.
Create/Finalize bind only that report and cross-check the GUID with the schema 3
journal and full stable context; the journal must be `SWAPPED_ENTRY_PENDING`.
Only exact swaps, backups and completed uninstall Finalize permit `INSTALLED`
with `installerCompletion=READY`. External
`/REPORT=` is a readable evidence copy, never the sole binding authority, even
when two Setup invocations intentionally reuse it. The private temporary files
are removed with normal NSIS cleanup; the durable journal/binding/report retain
the invocation identity. The native engine receives structured individual
`-File` arguments from the helper, so supported path lengths are not truncated
into a 1024-character NSIS engine command.

## CI / silent installation

`/S` is case-sensitive. `/D=` must be the final argument and is unquoted,
following NSIS's command-line convention. Paths containing spaces after `/D=`
are allowed. `/SELECT=` is for silent runs; interactive runs use the component
checkboxes.

```powershell
$exe = '.\build\installer\JUST-0.1.0-Windows-x64-preview-setup.exe'
Start-Process $exe -Wait -WindowStyle Hidden -ArgumentList '/S /SELECT=eq,delay /BACKUPROOT="D:\test\backups" /REPORT="D:\test\reports\install.json" /D=D:\test\VST3'
```

Omit `/SELECT=` to install all ten, or use `all`. Supported slugs are
`eq,reverb,delay,tremolo,compressor,limiter,gate,flanger,fake_stereo,distortion`.
Unknown, duplicate and empty selections are refused by the engine. Do not
place candidate, destination or backup paths inside one another. Reports must
also stay outside the candidate, destination, swap and backup trees. Local drive
paths are required. Selected JUST bundles, their descendants and path ancestors
must have no reparse points; UNC paths, drive roots and ambiguous components are
refused. Unrelated vendor links elsewhere in the shared destination are neither
traversed nor changed and do not by themselves block this transaction.

## Verified backups and restoration

The engine checks the manifest's **entire ten-plugin SHA256 tree**, even when
only one plugin is selected. It also checks each binary's AMD64 PE header and
`0.1.0` / `Yee Huang` version resources. Before any canonical replacement, it
verifies all selected old backups and complete staged candidate bundles.

Schema 3 binds full original/new root state and root identity, the engine and
manifest hashes, and the Setup invocation. A nonce-owned swap directory is on
the destination volume outside the selected VST3 scan root. The original whole
bundle is renamed into its bound `park` path; the preverified whole replacement
is moved to the original canonical path. Intent is journaled before each move,
and its result and observed identities are journaled afterward. Mapped old
bundles stay intact; no mapped tree is recursively deleted.

If installation fails, it attempts the inverse of this transaction's known
moves. Unknown external targets are never overwritten; blocked recovery reports
`ROLLBACK_BLOCKED` and preserves the journal and trees. Hard locks may require
saving work and closing the host normally before retrying recovery. A power loss
or forcibly terminated installer leaves its bound journal for explicit recovery.
Keep the recorded backup and owned swap directories together. These contain
selected plugin bundles and metadata, not user presets or REAPER private files.

Setup claims completion only after the install report says `INSTALLED` with
`installerCompletion=READY`, including the exact uninstall entry. Its path is the report's
`uninstallerPath`: `<this transaction's backupDirectory>\JUST-uninstall.exe`.
The adjacent fixed `transaction-binding.json` and journal must stay there.
No registry entry or Start menu shortcut is created.

卸载／撤销本次安装：撤销本次所选的全部插件；首次安装的插件将移除，覆盖安装的插件将恢复安装前版本；保留本次备份。

Save work. Run that exact EXE in place to see the original VST3 destination,
ordered selected plugin identities and external report path. Hosts may remain
open if the required whole-bundle moves succeed; locks may require a normal
host close. Close and reopen normally to activate the restored canonical version.
Cancel does not invoke Restore or change plugins/journal. Confirm undoes the
entire selection from this transaction; there is no arbitrary old-transaction
picker or individual uninstall selection. The unsigned entry requests the
same administrator elevation as Setup.

Use `/REPORT=` for a fresh report outside both the backup and VST3 destination.
Without it, the default is
`%ProgramData%\JUST\Windows-preview-logs\uninstall-<actual process ID>.json`.
The tested silent command uses NSIS's final unquoted `_?=` to wait for the
original EXE rather than its normal temporary shadow copy. It MUST equal the
generated entry's actual original directory; foreign `_?=` and `/D=` overrides
do not select another transaction:

```powershell
$entry = 'D:\test\backups\EXACT-TRANSACTION\JUST-uninstall.exe'
$originalDirectory = Split-Path -Parent $entry
$uninstallArguments = '/S /REPORT="D:\test\reports\uninstall.json" _?=' + $originalDirectory
Start-Process $entry -Wait -WindowStyle Hidden -ArgumentList $uninstallArguments
```

A successful actual EXE restore reports `RESTORED`; a refusal reports `REFUSED`,
`RESTORE_BLOCKED` or `ROLLBACK_BLOCKED` and an error. Retain the journal, old backups, binding and uninstall EXE after
success. Every entry failure after successful swaps (Create, Finalize, missing
transfer context, WriteUninstaller or the bounded interleaving wait) invokes
`RollbackInstall` for this Setup's exact private authority and schema 3 context.
The original helper/NSIS error is retained before recovery changes its output.
Actual recovery reports `ROLLED_BACK_AFTER_ENTRY_FAILURE` or `ROLLBACK_BLOCKED`;
Setup returns nonzero and aborts even when the inverse succeeds. A blocked or
unprovable recovery preserves incomplete state and never reports complete Setup.

No requested report is written before the exact uninstall context and binding
are verified and the path is proved outside the original backup/destination.
Early errors preserve that requested path. Once the physical original entry
and its trailer can be verified, a safe early refusal report can instead appear
in the original installation report's parent directory as
`uninstall-early-error-<setupInvocationId>-<actual process ID>.json`. Its fixed
file path is separately checked outside the original trees. If the physical
entry itself cannot be verified, only the actual error output/exit code is
available. Existing detailed engine restore failures are retained.

The embedded trusted engine is extracted and checked against its build-time
SHA; the mutable saved engine in the backup is never executed by this entry.
A per-transaction EXE trailer fixes the original directory, transaction ID,
binding hash and engine hash. The schema 3 binding cross-checks destination,
manifest, ordered selection, full original/new root type, existence, directories
and hashes, root identities, swap ownership nonce and same-volume stage/park
paths outside the scan root. Retained schema 2 transactions continue through
their original uninstall EXE and original packaged engine; the new engine
refuses old journals. Moving the EXE or losing/changing its binding
refuses operation. NSIS's native self-copy is supported only in its specific
TEMP/`~nsu<hex>.tmp`/`Un.exe` layout, with matching actual process path, internal
original-directory command line, original EXE still present and full byte hash.
These checks prevent accidental mis-binding; they are not a digital signature
or protection against an administrator forging writable metadata.

Before any rename, the engine preflights ALL selected targets, old backups and
bound swap trees; it repeats full-state, ownership, object-identity and
binding/journal/engine checks immediately before each selected move. Unknown
bytes, files, empty directories, root type changes and reparse entries refuse
operation. Restore moves the known current bundle outside the scan root and
returns the verified original whole bundle without deleting live mapped trees.
An authorized missing upgrade target may recover its verified original; initial
installs already absent are idempotent. Ordinary mutexes and rechecks do not
promise atomic exclusion of arbitrary external writers. After successful
restoration, repeated calls permit only the exact original full state and
identity as a no-op; later reinstallations or unknown same-name trees are kept.

## Archived schema 2 fixture coverage

The descriptions below cover the retained earlier installer and test records.
They do not claim that the older full test matrix was rerun for schema 3. Current
schema 3 validation must use its bounded Engine/Binding/Setup and live-host
reports, including locks, inverse failures and normal host reopening.

The archived script ran the **actual built NSIS executable** for default selection and
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

The archived `Test-Uninstaller.ps1` separately checked the actual generated uninstall EXE,
interactive cancellation evidence, default-ten / EQ-only / mixed restoration,
unknown-state rejection, binding/backup failures, moved entry / external
directory overrides, deletion-boundary mutation and repeat restores. Its test
hook is limited to `/TESTRESTOREBOUNDARYSLUG=<selected slug>` plus
`/ISOLATIONTESTROOT=<dedicated temporary fixture>`; it can create only the fixed
unknown empty directory `.resume25-boundary-unknown-empty`. It accepts no
arbitrary mutation path or code. Counts/results must be read from the current
test reports; no unrun EXE, GUI or host check is implied by this description.

The directed invocation interleaving fixture uses
`/TESTINSTALLBINDINGGAPROLE=A /TESTINSTALLBINDINGGAPROOT=<fresh fixture>`.
It is accepted only in a `JUST-install-binding-tests-<GUID>` root, with worker
TEMP exactly its `temp` child and all paths contained there. After the private
engine success, fixed `A-ready.json` exposes the synthetic invocation/report
identity; the helper waits at most 60 seconds for fixed `A-release.json` carrying
the same invocation ID. No arbitrary callback, code, target override or marker
path is accepted. This lets a second real Setup overwrite a shared public
report while proving the first still binds its own private transaction.

NSIS's official [scripting reference](https://nsis.sourceforge.io/Docs/Chapter4.html)
defines the 64-bit Common Files destination, all-user folder context and silent
`/D=` command-line behavior used here.
The official [uninstaller command-line reference](https://nsis.sourceforge.io/Docs/Chapter3.html#uninstallerusage)
describes `_?=` and normal self-copy. The upstream
[loader](https://github.com/kichik/nsis/blob/master/Source/exehead/fileform.c#L235-L241)
ends CRC at the NSIS data CRC and permits an appended trailer; this build keeps
`CRCCheck force`. The upstream
[startup](https://github.com/kichik/nsis/blob/master/Source/exehead/Main.c#L304-L391)
copies the complete EXE into its native temporary directory.
