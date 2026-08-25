[CmdletBinding()]
param(
    [string]$OutputPath = "data\motion-tracks\tilt-right.csv",
    [int]$Ticks = 720
)

$ErrorActionPreference = "Stop"

if ($Ticks -lt 720) {
    throw "Ticks must be at least 720 so the complete tilt sequence is present."
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
    if ($tick -gt 120 -and $tick -le 240) {
        $t = ($tick - 120) / 120.0
        $angleDegrees = 25.0 * $t * $t * (3.0 - 2.0 * $t)
    }
    elseif ($tick -gt 240 -and $tick -le 480) {
        $angleDegrees = 25.0
    }
    elseif ($tick -gt 480 -and $tick -le 600) {
        $t = ($tick - 480) / 120.0
        $angleDegrees = 25.0 * (1.0 - $t * $t * (3.0 - 2.0 * $t))
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
Write-Host "Tilt track written to '$trackPath'."
