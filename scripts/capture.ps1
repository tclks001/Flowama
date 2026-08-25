[CmdletBinding()]
param(
    [int]$Ticks = 720,
    [int]$CaptureEvery = 120,
    [string]$OutputDirectory = "artifacts\captures\granular-settling",
    [string]$MotionTrack = "",
    [string]$Preset = "windows-debug"
)

$ErrorActionPreference = "Stop"

if ($Ticks -le 0) {
    throw "Ticks must be a positive integer."
}

if ($CaptureEvery -le 0) {
    throw "CaptureEvery must be a positive integer."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    throw "OutputDirectory must not be empty."
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$executable = Join-Path $projectRoot "build\$Preset\flowama.exe"

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Expected executable was not found at '$executable'. Run scripts/setup.ps1 or scripts/build.ps1 first."
}

$captureDirectory = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
}
else {
    Join-Path $projectRoot $OutputDirectory
}

$arguments = @(
    "--capture",
    "--ticks", $Ticks,
    "--capture-every", $CaptureEvery,
    "--output", $captureDirectory
)
if (-not [string]::IsNullOrWhiteSpace($MotionTrack)) {
    $motionTrackPath = if ([System.IO.Path]::IsPathRooted($MotionTrack)) {
        $MotionTrack
    }
    else {
        Join-Path $projectRoot $MotionTrack
    }

    if (-not (Test-Path -LiteralPath $motionTrackPath -PathType Leaf)) {
        throw "Motion track was not found at '$motionTrackPath'."
    }

    $arguments += @("--motion-track", $motionTrackPath)
}

& $executable @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Capture run failed."
}

Write-Host "Capture output written to '$captureDirectory'."
