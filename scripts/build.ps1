[CmdletBinding()]
param(
    [string]$Preset = "windows-debug"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cacheFile = Join-Path $projectRoot "build\$Preset\CMakeCache.txt"
$toolchainScript = Join-Path $PSScriptRoot "import-msvc-environment.ps1"

if (-not (Test-Path -LiteralPath $cacheFile)) {
    throw "Build directory for preset '$Preset' is not configured. Run scripts/setup.ps1 first."
}

. $toolchainScript

Push-Location $projectRoot

try {
    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}
finally {
    Pop-Location
}
