; NSIS installer template for the ForkMesh desktop client (issue #370).
; Driven by tools/package/windows.sh, which passes:
;   /DFORKMESH_VERSION=X.Y.Z  /DFORKMESH_SRCEXE=<built exe>
;   /DFORKMESH_RESOURCES=<CMake-installed resources>
;   /DFORKMESH_OUTFILE=<setup exe>
; See /docs#installers-updates.

!ifndef FORKMESH_VERSION
  !define FORKMESH_VERSION "0.0.0"
!endif
!ifndef FORKMESH_SRCEXE
  !error "FORKMESH_SRCEXE (path to the built forkmesh.exe) must be defined"
!endif
!ifndef FORKMESH_OUTFILE
  !define FORKMESH_OUTFILE "forkmesh-windows-setup.exe"
!endif
!ifndef FORKMESH_RESOURCES
  !error "FORKMESH_RESOURCES (the CMake-installed resource tree) must be defined"
!endif

!include "MUI2.nsh"

Name "ForkMesh ${FORKMESH_VERSION}"
OutFile "${FORKMESH_OUTFILE}"
Unicode true
; Per-user install: no admin elevation, no UAC prompt — matches the download-and-run
; expectation and lets WinSparkle self-update without elevation.
InstallDir "$LOCALAPPDATA\ForkMesh"
RequestExecutionLevel user
SetCompressor /SOLID lzma

VIProductVersion "${FORKMESH_VERSION}.0"
VIAddVersionKey "ProductName"     "ForkMesh"
VIAddVersionKey "FileDescription" "ForkMesh desktop client"
VIAddVersionKey "FileVersion"     "${FORKMESH_VERSION}"
VIAddVersionKey "ProductVersion"  "${FORKMESH_VERSION}"
VIAddVersionKey "CompanyName"     "ForkMesh"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ForkMesh"

Section "ForkMesh" SecMain
  SetOutPath "$INSTDIR"
  File "/oname=forkmesh.exe" "${FORKMESH_SRCEXE}"
  SetOutPath "$INSTDIR\resources\forkmesh"
  File /r "${FORKMESH_RESOURCES}\*"
  SetOutPath "$INSTDIR"

  CreateShortCut "$SMPROGRAMS\ForkMesh.lnk" "$INSTDIR\forkmesh.exe"
  CreateShortCut "$DESKTOP\ForkMesh.lnk" "$INSTDIR\forkmesh.exe"

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr   HKCU "${REGKEY}" "DisplayName"     "ForkMesh"
  WriteRegStr   HKCU "${REGKEY}" "DisplayVersion"  "${FORKMESH_VERSION}"
  WriteRegStr   HKCU "${REGKEY}" "Publisher"       "ForkMesh"
  WriteRegStr   HKCU "${REGKEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr   HKCU "${REGKEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
  WriteRegStr HKCU "Software\Classes\forkmesh" "" "URL:ForkMesh local control link"
  WriteRegStr HKCU "Software\Classes\forkmesh" "URL Protocol" ""
  WriteRegStr HKCU "Software\Classes\forkmesh\DefaultIcon" "" "$INSTDIR\forkmesh.exe,0"
  WriteRegStr HKCU "Software\Classes\forkmesh\shell\open\command" "" "$\"$INSTDIR\forkmesh.exe$\" $\"%1$\""
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\forkmesh.exe"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\resources"
  Delete "$SMPROGRAMS\ForkMesh.lnk"
  Delete "$DESKTOP\ForkMesh.lnk"
  RMDir  "$INSTDIR"
  DeleteRegKey HKCU "${REGKEY}"
  DeleteRegKey HKCU "Software\Classes\forkmesh"
SectionEnd
