param([string]$InstallDirectory = (Join-Path $PSScriptRoot '..\out\build\x64\Release'))
$ErrorActionPreference='Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Installation requires x64 PowerShell' }
$resolved=(Resolve-Path -LiteralPath $InstallDirectory).Path
$dll=Join-Path $resolved 'CrackhoodShell.dll'
foreach($name in @('CrackhoodShell.dll','crackhood_broker.exe','crackhood_ui.exe')) {
 $file=Join-Path $resolved $name
 if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Installation file missing: $name"}
 $bytes=[IO.File]::ReadAllBytes($file)
 if($bytes.Length -lt 64){throw "Invalid PE: $name"}
 $pe=[BitConverter]::ToInt32($bytes,60)
 if($pe -lt 0 -or $pe+6 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664){throw "Expected x64 PE: $name"}
}
if (-not (Test-Path -LiteralPath (Join-Path $env:XEDK 'bin\x64\xbdm.dll'))) { throw 'Xbox SDK not found' }
$clsid='{5B4B37D2-ED9F-4A50-8A76-8DFE13DA7318}'
$root="HKCU:\Software\Classes\CLSID\$clsid"
New-Item -Path "$root\InprocServer32" -Force | Out-Null
Set-Item -LiteralPath $root -Value 'Crackhood Xbox 360'
Set-Item -LiteralPath "$root\InprocServer32" -Value $dll
New-ItemProperty -LiteralPath "$root\InprocServer32" -Name ThreadingModel -Value Apartment -Force | Out-Null
New-Item -Path "$root\DefaultIcon" -Force | Out-Null
Set-Item -LiteralPath "$root\DefaultIcon" -Value "$resolved\crackhood_ui.exe,0"
New-Item -Path "$root\ShellFolder" -Force | Out-Null
New-ItemProperty -LiteralPath "$root\ShellFolder" -Name Attributes -PropertyType DWord -Value ([uint32]0xA0000000L) -Force | Out-Null
New-ItemProperty -LiteralPath $root -Name DescriptionID -PropertyType DWord -Value 9 -Force | Out-Null
New-Item -Path "$root\shell\open\command" -Force | Out-Null
Set-Item -LiteralPath "$root\shell" -Value 'open'
Set-Item -LiteralPath "$root\shell\open\command" -Value (Join-Path $env:WINDIR 'explorer.exe')
New-ItemProperty -LiteralPath "$root\shell\open\command" -Name DelegateExecute -Value '{11dbb47c-a525-400b-9e80-a54615a090c0}' -Force | Out-Null
New-ItemProperty -LiteralPath $root -Name 'System.IsPinnedToNameSpaceTree' -PropertyType DWord -Value 1 -Force | Out-Null
$namespace="HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\NameSpace\$clsid"
New-Item -Path $namespace -Force | Out-Null
Set-Item -LiteralPath $namespace -Value 'Crackhood Xbox 360'
$approved='HKCU:\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
if(-not(Test-Path -LiteralPath $approved)){New-Item -Path $approved | Out-Null}
New-ItemProperty -LiteralPath $approved -Name $clsid -Value 'Crackhood Xbox 360' -Force | Out-Null
if (-not ('Crackhood.ShellNotify' -as [type])) {
    Add-Type 'namespace Crackhood { public static class ShellNotify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, System.IntPtr a, System.IntPtr b); } }'
}
[Crackhood.ShellNotify]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
if (-not ('Crackhood.JunctionNotify' -as [type])) {
 Add-Type 'namespace Crackhood { public static class JunctionNotify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern int SHGetSpecialFolderLocation(System.IntPtr hwnd, int id, out System.IntPtr pidl); [System.Runtime.InteropServices.DllImport("ole32.dll")] public static extern void CoTaskMemFree(System.IntPtr p); } }'
}
$pidl=[IntPtr]::Zero
if([Crackhood.JunctionNotify]::SHGetSpecialFolderLocation([IntPtr]::Zero,17,[ref]$pidl) -eq 0) {
 [Crackhood.ShellNotify]::SHChangeNotify(0x1000,0x1000,$pidl,[IntPtr]::Zero)
 [Crackhood.JunctionNotify]::CoTaskMemFree($pidl)
}
$shell=New-Object -ComObject Shell.Application
foreach($window in @($shell.Windows())) {
    try { if($window.Document.Folder.Self.Path -eq '::{20D04FE0-3AEA-1069-A2D8-08002B30309D}') { $window.Refresh() } } catch { }
}
Write-Output 'Crackhood registered for this Windows user. Open a new Explorer window.'
