# Installs the pinned Qt toolchain into .deps/ so the repo builds without a system Qt.
#
#   pwsh etc/bootstrap.ps1          # install (no-op if already present)
#   pwsh etc/bootstrap.ps1 -Force   # wipe and reinstall
#
# Requires Python 3 (the `py` launcher or `python` on PATH). Qt is fetched from the
# official mirrors via aqtinstall, inside a private venv under .deps/.

[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'

# Keep in sync with CMakePresets.json.
$QtVersion = '6.10.3'
$QtArch    = 'win64_msvc2022_64'
$QtArchDir = 'msvc2022_64'
$AqtVersion = '3.3.0'

$Root    = Split-Path -Parent $PSScriptRoot
$Deps    = Join-Path $Root '.deps'
$Venv    = Join-Path $Deps 'venv'
$QtRoot  = Join-Path $Deps 'Qt'
$QtPrefix = Join-Path $QtRoot "$QtVersion\$QtArchDir"

if ((Test-Path "$QtPrefix\lib\cmake\Qt6\Qt6Config.cmake") -and -not $Force) {
    Write-Host "Qt $QtVersion already installed at $QtPrefix"
    exit 0
}

if ($Force -and (Test-Path $QtRoot)) { Remove-Item -Recurse -Force $QtRoot }
New-Item -ItemType Directory -Force $Deps | Out-Null

# visor pins the same Qt; if it is checked out next to this repo, share its
# toolchain through a junction instead of downloading another copy.
$SiblingQt = Join-Path (Split-Path -Parent $Root) 'visor\.deps\Qt'
if (-not $Force -and (Test-Path "$SiblingQt\$QtVersion\$QtArchDir\lib\cmake\Qt6\Qt6Config.cmake")) {
    if (Test-Path $QtRoot) { Remove-Item -Recurse -Force $QtRoot }
    New-Item -ItemType Junction -Path $QtRoot -Target $SiblingQt | Out-Null
    Write-Host "Linked .deps\Qt to visor's Qt $QtVersion ($SiblingQt)"
    exit 0
}

if (Get-Command py -ErrorAction SilentlyContinue) { $py = 'py'; $pyArgs = @('-3') }
elseif (Get-Command python -ErrorAction SilentlyContinue) { $py = 'python'; $pyArgs = @() }
else { throw 'Python 3 is required to install Qt (https://www.python.org/downloads/).' }

$venvPy = Join-Path $Venv 'Scripts\python.exe'
if (-not (Test-Path $venvPy)) {
    Write-Host 'Creating Python venv for aqtinstall...'
    & $py @pyArgs -m venv $Venv
    if ($LASTEXITCODE) { throw 'Failed to create venv.' }
}

& $venvPy -m pip install --disable-pip-version-check -q "aqtinstall==$AqtVersion"
if ($LASTEXITCODE) { throw 'Failed to install aqtinstall.' }

Write-Host "Installing Qt $QtVersion ($QtArch) into $QtRoot ..."
Push-Location $Deps  # aqt writes aqtinstall.log to the cwd
& $venvPy -m aqt install-qt windows desktop $QtVersion $QtArch --outputdir $QtRoot
$failed = $LASTEXITCODE
Pop-Location
if ($failed) { throw 'aqt install-qt failed.' }

Write-Host ''
Write-Host "Done. Configure and build with:"
Write-Host '  cmake --preset debug'
Write-Host '  cmake --build --preset debug'
