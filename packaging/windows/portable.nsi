Unicode true
!include "FileFunc.nsh"
!include "x64.nsh"

Name "OpenKai2 ${VERSION}"
OutFile "${OUTPUT_FILE}"
RequestExecutionLevel user
SilentInstall silent
AutoCloseWindow true
SetCompressor /SOLID lzma
SetCompressorDictSize 32
VIProductVersion "${VERSION_NUMERIC}"
VIAddVersionKey "ProductName" "OpenKai2"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "OpenKai2 portable launcher"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "OpenKai2 contributors"

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "OpenKai2 requires 64-bit Windows."
        Abort
    ${EndIf}
FunctionEnd

Section
    InitPluginsDir
    SetOutPath "$PLUGINSDIR\OpenKai2"
    File /r "${PAYLOAD_DIR}\*"
    ${GetParameters} $0
    ClearErrors
    ExecWait '"$PLUGINSDIR\OpenKai2\OpenKai2.exe" $0' $1
    IfErrors launch_failed
    SetOutPath "$TEMP"
    SetErrorLevel $1
    Goto done
launch_failed:
    MessageBox MB_OK|MB_ICONSTOP "OpenKai2 could not start. Extract the portable ZIP and try OpenKai2.exe directly."
    SetErrorLevel 1
done:
SectionEnd
