[CmdletBinding()]
param(
    [string]$AssetCachePath,
    [string]$BinaryCachePath
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$vcpkgRoot = Join-Path $projectRoot "third_party\vcpkg"

if ([string]::IsNullOrWhiteSpace($AssetCachePath)) {
    $AssetCachePath = Join-Path $vcpkgRoot "downloads"
}

if ([string]::IsNullOrWhiteSpace($BinaryCachePath)) {
    $BinaryCachePath = $env:VCPKG_DEFAULT_BINARY_CACHE
    if ([string]::IsNullOrWhiteSpace($BinaryCachePath)) {
        $BinaryCachePath = Join-Path $env:LOCALAPPDATA "vcpkg\archives"
    }
}

$hasRequiredProblem = $false

function Write-Check {
    param(
        [ValidateSet("Pass", "Warn", "Fail")]
        [string]$State,
        [string]$Name,
        [string]$Detail
    )

    $prefix = switch ($State) {
        "Pass" { "[PASS]" }
        "Warn" { "[WARN]" }
        "Fail" { "[FAIL]" }
    }

    $color = switch ($State) {
        "Pass" { "Green" }
        "Warn" { "Yellow" }
        "Fail" { "Red" }
    }

    Write-Host "$prefix $Name - $Detail" -ForegroundColor $color
}

function Get-DirectorySummary {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $null
    }

    $items = @(Get-ChildItem -LiteralPath $Path -File -Recurse -ErrorAction SilentlyContinue)
    $bytes = ($items | Measure-Object -Property Length -Sum).Sum
    if ($null -eq $bytes) {
        $bytes = 0
    }

    return "{0} files, {1:N1} MiB" -f $items.Count, ($bytes / 1MB)
}

Write-Host "Flowama environment diagnosis (read-only)" -ForegroundColor Cyan
Write-Host "Project: $projectRoot"
Write-Host ""

$git = Get-Command git -ErrorAction SilentlyContinue
if ($null -eq $git) {
    Write-Check Fail "Git" "not found in PATH. Install Git, reopen PowerShell, then rerun this script."
    $hasRequiredProblem = $true
}
else {
    $gitVersion = (& $git.Source --version 2>$null)
    Write-Check Pass "Git" "$gitVersion ($($git.Source))"
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($null -eq $cmake) {
    Write-Check Fail "CMake" "not found in PATH. Install CMake 3.25 or newer, reopen PowerShell, then rerun this script."
    $hasRequiredProblem = $true
}
else {
    $cmakeVersionText = (& $cmake.Source --version 2>$null | Select-Object -First 1)
    $cmakeVersionMatch = [regex]::Match($cmakeVersionText, "(\d+)\.(\d+)\.(\d+)")
    if (-not $cmakeVersionMatch.Success) {
        Write-Check Warn "CMake" "found at $($cmake.Source), but its version could not be read."
    }
    else {
        $cmakeVersion = [Version]$cmakeVersionMatch.Value
        if ($cmakeVersion -lt [Version]"3.25.0") {
            Write-Check Fail "CMake" "$cmakeVersion is below the project's required 3.25.0."
            $hasRequiredProblem = $true
        }
        else {
            Write-Check Pass "CMake" "$cmakeVersion ($($cmake.Source))"
        }
    }
}

$ninja = Get-Command ninja -ErrorAction SilentlyContinue
if ($null -eq $ninja) {
    Write-Check Fail "Ninja" "not found in PATH. The windows-debug preset uses the Ninja generator."
    $hasRequiredProblem = $true
}
else {
    $ninjaVersion = (& $ninja.Source --version 2>$null | Select-Object -First 1)
    Write-Check Pass "Ninja" "$ninjaVersion ($($ninja.Source))"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    Write-Check Fail "MSVC x64 toolchain" "Visual Studio Installer/vswhere was not found at '$vswhere'."
    $hasRequiredProblem = $true
}
else {
    $vsInstallPath = & $vswhere `
        -latest `
        -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath |
        Select-Object -First 1
    $vsDevCmd = if ($vsInstallPath) { Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat" }

    if ([string]::IsNullOrWhiteSpace($vsInstallPath) -or -not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
        Write-Check Fail "MSVC x64 toolchain" "Install Visual Studio with 'Desktop development with C++'."
        $hasRequiredProblem = $true
    }
    else {
        Write-Check Pass "MSVC x64 toolchain" "$vsInstallPath"
    }
}

if ($null -eq $git) {
    Write-Check Warn "vcpkg submodule" "Git is unavailable, so submodule state could not be checked."
}
else {
    $submoduleStatus = (& $git.Source -C $projectRoot submodule status -- third_party/vcpkg 2>$null | Select-Object -First 1)
    $vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
    if ($submoduleStatus -match "^-") {
        Write-Check Fail "vcpkg submodule" "not initialized. Run: git submodule update --init --recursive"
        $hasRequiredProblem = $true
    }
    elseif (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
        Write-Check Fail "vcpkg submodule" "expected toolchain file is missing: $vcpkgToolchain"
        $hasRequiredProblem = $true
    }
    else {
        $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
        $bootstrapNote = if (Test-Path -LiteralPath $vcpkgExe -PathType Leaf) { "vcpkg.exe available" } else { "vcpkg.exe will be bootstrapped by setup" }
        Write-Check Pass "vcpkg submodule" "$submoduleStatus; $bootstrapNote"
    }
}

$assetSummary = Get-DirectorySummary $AssetCachePath
if ($null -eq $assetSummary) {
    Write-Check Warn "vcpkg download cache" "not found at '$AssetCachePath'. First setup may download source archives and tools."
}
elseif ($assetSummary -match "^0 files") {
    Write-Check Warn "vcpkg download cache" "empty at '$AssetCachePath'. First setup may download source archives and tools."
}
else {
    Write-Check Pass "vcpkg download cache" "$assetSummary at '$AssetCachePath'"
}

$binarySummary = Get-DirectorySummary $BinaryCachePath
if ($null -eq $binarySummary) {
    Write-Check Warn "vcpkg binary cache" "not found at '$BinaryCachePath'. Dependencies may need local compilation."
}
elseif ($binarySummary -match "^0 files") {
    Write-Check Warn "vcpkg binary cache" "empty at '$BinaryCachePath'. Dependencies may need local compilation."
}
else {
    Write-Check Pass "vcpkg binary cache" "$binarySummary at '$BinaryCachePath'"
}

Write-Host ""
if ($hasRequiredProblem) {
    Write-Host "Required environment checks failed. Fix the [FAIL] items before running scripts/setup.ps1." -ForegroundColor Red
    exit 1
}

Write-Host "Required environment checks passed. [WARN] cache items only affect first-run speed." -ForegroundColor Green
exit 0
