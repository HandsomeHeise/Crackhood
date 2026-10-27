param(
 [string]$Broker=(Join-Path $PSScriptRoot '..\out\build\x64\Release\crackhood_broker.exe'),
 [string]$Console='10.10.10.218'
)
$ErrorActionPreference='Stop'
if($Console -ne '10.10.10.218'){throw 'This acceptance script uses the configured rgh profile at 10.10.10.218.'}
$Broker=(Resolve-Path -LiteralPath $Broker).Path
$id=[guid]::NewGuid().ToString('N').Substring(0,12)
$remote="HDD:\CH-Test-$id"
if($remote -notmatch '^HDD:\\CH-Test-[a-f0-9]{12}$'){throw 'Unsafe test directory'}
$local=Join-Path (Split-Path $Broker) "live-$id"
New-Item -ItemType Directory -Path $local | Out-Null
function Rpc([string[]]$Arguments){ & $Broker --rpc @Arguments; if($LASTEXITCODE -ne 0){throw "Acceptance operation failed: $($Arguments[0])"} }
$marker=[guid]::NewGuid().ToString()
$markerFile=Join-Path $local 'owner.txt'
[IO.File]::WriteAllText($markerFile,$marker)
$owned=$false
try {
 Rpc @('mkdir',$remote)
 Rpc @('upload',"$remote\owner.txt",$markerFile)
 $owned=$true
 $source=Join-Path $local 'Prüfung.bin'
 $bytes=[byte[]]::new(1048576)
 $rng=[Security.Cryptography.RandomNumberGenerator]::Create();$rng.GetBytes($bytes);$rng.Dispose()
 [IO.File]::WriteAllBytes($source,$bytes)
 Rpc @('upload',"$remote\payload.bin",$source)
 $download=Join-Path $local 'download.bin'
 Rpc @('download',"$remote\payload.bin",$download)
 if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $download).Hash){throw 'Binary roundtrip mismatch'}
 $empty=Join-Path $local 'empty.bin';[IO.File]::WriteAllBytes($empty,[byte[]]::new(0))
 Rpc @('upload',"$remote\empty.bin",$empty)
 $emptyDownload=Join-Path $local 'empty-downloaded.bin';Rpc @('download',"$remote\empty.bin",$emptyDownload)
 if((Get-Item -LiteralPath $emptyDownload).Length -ne 0){throw 'Empty file roundtrip mismatch'}
 Rpc @('rename',"$remote\payload.bin","$remote\renamed.bin")
 Rpc @('job_move',"$remote\renamed.bin","$remote\moved.bin")
 $tree=Join-Path $local 'tree';New-Item -ItemType Directory -Path (Join-Path $tree 'nested') | Out-Null
 [IO.File]::WriteAllText((Join-Path $tree 'nested\text.txt'),'Crackhood recursive roundtrip')
 Rpc @('upload',"$remote\tree",$tree)
 $treeDownload=Join-Path $local 'tree-downloaded';Rpc @('download',"$remote\tree",$treeDownload)
 if((Get-FileHash -LiteralPath (Join-Path $tree 'nested\text.txt')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $treeDownload 'nested\text.txt')).Hash){throw 'Recursive roundtrip mismatch'}
 Rpc @('delete',"$remote\moved.bin")
 Write-Output 'PASS: SHA256 binary, empty file, Unicode local path, recursive folders, rename, move and delete.'
} finally {
 if($owned){
  $check=Join-Path $local 'owner-confirmation.txt'
  & $Broker --rpc download "$remote\owner.txt" $check
  if($LASTEXITCODE -eq 0 -and [IO.File]::ReadAllText($check) -ceq $marker){Rpc @('delete',$remote)}
  else {Write-Warning "Ownership not confirmed; test directory retained: $remote"}
 }
}
