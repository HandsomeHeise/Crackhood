param([ValidateSet('Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$stage=Join-Path $repo 'out\installer\payload';New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach($name in @('CrackhoodShell.dll','crackhood_broker.exe','crackhood_ui.exe')){Copy-Item -LiteralPath (Join-Path $repo "out\build\x64\$Configuration\$name") -Destination (Join-Path $stage $name)}
foreach($name in @('install.ps1','uninstall.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $stage $name)}
foreach($name in @('setup.ps1','remove.ps1')){Copy-Item -LiteralPath (Join-Path $repo "installer\$name") -Destination (Join-Path $stage $name)}
foreach($file in (Get-ChildItem -LiteralPath $stage -Filter '*.ps1')){$text=[IO.File]::ReadAllText($file.FullName);[IO.File]::WriteAllText($file.FullName,$text,(New-Object Text.UTF8Encoding($true)))}
& (Join-Path $stage 'setup.ps1') -VerifyPackage
$output=Join-Path $repo 'out\installer\Crackhood-Setup-0.1.0.exe'
$scratch=Join-Path $env:TEMP ('CrackhoodBuild-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
Copy-Item -LiteralPath $stage -Destination (Join-Path $scratch 'payload') -Recurse
$stage=Join-Path $scratch 'payload'
$temporaryOutput=Join-Path $scratch 'Crackhood-Setup.exe'
$files=Get-ChildItem -LiteralPath $stage -File | Sort-Object Name
$sed=@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$temporaryOutput
FriendlyName=Crackhood Setup
AppLaunched=powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File setup.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
SourceFiles=SourceFiles
[Strings]
"@
$index=0;foreach($file in $files){$sed+="`r`nFILE$index=`"$($file.Name)`"";$index++}
$sed+="`r`n[SourceFiles]`r`nSourceFiles0=$stage\`r`n[SourceFiles0]"
for($index=0;$index -lt $files.Count;$index++){$sed+="`r`n%FILE$index%="}
$sed=$sed -replace '\r?\n',"`r`n"
$sedPath=Join-Path $scratch 'Crackhood.sed';[IO.File]::WriteAllText($sedPath,$sed,[Text.Encoding]::Default)
$build=Start-Process -FilePath (Join-Path $env:WINDIR 'System32\iexpress.exe') -ArgumentList @('/N','/Q','Crackhood.sed') -WorkingDirectory $scratch -WindowStyle Hidden -Wait -PassThru
if($build.ExitCode -ne 0 -or -not(Test-Path -LiteralPath $temporaryOutput)){throw "Installer packaging failed (exit $($build.ExitCode)). Build files: $scratch"}
Copy-Item -LiteralPath $temporaryOutput -Destination $output
$sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($output))).Replace('-','')}finally{$sha.Dispose()}
[IO.File]::WriteAllText(($output+'.sha256'),$hash+'  '+[IO.Path]::GetFileName($output))
Get-Item -LiteralPath $output | Select-Object FullName,Length
