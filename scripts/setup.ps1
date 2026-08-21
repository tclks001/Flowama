[CmdletBinding()]
param(
    [string]$Preset = "windows-debug"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vcpkgRoot = Join-Path $projectRoot "third_party\vcpkg"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
$bootstrapScript = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"

Push-Location $projectRoot

try {
    git submodule update --init --recursive

    if (-not (Test-Path $vcpkgExe)) {
        & $bootstrapScript -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg bootstrap failed."
        }
    }

    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }

    cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}
finally {
    Pop-Location
}