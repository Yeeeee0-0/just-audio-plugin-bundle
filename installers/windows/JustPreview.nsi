Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"
!include "Sections.nsh"

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
Name "JUST 0.1.0 Windows x64 Preview"
OutFile "${OUTPUT_FILE}"
InstallDir "$COMMONFILES64\VST3"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
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
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "JUST Windows x64 Preview"
!define MUI_WELCOMEPAGE_TEXT "Install any of the ten JUST VST3 plugins. All are selected by default.$\r$\n$\r$\nVersion 0.1.0 by Yee Huang.$\r$\n$\r$\nThis preview is unsigned. It has not been tested in the user's REAPER installation. Save your work and close REAPER normally before continuing.$\r$\n$\r$\nOnly selected plugins are replaced after verified backups. Presets, licenses, settings and projects are not accessed."
!insertmacro MUI_PAGE_WELCOME
!define MUI_LICENSEPAGE_TEXT_TOP "Read the preview and backup notice before installing."
!insertmacro MUI_PAGE_LICENSE "${NOTICE_FILE}"
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the x64 VST3 directory used by your host. Only selected JUST bundles in this directory will be backed up and replaced. Unselected plugins remain untouched."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_TITLE "Selected JUST preview plugins installed"
!define MUI_FINISHPAGE_TEXT "The selected bundles passed file integrity checks. This is an unsigned preview, not REAPER visual/audio acceptance.$\r$\n$\r$\nKeep the backup directory recorded in the installer report. Open REAPER normally and use a new test project.$\r$\n$\r$\nInstaller report:$\r$\n$ReportPath"
!insertmacro MUI_PAGE_FINISH
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
  DetailPrint "Selected plugins: $Selected"
  DetailPrint "Verified backups: $BackupRoot"
  DetailPrint "Report: $ReportPath"
  nsExec::ExecToLog '"$WINDIR\sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$PLUGINSDIR\Installer-Engine.ps1" -CandidateDir "$PLUGINSDIR\candidate" -Destination "$INSTDIR" -BackupRoot "$BackupRoot" -Selected "$Selected" -ReportPath "$ReportPath"'
  Pop $ExitResult
  ${If} $ExitResult != 0
    SetErrorLevel 1
    IfSilent +2
      MessageBox MB_ICONSTOP "Installation did not complete. The engine attempts to restore selected plugins if a replacement failed.$\r$\n$\r$\nRead the report before retrying:$\r$\n$ReportPath"
    Abort
  ${EndIf}
  SetErrorLevel 0
SectionEnd

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
  StrCpy $ReportPath "$APPDATA\JUST\Windows-preview-logs\installer-last.json"
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
  ; /SELECT applies to unattended use. Interactive users select the ten
  ; component checkboxes; no external script edits the component state.
  IfSilent 0 done
  ${GetOptions} $Parameters "/SELECT=" $SelectionOverride
  done:
FunctionEnd
