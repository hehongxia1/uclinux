; $Id$
;
; NSIS Installation script for Snort 2.6.1 Win32
; Written by Chris Reid <chris.reid@codecraftconsultants.com>
; Updated by Steven Sturges <ssturges@sourcefire.com>
;
; This script will create a Win32 installer for Snort 2.6.1 (Win32 only).
; For more information about NSIS, see their homepage:
;     http://nsis.sourceforge.net/
;
; Note that this NSIS script is designed for NSIS version 2.09.
;

Name "Snort 2.6.1.1"

CRCCheck On

!include "MUI.nsh"

!define TEMP $R0

;--------------------------------
;Configuration

  ;General
  OutFile "Snort_261_Installer.exe"  ; The name of the installer executable

  ;Folder selection page
  InstallDir "C:\Snort"


;--------------------------------
;Modern UI Configuration

  !define MUI_CUSTOMPAGECOMMANDS

  !define MUI_LICENSEPAGE
  !define MUI_COMPONENTSPAGE
  !define MUI_DIRECTORYPAGE
  
  !define MUI_ABORTWARNING
  
  !define MUI_UNINSTALLER
  !define MUI_UNCONFIRMPAGE
  
;--------------------------------
;Languages
 
  !insertmacro MUI_LANGUAGE "English"
  
;--------------------------------
;Language Strings

  ;Description
  LangString DESC_Snort   ${LANG_ENGLISH} "Install snort, configuration files, and rules."
  LangString DESC_Dynamic ${LANG_ENGLISH} "Install dynamic preprocessor and dynamic engine modules."
  LangString DESC_Doc     ${LANG_ENGLISH} "Install snort documentation."
  LangString DESC_Schemas ${LANG_ENGLISH} "Copy database schemas."
  
  ;Header
  LangString TEXT_IO_TITLE    ${LANG_ENGLISH} "Installation Options"
  LangString TEXT_IO_SUBTITLE ${LANG_ENGLISH} "Select which configuration options you want installed"
  
  ;Window titles
  LangString TEXT_IO_PAGETITLE_OPTIONS ${LANG_ENGLISH} ": Installation Options"

;--------------------------------
;Data
  
  LicenseData "..\..\..\LICENSE"

;--------------------------------
;Pages
  
  !insertmacro MUI_PAGE_LICENSE "..\..\..\LICENSE"
  Page custom fnSelectCustomOptions
  Page custom fnSetHeaderText
  !insertmacro MUI_PAGE_COMPONENTS
  Page custom fnSetHeaderText
  !insertmacro MUI_PAGE_DIRECTORY
  Page custom fnSetHeaderText
  !insertmacro MUI_PAGE_INSTFILES

  ; Call .onDirectoryLeave whenever user leaves
  ; the directory selection page
  ;!define MUI_CUSTOMFUNCTION_DIRECTORY_LEAVE  onDirectoryLeave

;--------------------------------
;Reserve Files
  
  ;Things that need to be extracted on first (keep these lines before any File command!)
  ;Only useful for BZIP2 compression
  
  ReserveFile "snort_installer_options.ini"
  !insertmacro MUI_RESERVEFILE_INSTALLOPTIONS

;--------------------------------
; Event Handlers

Function .onInstSuccess
  StrCpy $0 "Snort has successfully been installed.$\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0Snort also requires WinPcap 3.1 to be installed on this machine.$\r$\n"
  StrCpy $0 "$0WinPcap can be downloaded from:$\r$\n"
  StrCpy $0 "$0    http://www.winpcap.org/ $\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0It would also be wise to tighten the security on the Snort installation$\r$\n"
  StrCpy $0 "$0directory to prevent any malicious modification of the Snort executable.$\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0$\r$\n"
  StrCpy $0 "$0Next, you must manually edit the 'snort.conf' file to$\r$\n"
  StrCpy $0 "$0specify proper paths to allow Snort to find the rules files$\r$\n"
  StrCpy $0 "$0and classification files."
  MessageBox MB_OK $0
FunctionEnd


;--------------------------------
;Installer Sections

Section "Snort" Snort
  ; --------------------------------------------------------------------
  ; NOTE: The installer, as delivered here, will only allow the user
  ;       to install configurations which can optionally be run as a
  ;       Windows Service.
  ; --------------------------------------------------------------------

  ; Search for a space embedded within $INSTDIR
  StrCpy $R4 0  ; index within $INSTDIR
  searching_for_space:
    StrCpy $R5 $INSTDIR 1 $R4  ; copy 1 char from $INSTDIR[$R4] into $R5
    StrCmp $R5 " " found_space
    StrCmp $R5 "" done_searching_for_space
    IntOp $R4 $R4 + 1  ; increment index
    Goto searching_for_space
  found_space:
    StrCpy $0 "The installation directory appears to contain an$\r$\n"
    StrCpy $0 "$0embedded space character.  You need to be aware that$\r$\n"
    StrCpy $0 "$0because of this, all paths specified on the command-line$\r$\n"
    StrCpy $0 "$0and in the 'snort.conf' file must be enclosed within$\r$\n"
    StrCpy $0 "$0double-quotes.$\r$\n"
    MessageBox MB_OK $0
  done_searching_for_space:


  CreateDirectory "$INSTDIR"

  CreateDirectory "$INSTDIR\bin"
  SetOutPath "$INSTDIR\bin"
  File ".\LibnetNT.dll"
  File ".\pcre.dll"
  
  CreateDirectory "$INSTDIR\etc"
  SetOutPath "$INSTDIR\etc"
  File "..\..\..\etc\*.conf"
  File "..\..\..\etc\*.config"
  File "..\..\..\etc\*.map"

  CreateDirectory "$INSTDIR\rules"
  SetOutPath "$INSTDIR\rules"
  ;Rules are no longer part of the distribution
  ;File /r "..\..\..\rules\*.rules"

  CreateDirectory "$INSTDIR\log"

  ;--------------------------
  ;Read the Checkbox values from the snort_installer_options INI File

  ; $0 - will be set to one of:  "MySQL", "MSSQL" or "Oracle"

  StrCpy $0 "MySQL"

  ; CheckForMSSqlServer:
  !insertmacro MUI_INSTALLOPTIONS_READ ${TEMP} "snort_installer_options.ini" "Field 3" "State"
  StrCmp ${TEMP} "1" "" +2
    StrCpy $0 "MSSQL"

  ; CheckForFlexResp:
  !insertmacro MUI_INSTALLOPTIONS_READ ${TEMP} "snort_installer_options.ini" "Field 4" "State"
  StrCmp ${TEMP} "1" "" +2
    StrCpy $0 "Oracle"

  SetOutPath "$INSTDIR\bin"

  ; --------------------------------------------------------------------
  ; Configurations
  ; --------------------------------------------------------------------
  StrCmp "$0" "MySQL" "" +2
    File ".\snort___Win32_MySQL_Release\snort.exe"

  StrCmp "$0" "MSSQL" "" +2
    File ".\snort___Win32_SQLServer_Release\snort.exe"

  StrCmp "$0" "Oracle" "" +2
    File ".\snort___Win32_Oracle_Release\snort.exe"

  ;Create uninstaller
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Dynamic Modules" Dynamic
  CreateDirectory "$INSTDIR\lib"
  CreateDirectory "$INSTDIR\lib\snort_dynamicpreprocessor"
  SetOutPath "$INSTDIR\lib\snort_dynamicpreprocessor"
  File "..\..\dynamic-preprocessors\ftptelnet\Release\sf_ftptelnet.dll"
  File "..\..\dynamic-preprocessors\smtp\Release\sf_smtp.dll"
  File "..\..\dynamic-preprocessors\ssh\Release\sf_ssh.dll"
  File "..\..\dynamic-preprocessors\dcerpc\Release\sf_dcerpc.dll"
  File "..\..\dynamic-preprocessors\dns\Release\sf_dns.dll"

  CreateDirectory "$INSTDIR\lib\snort_dynamicengine"
  SetOutPath "$INSTDIR\lib\snort_dynamicengine"
  File ".\SF_Engine_Release\sf_engine.dll"
SectionEnd

Section "Documentation" Doc
  CreateDirectory "$INSTDIR\doc"
  SetOutPath "$INSTDIR\doc"
  File "..\..\..\ChangeLog"
  File "..\..\..\LICENSE"
  File "..\..\..\RELEASE.NOTES"
  File "..\..\..\doc\*.*"
  Delete "$INSTDIR\doc\.cvsignore"

  CreateDirectory "$INSTDIR\doc\signatures"
  SetOutPath "$INSTDIR\doc\signatures"
  ;Rules are no longer part of the distribution
  ;File "..\..\..\doc\signatures\*.*"

  CreateDirectory "$INSTDIR\contrib"
  SetOutPath "$INSTDIR\contrib"
  File "..\..\..\contrib\*.*"
  Delete "$INSTDIR\contrib\.cvsignore"
SectionEnd

Section "Schemas" Schemas
  CreateDirectory "$INSTDIR\schemas"
  SetOutPath "$INSTDIR\schemas"
  File "..\..\..\schemas\*.*"
  Delete "$INSTDIR\schemas\Makefile.am"
SectionEnd

;Display the Finish header
;Insert this macro after the sections if you are not using a finish page
;!insertmacro MUI_SECTIONS_FINISHHEADER


;--------------------------------
;Descriptions

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${Snort}   $(DESC_Snort)
  !insertmacro MUI_DESCRIPTION_TEXT ${Dynamic} $(DESC_Dynamic)
  !insertmacro MUI_DESCRIPTION_TEXT ${Doc}     $(DESC_Doc)
  !insertmacro MUI_DESCRIPTION_TEXT ${Schemas} $(DESC_Schemas)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
;Installer Functions

Function .onInit
  ;Extract InstallOptions INI Files
  !insertmacro MUI_INSTALLOPTIONS_EXTRACT "snort_installer_options.ini"
FunctionEnd

Function fnSelectCustomOptions
  !insertmacro MUI_HEADER_TEXT "$(TEXT_IO_TITLE)" "$(TEXT_IO_SUBTITLE)"
  !insertmacro MUI_INSTALLOPTIONS_DISPLAY "snort_installer_options.ini"
FunctionEnd


Function fnSetHeaderText
  !insertmacro MUI_HEADER_TEXT "$(TEXT_IO_PAGETITLE_OPTIONS)" ""
FunctionEnd

;--------------------------------
;Uninstaller Section

Section "Uninstall"

  ; If Snort appears to already be installed as a Windows Service,
  ; then ask the user if the uninstall should unregister the
  ; Service.

  ReadRegStr $1 HKLM "Software\Snort" "CmdLineParamCount"
  StrCmp $1 "" service_not_registered
    MessageBox MB_YESNO "It appears that Snort is registered as a Windows Service.  Should it be unregistered now?" IDNO finished_unregistering_service
    ExecWait "net stop snortsvc"
    ExecWait "$INSTDIR\bin\snort.exe /SERVICE /UNINSTALL"
    GoTo finished_unregistering_service

  service_not_registered:
    MessageBox MB_OK "Snort not installed as a service"
   
  finished_unregistering_service:
    RMDir /r "$INSTDIR"
    ;!insertmacro MUI_UNFINISHHEADER

SectionEnd
