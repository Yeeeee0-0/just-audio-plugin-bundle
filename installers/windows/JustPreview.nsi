Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "Sections.nsh"
!include "nsDialogs.nsh"

!ifndef CANDIDATE_DIR
  !error "CANDIDATE_DIR must name the validated native candidate."
!endif
!ifndef OUTPUT_FILE
  !error "OUTPUT_FILE is required."
!endif
!ifndef ENGINE_SCRIPT
  !error "ENGINE_SCRIPT is required."
!endif
!ifndef NOTICE_FILE
  !error "NOTICE_FILE is required."
!endif
!ifndef BINDING_SCRIPT
  !error "BINDING_SCRIPT is required."
!endif
!ifndef ENGINE_SHA256
  !error "ENGINE_SHA256 is required."
!endif
!ifndef MANIFEST_SHA256
  !error "MANIFEST_SHA256 is required."
!endif
Name "JUST 0.1.0 Windows x64 Preview"
OutFile "${OUTPUT_FILE}"
InstallDir "$COMMONFILES64\VST3"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
CRCCheck force
ShowInstDetails show
BrandingText "Yee Huang | Unsigned Windows preview"
VIProductVersion "0.1.0.0"
VIAddVersionKey "ProductName" "JUST Audio Plugin Bundle Windows Preview"
VIAddVersionKey "ProductVersion" "0.1.0"
VIAddVersionKey "FileVersion" "0.1.0"
VIAddVersionKey "CompanyName" "Yee Huang"
VIAddVersionKey "FileDescription" "Unsigned x64 VST3 preview installer; REAPER acceptance pending"
VIAddVersionKey "LegalCopyright" "Yee Huang"

Var Selected
Var SelectionOverride
Var BackupRoot
Var ReportPath
Var Parameters
Var ExitResult
Var TransactionDirectory
Var UnDestination
Var UnSelected
Var EntryProcessId
Var RestoreBoundarySlug
Var IsolationTestRoot
Var SetupInvocationId
Var InstallBindingGapRoot
Var InstallBindingGapRole
Var BindingOutput
Var BindingPreview
Var BindingDiagnosticPath
Var InstallSwapsCompleted
Var InstallEntryError
Var InstallEntryErrorPath
Var InstallEntryErrorPreview
Var InstallEntryRecoveryPreview
Var InstallEntryRecoveryExit
!macro RunBinding OP ENTRY
  ; The fixed UTF-16 request keeps long supported local paths out of NSIS's
  ; 1024-character command string. It is data, never interpolated PS code.
  Delete "$PLUGINSDIR\binding-request.ini"
  Push $R9
  FileOpen $R9 "$PLUGINSDIR\binding-request.ini" w
  FileWriteUTF16LE /BOM $R9 "[Request]$\r$\n"
  FileClose $R9
  Pop $R9
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "ManifestSHA256" "${MANIFEST_SHA256}"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "InstallReportPath" "$PLUGINSDIR\install-authority.json"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "ExpectedDestination" "$INSTDIR"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "ExpectedSelected" "$Selected"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "TransferPath" "$PLUGINSDIR\entry.ini"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "EntryPath" "${ENTRY}"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "ClaimedDirectory" "$INSTDIR"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "EntryProcessId" "$EntryProcessId"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "ReportPath" "$ReportPath"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "TestRestoreBoundarySlug" "$RestoreBoundarySlug"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "IsolationTestRoot" "$IsolationTestRoot"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "BackupRoot" "$BackupRoot"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "SetupInvocationId" "$SetupInvocationId"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "TestInstallBindingGapRoot" "$InstallBindingGapRoot"
  WriteINIStr "$PLUGINSDIR\binding-request.ini" "Request" "TestInstallBindingGapRole" "$InstallBindingGapRole"
  ; The official nsExec contract returns exit status then bounded output.
  ; Keep that actual output even when binding refuses the requested report.
  nsExec::ExecToStack '"$WINDIR\sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\Installer-Binding.ps1" -Operation ${OP} -ExpectedEngineSHA256 "${ENGINE_SHA256}" -RequestPath "$PLUGINSDIR\binding-request.ini"'
  Pop $ExitResult
  Pop $BindingOutput
  StrCpy $BindingPreview $BindingOutput 700
  StrCpy $BindingDiagnosticPath "$PLUGINSDIR\binding-${OP}-diagnostic.log"
  Push $R9
  ClearErrors
  FileOpen $R9 "$BindingDiagnosticPath" w
  ${IfNot} ${Errors}
    FileWriteUTF16LE /BOM $R9 "operation=${OP}$\r$\nactualExitStatus=$ExitResult$\r$\ncaptureLimit=${NSIS_MAX_STRLEN} characters (nsExec::ExecToStack)$\r$\nactualOutput:$\r$\n"
    FileWriteUTF16LE $R9 "$BindingOutput$\r$\n"
    FileClose $R9
  ${Else}
    DetailPrint "Could not retain the temporary helper diagnostic. Actual exit: $ExitResult"
  ${EndIf}
  Pop $R9
  DetailPrint "Helper ${OP} actual exit: $ExitResult; bounded output: $BindingOutput"
  DetailPrint "Temporary helper diagnostic: $BindingDiagnosticPath"
  ; Existing callers still pop only the actual helper exit status.
  Push $ExitResult
!macroend

!define MUI_ICON "${__FILEDIR__}\art-v2\JUST-Installer.ico"
!define MUI_UNICON "${__FILEDIR__}\art-v2\JUST-Installer.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${__FILEDIR__}\art-v2\JUST-Welcome-164x314.bmp"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "JUST Windows x64 Preview"
!define MUI_WELCOMEPAGE_TEXT "Install any of the ten JUST VST3 plugins. All are selected by default.$\r$\n$\r$\nVersion 0.1.0 by Yee Huang. Unsigned preview; full host/audio acceptance is pending.$\r$\n$\r$\nSave projects. Hosts may stay open if selected bundles can be renamed. Old loaded bundles are kept; locks refuse installation without closing hosts.$\r$\n$\r$\nClose and reopen your host normally to use the installed version. No DSP hot switch.$\r$\n$\r$\nOnly selected bundles change after verified backups. Private settings, projects, presets and licenses are not accessed."
!insertmacro MUI_PAGE_WELCOME
!define MUI_LICENSEPAGE_TEXT_TOP "Read the preview and backup notice before installing."
!insertmacro MUI_PAGE_LICENSE "${NOTICE_FILE}"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the x64 VST3 directory used by your host. Only selected JUST bundles in this directory will be backed up and replaced. Unselected plugins remain untouched."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Selected JUST preview plugins installed"
!define MUI_FINISHPAGE_TEXT "Selected bundles passed integrity checks. This unsigned preview still requires host/audio acceptance.$\r$\n$\r$\nOld loaded bundles are kept. Save projects, then close and reopen your host normally to use the installed version; no DSP hot switch.$\r$\n$\r$\nKeep the recorded backup and transaction paths.$\r$\n$\r$\nInstaller report:$\r$\n$ReportPath"
!insertmacro MUI_PAGE_FINISH
UninstPage custom un.ConfirmTransaction
!define MUI_CUSTOMFUNCTION_UNABORT un.JustCancelTransaction
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!macro PluginSection TITLE SLUG ID
Section "${TITLE}" ${ID}
  ${If} $Selected == ""
    StrCpy $Selected "${SLUG}"
  ${Else}
    StrCpy $Selected "$Selected,${SLUG}"
  ${EndIf}
SectionEnd
!macroend
!insertmacro PluginSection "JUST EQ" "eq" SEC_EQ
!insertmacro PluginSection "JUST Reverb" "reverb" SEC_REVERB
!insertmacro PluginSection "JUST Delay" "delay" SEC_DELAY
!insertmacro PluginSection "JUST Tremolo" "tremolo" SEC_TREMOLO
!insertmacro PluginSection "JUST Compressor" "compressor" SEC_COMPRESSOR
!insertmacro PluginSection "JUST Limiter" "limiter" SEC_LIMITER
!insertmacro PluginSection "JUST Gate" "gate" SEC_GATE
!insertmacro PluginSection "JUST Flanger" "flanger" SEC_FLANGER
!insertmacro PluginSection "JUST Wider" "fake_stereo" SEC_WIDER
!insertmacro PluginSection "JUST Distortion" "distortion" SEC_DISTORTION

Section "-Install selected VST3 bundles" SEC_INSTALL
  SectionIn RO
  ${If} $SelectionOverride != ""
    StrCpy $Selected $SelectionOverride
  ${EndIf}
  ${If} $Selected == ""
    SetErrorLevel 1
    IfSilent +2
      MessageBox MB_ICONEXCLAMATION "Select at least one JUST plugin. Nothing installed."
    Abort
  ${EndIf}
  InitPluginsDir
  SetOutPath "$PLUGINSDIR\candidate"
  File /oname=candidate-manifest.json "${CANDIDATE_DIR}\candidate-manifest.json"
  SetOutPath "$PLUGINSDIR\candidate\VST3"
  File /r "${CANDIDATE_DIR}\VST3\*"
  SetOutPath "$PLUGINSDIR"
  File /oname=Installer-Engine.ps1 "${ENGINE_SCRIPT}"
  File /oname=Installer-Binding.ps1 "${BINDING_SCRIPT}"
  !insertmacro RunBinding PrepareInstall "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    Goto entry_failed
  ${EndIf}
  ReadINIStr $SetupInvocationId "$PLUGINSDIR\invocation.ini" "Invocation" "id"
  ReadINIStr $ReportPath "$PLUGINSDIR\invocation.ini" "Invocation" "reportPath"
  ${If} $SetupInvocationId == ""
    StrCpy $BindingPreview "PrepareInstall did not supply this Setup's invocation identity. The install engine was not invoked."
    Goto entry_failed
  ${EndIf}
  DetailPrint "Selected plugins: $Selected"
  DetailPrint "Verified backups: $BackupRoot"
  DetailPrint "Report: $ReportPath"
  !insertmacro RunBinding InvokeInstall "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    SetErrorLevel 1
    IfSilent +2
      MessageBox MB_ICONSTOP "Installation was stopped.$\r$\n$\r$\nEngine diagnostic:$\r$\n$BindingPreview$\r$\n$\r$\nUse the guidance for the actual error above. Keep the exact report before retrying:$\r$\n$ReportPath"
    Abort
  ${EndIf}
  ; A successful engine swap still needs this invocation's uninstall entry.
  StrCpy $InstallSwapsCompleted 1
  ${If} $InstallBindingGapRoot != ""
  ${OrIf} $InstallBindingGapRole != ""
    !insertmacro RunBinding WaitBindingGap "$EXEPATH"
    Pop $ExitResult
    ${If} $ExitResult != 0
      Goto entry_failed
    ${EndIf}
  ${EndIf}
  ; The exact pending-swap report supplies this unique transaction. Never use
  ; latest-directory discovery, the product destination, or a caller override.
  !insertmacro RunBinding Create "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    Goto entry_failed
  ${EndIf}
  ReadINIStr $TransactionDirectory "$PLUGINSDIR\entry.ini" "Entry" "backupDirectory"
  ${If} $TransactionDirectory == ""
    StrCpy $BindingPreview "Create did not supply this invocation's exact transaction directory. Setup did not complete."
    Goto entry_failed
  ${EndIf}
  ClearErrors
  WriteUninstaller "$TransactionDirectory\JUST-uninstall.exe"
  IfErrors 0 uninstall_written
  StrCpy $BindingPreview "WriteUninstaller failed for this invocation's transaction entry. Setup did not complete."
  Goto entry_failed
  uninstall_written:
  ; Appending the per-transaction trailer preserves the native NSIS CRC and
  ; is copied with the entire EXE by NSIS's normal uninstaller self-copy.
  !insertmacro RunBinding Finalize "$TransactionDirectory\JUST-uninstall.exe"
  Pop $ExitResult
  ${If} $ExitResult != 0
    Goto entry_failed
  ${EndIf}
  DetailPrint "Undo this transaction: $TransactionDirectory\JUST-uninstall.exe"
  SetErrorLevel 0
  Goto entry_done
  entry_failed:
  ; Preserve the original helper/NSIS error before recovery updates its output.
  StrCpy $InstallEntryError $BindingPreview
  StrCpy $InstallEntryErrorPath $BindingDiagnosticPath
  StrCpy $InstallEntryErrorPreview $InstallEntryError 350
  DetailPrint "Original entry diagnostic: $InstallEntryError"
  DetailPrint "Original helper diagnostic file: $InstallEntryErrorPath"
  ${If} $InstallSwapsCompleted == 1
    ; Recovery derives ONLY this invocation's private authority and context.
    !insertmacro RunBinding RollbackInstall "$EXEPATH"
    Pop $InstallEntryRecoveryExit
    StrCpy $InstallEntryRecoveryPreview $BindingPreview 250
    DetailPrint "Entry recovery actual exit: $InstallEntryRecoveryExit; diagnostic: $BindingPreview"
  ${Else}
    StrCpy $InstallEntryRecoveryPreview "The install engine did not report successful swaps. Entry recovery was not invoked."
  ${EndIf}
  ; Even a successful inverse is an incomplete, failed Setup.
  SetErrorLevel 1
  IfSilent +2
    MessageBox MB_ICONSTOP "Setup did not complete.$\r$\n$\r$\nOriginal entry diagnostic (preview):$\r$\n$InstallEntryErrorPreview$\r$\n$\r$\nRecovery diagnostic (preview):$\r$\n$InstallEntryRecoveryPreview$\r$\n$\r$\nPreserve the transaction and report:$\r$\n$ReportPath"
  Abort
  entry_done:
SectionEnd

Section "Uninstall"
  ; Always execute the resource engine, never backup\Installer-Engine.ps1.
  !insertmacro RunBinding Restore "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    SetErrorLevel 1
    IfSilent +2
      MessageBox MB_ICONSTOP "Undo was refused or did not finish.$\r$\n$\r$\nActual engine/helper diagnostic:$\r$\n$BindingPreview$\r$\n$\r$\nPreserve the transaction and read:$\r$\n$ReportPath"
    Abort
  ${EndIf}
  DetailPrint "Transaction restored. Old backups and this uninstall entry are retained."
  DetailPrint "Report: $ReportPath"
  SetErrorLevel 0
SectionEnd

Function un.onInit
  ${IfNot} ${RunningX64}
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  System::Call 'kernel32::GetCurrentProcessId() i .r0'
  StrCpy $EntryProcessId $0
  StrCpy $ReportPath "$APPDATA\JUST\Windows-preview-logs\uninstall-$EntryProcessId.json"
  StrCpy $RestoreBoundarySlug ""
  StrCpy $IsolationTestRoot ""
  ${un.GetParameters} $Parameters
  StrCpy $0 ""
  ${un.GetOptions} $Parameters "/REPORT=" $0
  ${If} $0 != ""
    StrCpy $ReportPath $0
  ${EndIf}
  ${un.GetOptions} $Parameters "/TESTRESTOREBOUNDARYSLUG=" $RestoreBoundarySlug
  ${un.GetOptions} $Parameters "/ISOLATIONTESTROOT=" $IsolationTestRoot
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=Installer-Engine.ps1 "${ENGINE_SCRIPT}"
  File /oname=Installer-Binding.ps1 "${BINDING_SCRIPT}"
  !insertmacro RunBinding Describe "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    SetErrorLevel 1
    IfSilent +2
      MessageBox MB_ICONSTOP "Uninstall transaction binding was refused. Nothing restored.$\r$\n$\r$\nActual helper exit: $ExitResult$\r$\nActual helper output (first 700 captured characters):$\r$\n$BindingPreview"
    Abort
  ${EndIf}
  ReadINIStr $UnDestination "$PLUGINSDIR\entry.ini" "Entry" "destination"
  ReadINIStr $UnSelected "$PLUGINSDIR\entry.ini" "Entry" "selected"
FunctionEnd

Function un.ConfirmTransaction
  !insertmacro MUI_HEADER_TEXT "Undo this installation" "Restore this transaction's selected plugins; keep its backups."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateLabel} 0 0 100% 34u "Undo ALL plugins selected in this transaction. First-time installations are removed; overwritten plugins return to their verified original versions. The transaction backup is kept. Cancel performs no Restore."
  Pop $0
  ${NSD_CreateLabel} 0 37u 100% 10u "Original VST3 destination:"
  Pop $0
  nsDialogs::CreateControl EDIT ${DEFAULT_STYLES}|${WS_TABSTOP}|${ES_MULTILINE}|${ES_AUTOVSCROLL}|${ES_READONLY}|${WS_VSCROLL} ${WS_EX_CLIENTEDGE} 0 48u 100% 22u "$UnDestination"
  Pop $0
  SendMessage $0 ${EM_SETREADONLY} 1 0
  ${NSD_CreateLabel} 0 73u 100% 10u "Selected plugin identities (Wider = fake_stereo):"
  Pop $0
  nsDialogs::CreateControl EDIT ${DEFAULT_STYLES}|${WS_TABSTOP}|${ES_MULTILINE}|${ES_AUTOVSCROLL}|${ES_READONLY}|${WS_VSCROLL} ${WS_EX_CLIENTEDGE} 0 84u 100% 24u "$UnSelected"
  Pop $0
  SendMessage $0 ${EM_SETREADONLY} 1 0
  ${NSD_CreateLabel} 0 111u 100% 10u "External report:"
  Pop $0
  nsDialogs::CreateControl EDIT ${DEFAULT_STYLES}|${WS_TABSTOP}|${ES_MULTILINE}|${ES_AUTOVSCROLL}|${ES_READONLY}|${WS_VSCROLL} ${WS_EX_CLIENTEDGE} 0 122u 100% 24u "$ReportPath"
  Pop $0
  SendMessage $0 ${EM_SETREADONLY} 1 0
  nsDialogs::Show
FunctionEnd

Function un.JustCancelTransaction
  ; Normal cancellation only records the decision; it never invokes Restore.
  !insertmacro RunBinding Cancel "$EXEPATH"
  Pop $ExitResult
  ${If} $ExitResult != 0
    DetailPrint "Cancellation report was refused. No Restore was called; preserve the safe refusal evidence."
  ${EndIf}
  SetErrorLevel 1
FunctionEnd

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "This preview requires native Windows x64."
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  StrCpy $Selected ""
  StrCpy $SelectionOverride ""
  StrCpy $BackupRoot "$APPDATA\JUST\Windows-preview-backups"
  ; PrepareInstall chooses a fresh GUID-named default external report. It is
  ; evidence only; binding always reads this invocation's private authority.
  StrCpy $ReportPath ""
  StrCpy $SetupInvocationId ""
  StrCpy $InstallBindingGapRoot ""
  StrCpy $InstallBindingGapRole ""
  StrCpy $InstallSwapsCompleted 0
  ${GetParameters} $Parameters
  StrCpy $0 ""
  ${GetOptions} $Parameters "/BACKUPROOT=" $0
  ${If} $0 != ""
    StrCpy $BackupRoot $0
  ${EndIf}
  StrCpy $0 ""
  ${GetOptions} $Parameters "/REPORT=" $0
  ${If} $0 != ""
    StrCpy $ReportPath $0
  ${EndIf}
  ${GetOptions} $Parameters "/TESTINSTALLBINDINGGAPROOT=" $InstallBindingGapRoot
  ${GetOptions} $Parameters "/TESTINSTALLBINDINGGAPROLE=" $InstallBindingGapRole
  ; /SELECT applies to unattended use. Interactive users select the ten
  ; component checkboxes; no external script edits the component state.
  IfSilent 0 done
  ${GetOptions} $Parameters "/SELECT=" $SelectionOverride
  done:
FunctionEnd
