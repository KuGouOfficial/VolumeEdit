param([Parameter(Mandatory=$true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'
$taskBuildRoot = (Resolve-Path -LiteralPath $BuildDir).Path
$taskFixture = [IO.Path]::GetFullPath((Join-Path $taskBuildRoot ('self-delete-' + [guid]::NewGuid().ToString('N'))))
if (-not $taskFixture.StartsWith($taskBuildRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe fixture path' }
New-Item -ItemType Directory -Path $taskFixture | Out-Null
Copy-Item -LiteralPath (Join-Path $taskBuildRoot 'uninstall.exe') -Destination (Join-Path $taskFixture 'uninstall.exe')
[IO.File]::WriteAllText((Join-Path $taskFixture 'product.id'), "VolumeEdit.Portable.8A7C7476-55F3-4525-82E7-33B2FB781132")
$taskProcess = Start-Process -FilePath (Join-Path $taskFixture 'uninstall.exe') -ArgumentList '--self-delete-smoke' -PassThru -WindowStyle Hidden
if (-not $taskProcess.WaitForExit(20000)) { throw 'Self-delete test timed out' }
if ($taskProcess.ExitCode -ne 0) { throw "Self-delete test failed: $($taskProcess.ExitCode)" }
$taskDeadline=[DateTime]::UtcNow.AddSeconds(10)
while((Test-Path -LiteralPath $taskFixture) -and [DateTime]::UtcNow -lt $taskDeadline){Start-Sleep -Milliseconds 100}
if (Test-Path -LiteralPath $taskFixture) { throw 'Self-delete left files or folder behind' }
Write-Host 'Self-delete: EXE and folder removed after exit, without helper files, services or scheduled tasks.'
