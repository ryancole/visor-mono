# Configures and builds visor from a plain PowerShell prompt by loading the MSVC
# environment first (Ninja needs cl.exe on PATH).
#
#   pwsh etc/build.ps1              # debug
#   pwsh etc/build.ps1 release
#   pwsh etc/build.ps1 debug -Run   # build, then launch

[CmdletBinding()]
param(
    [ValidateSet('debug', 'release')][string]$Preset = 'debug',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio (with the C++ workload) is required.' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'No Visual Studio install with MSVC x64 tools was found.' }
    Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

if (-not (Test-Path "$Root\.deps\Qt")) {
    & "$PSScriptRoot\bootstrap.ps1"
}

Push-Location $Root
try {
    cmake --preset $Preset
    if ($LASTEXITCODE) { throw 'Configure failed.' }
    cmake --build --preset $Preset
    if ($LASTEXITCODE) { throw 'Build failed.' }
} finally {
    Pop-Location
}

if ($Run) { & "$Root\build\$Preset\visor.exe" }
