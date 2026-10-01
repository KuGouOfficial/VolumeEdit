# Shared build helpers. The product version is read only from Makefile.
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskOut = Join-Path $taskRoot 'out'
$taskDeployFiles = @('VolumeEdit.exe','uninstall.exe','VolumeEditBroker.exe','product.id','README.md','DESIGN.md','BUILD_STATUS.md','LICENSE')

function Get-ProjectVersion {
    $taskMatches = [regex]::Matches((Get-Content -LiteralPath (Join-Path $taskRoot 'Makefile') -Raw), '(?m)^VERSION[ \t]*=[ \t]*([0-9]+\.[0-9]+(?:\.[0-9]+)?)[ \t]*\r?$')
    if ($taskMatches.Count -ne 1) { throw 'Makefile must define one VERSION = major.minor[.patch]' }
    return $taskMatches[0].Groups[1].Value
}

function Get-ProductSha256([string]$Path) {
    $taskHasher = [Security.Cryptography.SHA256]::Create()
    $taskStream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($taskHasher.ComputeHash($taskStream)).Replace('-', '') }
    finally { $taskStream.Dispose(); $taskHasher.Dispose() }
}

function Assert-OutputPath([string]$Path) {
    $taskFullPath = [IO.Path]::GetFullPath($Path)
    if ($taskFullPath -ne $taskOut -and -not $taskFullPath.StartsWith($taskOut + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Output escaped the workspace out directory.'
    }
    $taskCheck = $taskFullPath
    while ($taskCheck -ne $taskRoot) {
        if (Test-Path -LiteralPath $taskCheck) {
            if ((Get-Item -LiteralPath $taskCheck -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Output path contains a link or junction: $taskCheck"
            }
        }
        $taskCheck = Split-Path -Parent $taskCheck
    }
}

function Initialize-Output {
    Assert-OutputPath $taskOut
    foreach ($taskName in $taskDeployFiles) { Assert-OutputPath (Join-Path $taskOut $taskName) }
    $taskMarker = Join-Path $taskOut 'product.id'
    if (Test-Path -LiteralPath $taskMarker) {
        if ([IO.File]::ReadAllText($taskMarker) -ne [IO.File]::ReadAllText((Join-Path $taskRoot 'product.id'))) {
            throw 'out/product.id belongs to an unknown product; build stopped.'
        }
    }
    New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
}
