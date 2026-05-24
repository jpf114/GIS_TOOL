!define PRODUCT_NAME "GIS TOOLKIT"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "GIS TOOL"
!define PRODUCT_DIR_REGKEY "Software\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "gis-toolkit-${PRODUCT_VERSION}-win64-setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir"
RequestExecutionLevel admin

Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  File /r "..\install"

  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\GIS TOOLKIT.lnk" "$INSTDIR\bin\gis-gui.exe"
  CreateShortCut "$DESKTOP\GIS TOOLKIT.lnk" "$INSTDIR\bin\gis-gui.exe"
SectionEnd

Section -Post
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir" "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" "$INSTDIR\uninst.exe"
SectionEnd

Section Uninstall
  Delete "$INSTDIR\uninst.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\${PRODUCT_NAME}\GIS TOOLKIT.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT_NAME}"
  Delete "$DESKTOP\GIS TOOLKIT.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
SectionEnd
