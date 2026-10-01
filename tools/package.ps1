param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'project.ps1')
if ($OutputDirectory) {
    $taskCandidate = [IO.Path]::GetFullPath((Join-Path $taskRoot $OutputDirectory))
    if ($taskCandidate -ne $taskOut -and -not $taskCandidate.StartsWith($taskOut+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Alternate output must stay under out.' }
    $taskOut = $taskCandidate
}
$taskVersion = Get-ProjectVersion
$taskZip = Join-Path $taskOut ("VolumeEdit-portable-v$taskVersion-x64.zip")
$taskZipHash = $taskZip + '.sha256'
$taskChecksums = Join-Path $taskOut 'SHA256SUMS.txt'
Initialize-Output
foreach ($taskPath in @($taskZip, $taskZipHash, $taskChecksums)) { Assert-OutputPath $taskPath }

& (Join-Path $PSScriptRoot 'build.ps1') -OutputDirectory $OutputDirectory
# Package only the explicit product allowlist. Never include reports, state,
# old ZIPs or user files, and never delete those files from out.
$taskDeployFiles | ForEach-Object {
    '{0}  {1}' -f (Get-ProductSha256 (Join-Path $taskOut $_)), $_
} | Set-Content -LiteralPath $taskChecksums -Encoding UTF8
$taskPayload = @($taskDeployFiles | ForEach-Object { Join-Path $taskOut $_ }) + $taskChecksums
# Use the built-in .NET APIs in Windows PowerShell 5.1 and PowerShell 7 alike.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$taskZipStream = [IO.File]::Open($taskZip, [IO.FileMode]::Create)
try {
    $taskArchive = [IO.Compression.ZipArchive]::new($taskZipStream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($taskPath in $taskPayload) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($taskArchive, $taskPath, [IO.Path]::GetFileName($taskPath), [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $taskArchive.Dispose() }
} finally { $taskZipStream.Dispose() }
'{0}  {1}' -f (Get-ProductSha256 $taskZip), (Split-Path -Leaf $taskZip) |
    Set-Content -LiteralPath $taskZipHash -Encoding UTF8
Write-Host "Portable release: $taskZip"
Write-Host "Archive checksum: $taskZipHash"
