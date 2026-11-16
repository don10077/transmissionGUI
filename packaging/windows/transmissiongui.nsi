Unicode true
!include "MUI2.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\transmissionGUI"
!define SHELL_APP_KEY "Software\Classes\Applications\transmissiongui.exe"
!define TORRENT_COMMAND "Software\Classes\transmissionGUI.Torrent\shell\open\command"
!define MAGNET_COMMAND "Software\Classes\transmissionGUI.Magnet\shell\open\command"

Name "transmissionGUI ${VERSION}"
OutFile "${OUTPUT}"
InstallDir "$LOCALAPPDATA\Programs\transmissionGUI"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=0 "ProductName" "transmissionGUI"
VIAddVersionKey /LANG=0 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=0 "FileDescription" "transmissionGUI Setup"
VIAddVersionKey /LANG=0 "LegalCopyright" "transmissionGUI contributors"

!define MUI_ICON "${__FILEDIR__}\..\..\app\icons\transmission-upstream.ico"
!define MUI_UNICON "${MUI_ICON}"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\transmissiongui.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Russian"

LangString Needs64Bit ${LANG_ENGLISH} "transmissionGUI requires 64-bit Windows."
LangString Needs64Bit ${LANG_RUSSIAN} "Для transmissionGUI нужна 64-разрядная Windows."
LangString CloseApp ${LANG_ENGLISH} "Close transmissionGUI and try again."
LangString CloseApp ${LANG_RUSSIAN} "Закройте transmissionGUI и повторите попытку."
LangString UpgradeFailed ${LANG_ENGLISH} "The existing installation could not be removed."
LangString UpgradeFailed ${LANG_RUSSIAN} "Не удалось удалить предыдущую установку."

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "$(Needs64Bit)" /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Section "transmissionGUI" MainSection
    ; Use the previous manifest on upgrade so renamed DLLs do not accumulate.
    ReadRegStr $0 HKCU "${UNINSTALL_KEY}" "InstallLocation"
    ${If} $0 != ""
    ${AndIf} $0 == $INSTDIR
    ${AndIf} ${FileExists} "$INSTDIR\uninstall.exe"
        ClearErrors
        ExecWait '"$INSTDIR\uninstall.exe" /S _?=$INSTDIR' $1
        ${If} ${Errors}
        ${OrIf} $1 != 0
            MessageBox MB_ICONSTOP "$(UpgradeFailed)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${PAYLOAD}\*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateShortcut "$SMPROGRAMS\transmissionGUI.lnk" "$INSTDIR\transmissiongui.exe"

    WriteRegStr HKCU "${SHELL_APP_KEY}" "FriendlyAppName" "transmissionGUI"
    WriteRegStr HKCU "${SHELL_APP_KEY}\SupportedTypes" ".torrent" ""
    WriteRegStr HKCU "${SHELL_APP_KEY}\shell\open\command" "" '"$INSTDIR\transmissiongui.exe" "%1"'
    ; Preserve enabled associations when moving from a portable or development copy.
    ReadRegStr $0 HKCU "${TORRENT_COMMAND}" ""
    ${If} $0 != ""
        WriteRegStr HKCU "${TORRENT_COMMAND}" "" '"$INSTDIR\transmissiongui.exe" -- "%1"'
    ${EndIf}
    ReadRegStr $0 HKCU "${MAGNET_COMMAND}" ""
    ${If} $0 != ""
        WriteRegStr HKCU "${MAGNET_COMMAND}" "" '"$INSTDIR\transmissiongui.exe" -- "%1"'
    ${EndIf}
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "transmissionGUI"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\transmissiongui.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "transmissionGUI"
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
    ${If} ${FileExists} "$INSTDIR\transmissiongui.exe"
        ClearErrors
        Delete "$INSTDIR\transmissiongui.exe"
        ${If} ${Errors}
            MessageBox MB_ICONSTOP "$(CloseApp)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${EndIf}
    !include "${MANIFEST}"
    Delete "$SMPROGRAMS\transmissionGUI.lnk"
    ReadRegStr $0 HKCU "${SHELL_APP_KEY}\shell\open\command" ""
    ${If} $0 == '"$INSTDIR\transmissiongui.exe" "%1"'
        DeleteRegKey HKCU "${SHELL_APP_KEY}"
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ${EndIf}
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
