Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenKai2"
Name "OpenKai2 ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\OpenKai2"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32
VIProductVersion "${VERSION_NUMERIC}"
VIAddVersionKey "ProductName" "OpenKai2"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "OpenKai2 Windows installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "OpenKai2 contributors"

!define MUI_ABORTWARNING
!define MUI_ICON "${ICON}"
!define MUI_UNICON "${ICON}"
!define MUI_FINISHPAGE_RUN "$INSTDIR\OpenKai2.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PAYLOAD_DIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "OpenKai2 requires 64-bit Windows."
        Abort
    ${EndIf}
    SetShellVarContext current
    SetRegView 64
FunctionEnd

Function un.onInit
    SetShellVarContext current
    SetRegView 64
FunctionEnd

Section "OpenKai2"
    SetOutPath "$INSTDIR"
    File /r "${PAYLOAD_DIR}\*"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    CreateDirectory "$SMPROGRAMS\OpenKai2"
    CreateShortcut "$SMPROGRAMS\OpenKai2\OpenKai2.lnk" "$INSTDIR\OpenKai2.exe"
    CreateShortcut "$SMPROGRAMS\OpenKai2\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "OpenKai2"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "OpenKai2 contributors"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/7v0biscuit/OpenKai2"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\OpenKai2.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_KIB}
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
SectionEnd

Section "Uninstall"
    !include "${DELETE_FILES}"
    Delete "$INSTDIR\Uninstall.exe"
    Delete "$SMPROGRAMS\OpenKai2\OpenKai2.lnk"
    Delete "$SMPROGRAMS\OpenKai2\Uninstall.lnk"
    RMDir "$SMPROGRAMS\OpenKai2"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
SectionEnd
