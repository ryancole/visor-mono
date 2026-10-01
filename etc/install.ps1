#Requires -Version 5.1
<#
.SYNOPSIS
    Makes visor-session.exe the current user's Windows shell.

.DESCRIPTION
    Sets the per-user shell override
        HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
    (never the machine-wide HKLM value) and remembers the previous value so
    etc/uninstall.ps1 can put it back. Takes effect at the next sign-in.

    Refuses to run on a physical machine unless -AllowPhysicalMachine is given:
    replacing the shell is tested in a VM first.

    Runs on Windows PowerShell 5.1 too, so it works inside a fresh VM.

.EXAMPLE
    .\install.ps1 -Path C:\visor-shell\visor-session.exe
#>
[CmdletBinding()]
param(
    # visor-session.exe; defaults to the one next to this script.
    [string] $Path = (Join-Path $PSScriptRoot 'visor-session.exe'),
    [switch] $AllowPhysicalMachine
)

$ErrorActionPreference = 'Stop'

$WinlogonKey = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
$StateKey = 'HKCU:\Software\visor-shell'
$NoPrevious = '<none>'

$cs = Get-CimInstance Win32_ComputerSystem
$isVm = $cs.Model -match 'Virtual Machine|VMware|VirtualBox|KVM'
if (-not $isVm -and -not $AllowPhysicalMachine) {
    throw "This looks like a physical machine ($($cs.Manufacturer) $($cs.Model)). " +
        'Test visor-shell in a VM; pass -AllowPhysicalMachine only once you have a tested recovery path.'
}

# Never point Winlogon at something that can't start: check the whole set.
$Path = (Resolve-Path $Path).Path
$dir = Split-Path -Parent $Path
foreach ($file in 'visor-session.exe', 'visor-shell.exe', 'Qt6Core.dll') {
    if (-not (Test-Path (Join-Path $dir $file))) {
        throw "$file is missing from $dir"
    }
}

$value = "`"$Path`""
$current = (Get-ItemProperty $WinlogonKey -Name Shell -ErrorAction SilentlyContinue).Shell

# Only record the previous value on the first install, so re-installing
# doesn't overwrite the original with our own path.
if ($current -notlike '*visor-session.exe*') {
    New-Item $StateKey -Force | Out-Null
    $previous = if ($null -eq $current) { $NoPrevious } else { $current }
    Set-ItemProperty $StateKey -Name PreviousShell -Value $previous
}

Set-ItemProperty $WinlogonKey -Name Shell -Value $value
Write-Host "Shell for $env:USERNAME set to $value (takes effect at next sign-in)."
Write-Host @'

If sign-in ever lands on a broken desktop:
  - Sign in holding Shift to get Explorer for that session.
  - Ctrl+Shift+Esc -> Task Manager -> Run new task -> explorer.exe (or regedit).
  - From the host: pwsh etc/vm/deploy.ps1 -Restore
Undo with uninstall.ps1.
'@
