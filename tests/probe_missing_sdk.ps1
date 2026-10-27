param([Parameter(Mandatory=$true)][string]$Probe)
$result = & $Probe --sdk 'Z:\Crackhood-Missing-SDK' --console 10.10.10.218 --read-only 2>&1
if ($LASTEXITCODE -eq 0) { throw 'Missing SDK must fail before any connection is attempted' }
if (($result -join "`n") -notmatch 'Xbox SDK nicht gefunden') { throw "Unexpected diagnostic: $result" }
