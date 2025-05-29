; Basic information
Name "CADventory"
OutFile "CADventory-Installer.exe"
InstallDir "$PROGRAMFILES64\CADventory"
RequestExecutionLevel admin

; Pages
Page directory
Page instfiles

; Sections
Section "MainSection" SEC01
  SetOutPath "$INSTDIR"
  
  ; Copy main executable and DLLs
  File "build\Release\cadventory.exe"
  File "build\Release\Qt6Core.dll"
  File "build\Release\Qt6Gui.dll"
  File "build\Release\Qt6Widgets.dll"
  
  ; Copy imageformats directory
  SetOutPath "$INSTDIR\imageformats"
  File "build\Release\imageformats\*.dll"
  
  ; Copy platforms directory
  SetOutPath "$INSTDIR\platforms"
  File "build\Release\platforms\*.dll"
  
  ; Create ollama directory and copy ollama.exe - SIMPLIFIED APPROACH
  SetOutPath "$INSTDIR\ollama"
  File /nonfatal "third_party\ollama\windows\ollama.exe"
  SetOutPath "$INSTDIR"
  
  ; Create shortcuts
  CreateDirectory "$SMPROGRAMS\CADventory"
  CreateShortcut "$SMPROGRAMS\CADventory\CADventory.lnk" "$INSTDIR\cadventory.exe"
  CreateShortcut "$DESKTOP\CADventory.lnk" "$INSTDIR\cadventory.exe"
  
  ; Write registry
  WriteRegStr HKCU "Software\CADventory" "" $INSTDIR
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CADventory" "DisplayName" "CADventory"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CADventory" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CADventory" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CADventory" "NoRepair" 1
  
  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "Uninstall"
  ; Remove installed files
  Delete "$INSTDIR\cadventory.exe"
  Delete "$INSTDIR\Qt6Core.dll"
  Delete "$INSTDIR\Qt6Gui.dll"
  Delete "$INSTDIR\Qt6Widgets.dll"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\ollama"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\platforms"
  
  ; Remove shortcuts
  Delete "$SMPROGRAMS\CADventory\CADventory.lnk"
  Delete "$DESKTOP\CADventory.lnk"
  RMDir "$SMPROGRAMS\CADventory"
  
  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CADventory"
  DeleteRegKey HKCU "Software\CADventory"
  
  ; Remove installation directory if empty
  RMDir "$INSTDIR"
SectionEnd