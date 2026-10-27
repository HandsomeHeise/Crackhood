$ErrorActionPreference='Stop'
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath}
Add-Type -AssemblyName System.Windows.Forms
if([System.Windows.Forms.MessageBox]::Show('Uninstall Crackhood? Your profiles and saved files will be kept.','Crackhood Uninstall','YesNo','Question') -ne 'Yes'){exit}
try{
 $destination=[IO.Path]::GetFullPath($PSScriptRoot)
 $root=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\Crackhood')).TrimEnd('\')+'\'
 if(-not $destination.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected installation directory.'}
 $current=(Get-ItemProperty 'HKCU:\Software\Classes\CLSID\{5B4B37D2-ED9F-4A50-8A76-8DFE13DA7318}\InprocServer32' -ErrorAction SilentlyContinue).'(default)'
 if($current -ne (Join-Path $destination 'CrackhoodShell.dll')){throw 'This uninstaller belongs to an older installation. Use Crackhood in Installed Apps instead.'}
 $processes=Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq (Join-Path $destination 'crackhood_broker.exe')}
 if($processes | Where-Object {$_.CommandLine -match '--worker|--shell-job'}){throw 'An Xbox operation is running. Wait for it to finish before uninstalling.'}
 $processes | ForEach-Object {Stop-Process -Id $_.ProcessId}
 & (Join-Path $destination 'uninstall.ps1') | Out-Null
 Remove-Item -LiteralPath 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Crackhood' -Recurse -Force
 $menu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Crackhood'
 foreach($name in @('Crackhood Settings.lnk','Uninstall Crackhood.lnk')){Remove-Item -LiteralPath (Join-Path $menu $name) -Force -ErrorAction SilentlyContinue}
 if(Test-Path -LiteralPath $menu){if(-not(Get-ChildItem -LiteralPath $menu)){Remove-Item -LiteralPath $menu}}
 $remaining=@()
 foreach($name in @('crackhood_broker.exe','crackhood_ui.exe','CrackhoodShell.dll','install.ps1','uninstall.ps1','remove.ps1')){try{Remove-Item -LiteralPath (Join-Path $destination $name) -Force}catch{$remaining+=$name}}
 if($remaining.Count){$message='Crackhood was unregistered. Windows is still using some installation files. Close its Explorer windows, then remove this folder: '+$destination}else{Remove-Item -LiteralPath $destination;$message='Crackhood was uninstalled. Your profiles and saved files were kept.'}
 [System.Windows.Forms.MessageBox]::Show($message,'Crackhood Uninstall','OK','Information') | Out-Null
}catch{[System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Crackhood Uninstall','OK','Error') | Out-Null}
