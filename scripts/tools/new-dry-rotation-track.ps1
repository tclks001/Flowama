[CmdletBinding()]
param(
    [string]$OutputPath = "data\motion-tracks\dry-rotation.csv",
    [int]$Ticks = 960
)

$ErrorActionPreference = "Stop"

if ($Ticks -lt 960) {
    throw "Ticks must be at least 960 so the complete rotation sequence is present."
}

function Get-QuinticBlend([double]$Value) {
    return $Value * $Value * $Value * ($Value * ($Value * 6.0 - 15.0) + 10.0)
}

function Get-BlendedAngle(
    [int]$Tick,
    [int]$StartTick,
    [int]$EndTick,
    [double]$StartDegrees,
    [double]$EndDegrees) {
    $normalizedTime = ($Tick - $StartTick) / [double]($EndTick - $StartTick)
    $blend = Get-QuinticBlend $normalizedTime
    return $StartDegrees + ($EndDegrees - $StartDegrees) * $blend
}

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$trackPath = if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath
}
else {
    Join-Path $projectRoot $OutputPath
}

$trackDirectory = Split-Path -Parent $trackPath
New-Item -ItemType Directory -Force -Path $trackDirectory | Out-Null

$invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("# Flowama container motion track")
$lines.Add("# tick,px,py,pz,qx,qy,qz,qw")

for ($tick = 0; $tick -le $Ticks; ++$tick) {
    $angleDegrees = 0.0
    if ($tick -gt 240 -and $tick -le 360) {
        $angleDegrees = Get-BlendedAngle $tick 240 360 0.0 22.0
    }
    elseif ($tick -gt 360 -and $tick -le 480) {
        $angleDegrees = 22.0
    }
    elseif ($tick -gt 480 -and $tick -le 600) {
        $angleDegrees = Get-BlendedAngle $tick 480 600 22.0 -22.0
    }
    elseif ($tick -gt 600 -and $tick -le 720) {
        $angleDegrees = -22.0
    }
    elseif ($tick -gt 720 -and $tick -le 840) {
        $angleDegrees = Get-BlendedAngle $tick 720 840 -22.0 0.0
    }

    $halfAngleRadians = $angleDegrees * [Math]::PI / 360.0
    $qy = [Math]::Sin($halfAngleRadians)
    $qw = [Math]::Cos($halfAngleRadians)
    $format = "{0},{1},{2},{3},{4},{5},{6},{7}"
    $lines.Add(($format -f
        $tick,
        (0.0).ToString("G9", $invariantCulture),
        (0.0).ToString("G9", $invariantCulture),
        (0.0).ToString("G9", $invariantCulture),
        (0.0).ToString("G9", $invariantCulture),
        $qy.ToString("G9", $invariantCulture),
        (0.0).ToString("G9", $invariantCulture),
        $qw.ToString("G9", $invariantCulture)))
}

[System.IO.File]::WriteAllLines($trackPath, $lines)
Write-Host "Dry-container rotation track written to '$trackPath'."
