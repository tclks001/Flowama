[CmdletBinding()]
param(
    [string]$Preset = "windows-debug"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vcpkgRoot = Join-Path $projectRoot "third_party\vcpkg"
$vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
$bootstrapScript = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
$toolchainScript = Join-Path $PSScriptRoot "internal\import-msvc-environment.ps1"
$buildScript = Join-Path $PSScriptRoot "build.ps1"

Push-Location $projectRoot

try {
    git submodule update --init --recursive

    if (-not (Test-Path $vcpkgExe)) {
        & $bootstrapScript -disableMetrics
        if ($LASTEXITCODE -ne 0) {
            throw "vcpkg bootstrap failed."
        }
    }

    . $toolchainScript

    cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed."
    }

    & $buildScript -Preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed."
    }
}
finally {
    Pop-Location
}
