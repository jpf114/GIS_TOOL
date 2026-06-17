!define PRODUCT_NAME "GIS TOOLKIT"
!define PRODUCT_VERSION "1.0.0"
!define PRODUCT_PUBLISHER "GIS TOOL"
!define PRODUCT_DIR_REGKEY "Software\${PRODUCT_NAME}"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "gis-toolkit-${PRODUCT_VERSION}-win64-setup.exe"

; 允许用户选择安装路径
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"

; 检测旧版本安装路径
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir"

RequestExecutionLevel admin

; 引入现代 UI
!include "MUI2.nsh"

; 定义 MUI 页面
!define MUI_LICENSEPAGE_CHECKBOX
!define MUI_ABORTWARNING

; 页面顺序
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  File /r "..\install"

  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\GIS TOOLKIT.lnk" "$INSTDIR\bin\gis-gui.exe"
  CreateShortCut "$DESKTOP\GIS TOOLKIT.lnk" "$INSTDIR\bin\gis-gui.exe"
SectionEnd

Section -Post
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "Version" "${PRODUCT_VERSION}"
  WriteUninstaller "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" "$INSTDIR\uninst.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
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
