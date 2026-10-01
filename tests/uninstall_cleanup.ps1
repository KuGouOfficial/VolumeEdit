param([Parameter(Mandatory=$true)][string]$BuildDir, [Parameter(Mandatory=$true)][string]$ProgramDir)
$ErrorActionPreference='Stop'
$taskBuildRoot=(Resolve-Path -LiteralPath $BuildDir).Path
$taskProgramRoot=(Resolve-Path -LiteralPath $ProgramDir).Path
$taskFixture=[IO.Path]::GetFullPath((Join-Path $taskBuildRoot ('uninstall-test-'+[guid]::NewGuid().ToString('N'))))
if(-not $taskFixture.StartsWith($taskBuildRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe fixture path'}
New-Item -ItemType Directory -Path $taskFixture | Out-Null
foreach($taskName in @('VolumeEdit.exe','uninstall.exe')){Copy-Item -LiteralPath (Join-Path $taskProgramRoot $taskName) -Destination (Join-Path $taskFixture $taskName)}
[IO.File]::WriteAllText((Join-Path $taskFixture 'product.id'),'VolumeEdit.Portable.8A7C7476-55F3-4525-82E7-33B2FB781132')
foreach($taskName in @('README.md','DESIGN.md','BUILD_STATUS.md','LICENSE','SHA256SUMS.txt','my-notes.txt')){[IO.File]::WriteAllText((Join-Path $taskFixture $taskName),'fixture')}
$taskProcess=Start-Process -FilePath (Join-Path $taskFixture 'uninstall.exe') -ArgumentList '--uninstall-smoke' -PassThru -WindowStyle Hidden
if(-not $taskProcess.WaitForExit(20000)){throw 'GUI cleanup timed out'}
if($taskProcess.ExitCode -ne 0){throw 'GUI cleanup failed'}
$taskDeadline=[DateTime]::UtcNow.AddSeconds(10)
while((Test-Path -LiteralPath (Join-Path $taskFixture 'product.id')) -and [DateTime]::UtcNow -lt $taskDeadline){Start-Sleep -Milliseconds 100}
$taskRemaining=@(Get-ChildItem -LiteralPath $taskFixture -Force)
if($taskRemaining.Count -ne 1 -or $taskRemaining[0].Name -ne 'my-notes.txt'){throw 'Owned product files left behind or user file lost'}
Remove-Item -LiteralPath (Join-Path $taskFixture 'my-notes.txt')
Remove-Item -LiteralPath $taskFixture
Write-Host 'GUI uninstall: full product cleanup, user file preserved, no real audio or startup mutation.'
