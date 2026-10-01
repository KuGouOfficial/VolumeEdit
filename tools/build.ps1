param([switch]$SkipTests, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$env:VSLANG = '1033'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVsWhere -latest -products '*' -property installationPath
if (-not $taskVs) { throw 'Visual Studio C++ tools are required.' }
$taskDevCmd = Join-Path $taskVs 'Common7\Tools\VsDevCmd.bat'
# Import the toolchain environment; no filesystem mutation is delegated to cmd.
$taskEnvironment = & cmd.exe /d /s /c "`"$taskDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
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
    if (-not $SkipTests) {
        & (Join-Path (Split-Path $taskCMake) 'ctest.exe') --preset release
        if ($LASTEXITCODE) { throw 'Tests failed.' }
    }
} finally { Pop-Location }
