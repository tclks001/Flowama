[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"

$compiler = Get-Command cl -ErrorAction SilentlyContinue
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$hasX64DeveloperEnvironment = (
    $env:VSCMD_ARG_HOST_ARCH -eq "x64" -and
    $env:VSCMD_ARG_TGT_ARCH -eq "x64" -and
    $null -ne $compiler -and
    $null -ne $cmakeCommand
)

if ($hasX64DeveloperEnvironment) {
    return
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer was not found at '$vswhere'."
}

$vsInstallPath = & $vswhere `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath |
    Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
    throw "No Visual Studio installation with the x64 C++ toolchain was found."
}

$vsDevCmd = Join-Path $vsInstallPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "Visual Studio developer environment script was not found at '$vsDevCmd'."
}

# A batch file cannot modify this PowerShell process directly. Capture its
# environment through cmd.exe, then import each variable into this process.
$command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && set' -f $vsDevCmd
$environmentLines = & $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Failed to initialize the Visual Studio x64 developer environment."
}

foreach ($line in $environmentLines) {
    $separatorIndex = $line.IndexOf('=')
    if ($separatorIndex -le 0) {
        continue
    }

    $name = $line.Substring(0, $separatorIndex)
    $value = $line.Substring($separatorIndex + 1)
    [Environment]::SetEnvironmentVariable(
        $name,
        $value,
        [EnvironmentVariableTarget]::Process)
}

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    throw "MSVC compiler 'cl' is unavailable after developer-environment initialization."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is unavailable after developer-environment initialization."
}
