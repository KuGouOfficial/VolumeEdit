param([switch]$SkipTests, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
. (Join-Path $PSScriptRoot 'project.ps1')
Initialize-Output
$taskReports = Join-Path $taskOut 'reports'
Assert-OutputPath $taskReports
foreach ($taskName in @('test-results.xml','test-results.log','client-preview.png','uninstall-preview.png')) {
    Assert-OutputPath (Join-Path $taskReports $taskName)
}
New-Item -ItemType Directory -Force -Path $taskReports | Out-Null
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVsWhere -latest -products '*' -property installationPath
if (-not $taskVs) { throw 'Visual Studio C++ tools are required.' }
$taskDevCmd = Join-Path $taskVs 'Common7\Tools\VsDevCmd.bat'
# Import the toolchain environment; no filesystem mutation is delegated to cmd.
$taskEnvironment = & cmd.exe /d /s /c "`"$taskDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$' -and $matches[1] -ne 'PSModulePath') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}
$env:VSLANG = '1033'
$taskCMake = Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
Push-Location $taskRoot
try {
    & $taskCMake --preset release
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    if ($Clean) { & $taskCMake --build --preset release --clean-first }
    else { & $taskCMake --build --preset release }
    if ($LASTEXITCODE) { throw 'Build failed.' }
    foreach ($taskName in $taskDeployFiles | Where-Object { -not $_.EndsWith('.exe') }) {
        Copy-Item -LiteralPath (Join-Path $taskRoot $taskName) -Destination (Join-Path $taskOut $taskName) -Force
    }
    if (-not $SkipTests) {
        & (Join-Path (Split-Path $taskCMake) 'ctest.exe') --preset release --output-junit (Join-Path $taskReports 'test-results.xml') --output-log (Join-Path $taskReports 'test-results.log')
        if ($LASTEXITCODE) { throw 'Tests failed.' }
    } else {
        Write-Host 'Tests skipped. Existing out/reports files, if any, are from an earlier test run.'
    }
    Write-Host "Programs: $taskOut\VolumeEdit.exe and $taskOut\uninstall.exe"
} finally { Pop-Location }
