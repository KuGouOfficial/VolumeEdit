param([ValidatePattern('^VolumeEdit-portable(?:-[0-9]+\.[0-9]+\.[0-9]+)?$')][string]$OutputName='VolumeEdit-portable-0.3.0')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOut = [IO.Path]::GetFullPath((Join-Path $taskRoot ('out\'+$OutputName)))
$taskBuild = Join-Path $taskRoot 'build'
$taskFiles = @('VolumeEdit.exe','uninstall.exe','product.id','README.md','DESIGN.md','BUILD_STATUS.md','LICENSE')
if(-not $taskOut.StartsWith($taskRoot + '\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output escaped workspace'}
if(Test-Path -LiteralPath $taskOut){
    foreach($taskItem in Get-ChildItem -LiteralPath $taskOut -Force){
        if($taskItem.PSIsContainer -or ($taskItem.Name -notin ($taskFiles + 'SHA256SUMS.txt'))){throw 'Release folder contains runtime state or unknown files; packaging stopped.'}
    }
}
& (Join-Path $PSScriptRoot 'build.ps1')
New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
foreach($taskName in $taskFiles){
    $taskSource=if($taskName.EndsWith('.exe')){Join-Path $taskBuild $taskName}else{Join-Path $taskRoot $taskName}
    Copy-Item -LiteralPath $taskSource -Destination (Join-Path $taskOut $taskName) -Force
}
$taskFiles | ForEach-Object {
    $taskHash=Get-FileHash -LiteralPath (Join-Path $taskOut $_) -Algorithm SHA256
    '{0}  {1}' -f $taskHash.Hash,$_
} | Set-Content -LiteralPath (Join-Path $taskOut 'SHA256SUMS.txt') -Encoding UTF8
$taskZip=Join-Path $taskRoot ('out\'+$OutputName+'-x64.zip')
Compress-Archive -LiteralPath $taskOut -DestinationPath $taskZip -Force
Get-FileHash -LiteralPath $taskZip -Algorithm SHA256 | ForEach-Object { '{0}  {1}' -f $_.Hash,(Split-Path -Leaf $_.Path) } |
    Set-Content -LiteralPath (Join-Path $taskRoot 'out\SHA256SUMS.txt') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $taskRoot 'README.md') -Destination (Join-Path $taskRoot 'out\README.md') -Force
Write-Host "Portable release: $taskZip"
