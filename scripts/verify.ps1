[CmdletBinding()]
param(
    [int]$Ticks = 120,
    [string]$Preset = "windows-debug"
)

$ErrorActionPreference = "Stop"

if ($Ticks -le 0) {
    throw "Ticks must be a positive integer."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$executable = Join-Path $projectRoot "build\$Preset\flowama.exe"

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Expected executable was not found at '$executable'. Run scripts/setup.ps1 or scripts/build.ps1 first."
}

& $executable --verify --ticks $Ticks
if ($LASTEXITCODE -ne 0) {
    throw "Fixed-tick verification failed."
}

Write-Host "Fixed-tick verification passed for $Ticks ticks."
