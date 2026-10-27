param([switch]$VerifyPackage,[switch]$VerifyLayout)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSEdition -eq 'Desktop'){$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath}
$payload=@('CrackhoodShell.dll','crackhood_broker.exe','crackhood_ui.exe','install.ps1','uninstall.ps1','remove.ps1')
foreach($name in $payload){if(-not(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name))){throw "Missing setup payload: $name"}}
foreach($name in $payload[0..2]){$data=[IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name));$pe=[BitConverter]::ToInt32($data,60);if([BitConverter]::ToUInt32($data,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($data,$pe+4) -ne 0x8664){throw "Invalid x64 application: $name"}}
if($VerifyPackage){Write-Output 'PASS: installer payload contains all scripts and three x64 application binaries';exit 0}
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$form=New-Object System.Windows.Forms.Form
$form.Text='Crackhood Setup';$form.ClientSize=New-Object System.Drawing.Size(560,310);$form.StartPosition='CenterScreen';$form.FormBorderStyle='FixedDialog';$form.MaximizeBox=$false;$form.Font=New-Object System.Drawing.Font('Segoe UI',10)
$form.Icon=[System.Drawing.Icon]::ExtractAssociatedIcon((Join-Path $PSScriptRoot 'crackhood_ui.exe'))
$heading=New-Object System.Windows.Forms.Label;$heading.Text='Install Crackhood';$heading.Font=New-Object System.Drawing.Font('Segoe UI',20);$heading.SetBounds(24,20,512,44);$form.Controls.Add($heading)
$detail=New-Object System.Windows.Forms.Label;$detail.Text="Xbox 360 Neighborhood, reimagined.`r`nInstall for your Windows account. No administrator rights required.";$detail.SetBounds(26,76,508,54);$form.Controls.Add($detail)
$sdkLabel=New-Object System.Windows.Forms.Label;$sdkLabel.Text='Xbox 360 SDK folder';$sdkLabel.SetBounds(26,143,400,24);$form.Controls.Add($sdkLabel)
$sdk=New-Object System.Windows.Forms.TextBox;$sdk.SetBounds(26,171,400,28);$sdk.Text=$env:XEDK
if(-not $sdk.Text){$sdk.Text=[Environment]::GetEnvironmentVariable('XEDK','User')};if(-not $sdk.Text){$sdk.Text='C:\Program Files (x86)\Microsoft Xbox 360 SDK'};$form.Controls.Add($sdk)
$browse=New-Object System.Windows.Forms.Button;$browse.Text='Browse…';$browse.SetBounds(436,169,98,32);$form.Controls.Add($browse)
$browse.Add_Click({$picker=New-Object System.Windows.Forms.FolderBrowserDialog;$picker.Description='Select the Xbox 360 SDK folder';if($picker.ShowDialog() -eq 'OK'){$sdk.Text=$picker.SelectedPath};$picker.Dispose()})
$status=New-Object System.Windows.Forms.Label;$status.Text='Profiles and Xbox files are preserved.';$status.SetBounds(26,216,508,30);$form.Controls.Add($status)
$install=New-Object System.Windows.Forms.Button;$install.Text='Install';$install.SetBounds(320,263,104,32);$form.Controls.Add($install);$form.AcceptButton=$install
$cancel=New-Object System.Windows.Forms.Button;$cancel.Text='Cancel';$cancel.SetBounds(436,263,98,32);$cancel.Add_Click({$form.Close()});$form.Controls.Add($cancel)
$install.Add_Click({
 try{
  if($install.Text -eq 'Close'){$form.Close();return}
  if(-not(Test-Path -LiteralPath (Join-Path $sdk.Text 'bin\x64\xbdm.dll'))){throw 'Select a valid Xbox 360 SDK folder containing bin\x64\xbdm.dll.'}
  $active=Get-CimInstance Win32_Process | Where-Object {$_.Name -like 'crackhood*' -and ($_.CommandLine -match '--worker|--shell-job|--request')};if($active){throw 'An Xbox operation is running. Wait for it to finish before installing.'}
  $install.Enabled=$false;$status.Text='Installing Crackhood…';$form.Refresh()
  $destination=Join-Path $env:LOCALAPPDATA ('Programs\Crackhood\0.1.0-setup-'+[guid]::NewGuid().ToString('N').Substring(0,8))
  New-Item -ItemType Directory -Path $destination | Out-Null
  foreach($name in $payload){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $destination $name)}
  $env:XEDK=$sdk.Text;[Environment]::SetEnvironmentVariable('XEDK',$sdk.Text,'User')
  $oldDll=(Get-ItemProperty 'HKCU:\Software\Classes\CLSID\{5B4B37D2-ED9F-4A50-8A76-8DFE13DA7318}\InprocServer32' -ErrorAction SilentlyContinue).'(default)'
  if($oldDll){$oldBroker=Join-Path (Split-Path $oldDll) 'crackhood_broker.exe';Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq $oldBroker -and $_.CommandLine -notmatch '--'} | ForEach-Object {Stop-Process -Id $_.ProcessId}}
  & (Join-Path $destination 'install.ps1') -InstallDirectory $destination | Out-Null
  $uninstallKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Crackhood'
  if(-not(Test-Path $uninstallKey)){New-Item -Path $uninstallKey | Out-Null}
  $powershell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
  $uninstall='"'+$powershell+'" -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "'+(Join-Path $destination 'remove.ps1')+'"'
  foreach($value in @{DisplayName='Crackhood';DisplayVersion='0.1.0 Preview';Publisher='Crackhood';InstallLocation=$destination;DisplayIcon=(Join-Path $destination 'crackhood_ui.exe')+',0';UninstallString=$uninstall}.GetEnumerator()){New-ItemProperty -Path $uninstallKey -Name $value.Key -Value $value.Value -PropertyType String -Force | Out-Null}
  foreach($value in @('NoModify','NoRepair')){New-ItemProperty -Path $uninstallKey -Name $value -Value 1 -PropertyType DWord -Force | Out-Null}
  $menu=Join-Path ([Environment]::GetFolderPath('Programs')) 'Crackhood';New-Item -ItemType Directory -Path $menu -Force | Out-Null
  $wsh=New-Object -ComObject WScript.Shell;$shortcut=$wsh.CreateShortcut((Join-Path $menu 'Crackhood Settings.lnk'));$shortcut.TargetPath=Join-Path $destination 'crackhood_ui.exe';$shortcut.Arguments='--settings';$shortcut.IconLocation=(Join-Path $destination 'crackhood_ui.exe')+',0';$shortcut.Save()
  $shortcut=$wsh.CreateShortcut((Join-Path $menu 'Uninstall Crackhood.lnk'));$shortcut.TargetPath=$powershell;$shortcut.Arguments=$uninstall.Substring($powershell.Length+3);$shortcut.WindowStyle=7;$shortcut.Save()
  $heading.Text='Crackhood installed';$detail.Text="Open This PC and select Crackhood in the navigation pane.`r`nRestart Explorer or sign out and back in to activate this version.";$status.Text='Setup does not close Explorer or delete your profiles.';$install.Text='Close';$cancel.Visible=$false;$browse.Enabled=$false;$sdk.Enabled=$false
 }catch{[System.Windows.Forms.MessageBox]::Show($_.Exception.Message,'Crackhood Setup','OK','Error') | Out-Null}
 finally{$install.Enabled=$true}
})
if($VerifyLayout){foreach($control in $form.Controls){if($control.Right -gt $form.ClientSize.Width -or $control.Bottom -gt $form.ClientSize.Height){throw 'Setup control exceeds the window bounds'}};$form.Dispose();Write-Output 'PASS: setup form creates all controls within its client area';exit 0}
[void]$form.ShowDialog();$form.Dispose()
