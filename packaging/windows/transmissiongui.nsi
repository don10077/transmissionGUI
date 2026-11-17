Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\transmissionGUI"
!define SHELL_APP_KEY "Software\Classes\Applications\transmissiongui.exe"
!define TORRENT_COMMAND "Software\Classes\transmissionGUI.Torrent\shell\open\command"
!define MAGNET_COMMAND "Software\Classes\transmissionGUI.Magnet\shell\open\command"

Name "TransmissionGUI ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\transmissionGUI"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "TransmissionGUI"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "TransmissionGUI Setup"
VIAddVersionKey /LANG=0 "LegalCopyright" "TransmissionGUI contributors"

!define MUI_ICON "${__FILEDIR__}\..\..\app\icons\transmission-upstream.ico"
!define MUI_UNICON "${MUI_ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\TransmissionGUI.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

LangString Needs64Bit ${LANG_ENGLISH} "TransmissionGUI requires 64-bit Windows."
LangString Needs64Bit ${LANG_RUSSIAN} "Для TransmissionGUI нужна 64-разрядная Windows."
LangString CloseApp ${LANG_ENGLISH} "Close TransmissionGUI and try again."
LangString CloseApp ${LANG_RUSSIAN} "Закройте TransmissionGUI и повторите попытку."
LangString UpgradeFailed ${LANG_ENGLISH} "The existing installation could not be removed."
LangString UpgradeFailed ${LANG_RUSSIAN} "Не удалось удалить предыдущую установку."

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "$(Needs64Bit)" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Section "TransmissionGUI" MainSection
    ; Run the previous uninstaller before copying files, so renamed DLLs do not
    ; accumulate. When the user picked another directory, remove the old one too.
    ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 != ""
    ${AndIf} ${FileExists} "$0\uninstall.exe"
        ClearErrors
        ExecWait '"$0\uninstall.exe" /S _?=$0' $1
        ${If} ${Errors}
        ${OrIf} $1 != 0
            MessageBox MB_ICONSTOP "$(UpgradeFailed)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
        ${If} $0 != $INSTDIR
            Delete "$0\uninstall.exe"
            RMDir "$0"
        ${EndIf}
    ${EndIf}

    ; A running copy keeps its executable locked; ask the user to close it first.
    ${If} ${FileExists} "$INSTDIR\TransmissionGUI.exe"
        ClearErrors
        FileOpen $9 "$INSTDIR\TransmissionGUI.exe" a
        ${If} ${Errors}
            MessageBox MB_ICONSTOP "$(CloseApp)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
        FileClose $9
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${PAYLOAD}\*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "$SMPROGRAMS\TransmissionGUI.lnk" "$INSTDIR\TransmissionGUI.exe"

    WriteRegStr HKCU "${SHELL_APP_KEY}" "FriendlyAppName" "TransmissionGUI"
    WriteRegStr HKCU "${SHELL_APP_KEY}\SupportedTypes" ".torrent" ""
    WriteRegStr HKCU "${SHELL_APP_KEY}\shell\open\command" "" '"$INSTDIR\TransmissionGUI.exe" "%1"'
    ; Preserve enabled associations when moving from a portable or development copy.
    ReadRegStr $0 HKCU "${TORRENT_COMMAND}" ""
    ${If} $0 != ""
        WriteRegStr HKCU "${TORRENT_COMMAND}" "" '"$INSTDIR\TransmissionGUI.exe" -- "%1"'
    ${EndIf}
    ReadRegStr $0 HKCU "${MAGNET_COMMAND}" ""
    ${If} $0 != ""
        WriteRegStr HKCU "${MAGNET_COMMAND}" "" '"$INSTDIR\TransmissionGUI.exe" -- "%1"'
    ${EndIf}
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "TransmissionGUI"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\TransmissionGUI.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "TransmissionGUI"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    SectionGetSize ${MainSection} $0
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
    ; Stop if the application is still running before removing its DLLs.
    ${If} ${FileExists} "$INSTDIR\TransmissionGUI.exe"
        ClearErrors
        Delete "$INSTDIR\TransmissionGUI.exe"
        ${If} ${Errors}
            MessageBox MB_ICONSTOP "$(CloseApp)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${EndIf}
    !include "${MANIFEST}"
    Delete "$SMPROGRAMS\TransmissionGUI.lnk"
    ReadRegStr $0 HKCU "${SHELL_APP_KEY}\shell\open\command" ""
    ${If} $0 == '"$INSTDIR\TransmissionGUI.exe" "%1"'
        DeleteRegKey HKCU "${SHELL_APP_KEY}"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ${EndIf}
    ; Remove the file and URL associations only when they still point to this copy.
    ReadRegStr $0 HKCU "${TORRENT_COMMAND}" ""
    ${If} $0 == '"$INSTDIR\TransmissionGUI.exe" -- "%1"'
        DeleteRegKey HKCU "Software\Classes\transmissionGUI.Torrent"
        DeleteRegValue HKCU "Software\Classes\.torrent\OpenWithProgids" "transmissionGUI.Torrent"
    ${EndIf}
    ReadRegStr $0 HKCU "${MAGNET_COMMAND}" ""
    ${If} $0 == '"$INSTDIR\TransmissionGUI.exe" -- "%1"'
        DeleteRegKey HKCU "Software\Classes\transmissionGUI.Magnet"
    ${EndIf}
    DeleteRegKey HKCU "Software\transmissionGUI\Capabilities"
    DeleteRegValue HKCU "Software\RegisteredApplications" "transmissionGUI"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
