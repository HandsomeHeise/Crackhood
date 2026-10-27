param([switch]$RemoveUserData)
$ErrorActionPreference='Stop'
$clsid='{5B4B37D2-ED9F-4A50-8A76-8DFE13DA7318}'
$approved='HKCU:\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
if(Test-Path -LiteralPath $approved){Remove-ItemProperty -LiteralPath $approved -Name $clsid -ErrorAction SilentlyContinue}
foreach($key in @("HKCU:\Software\Classes\CLSID\$clsid","HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\MyComputer\NameSpace\$clsid")) {
    if(Test-Path -LiteralPath $key) { Remove-Item -LiteralPath $key -Recurse -Force }
}
if($RemoveUserData) {
    $target=[IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Crackhood'))
    $expected=[IO.Path]::GetFullPath($env:LOCALAPPDATA).TrimEnd('\')+'\Crackhood'
    if($target -ne $expected) { throw 'Unexpected user data path' }
    if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
}
Write-Output 'Crackhood registration removed. Close its Explorer windows to release the DLL.'
if (-not ('Crackhood.ShellNotify' -as [type])) {
 Add-Type 'namespace Crackhood { public static class ShellNotify { [System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e, uint f, System.IntPtr a, System.IntPtr b); } }'
}
[Crackhood.ShellNotify]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero)
