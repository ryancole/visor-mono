#Requires -Version 5.1
<#
.SYNOPSIS
    Installs visor for the current user: as the Windows shell (replace mode),
    or alongside Explorer (hosted mode, -Hosted).

.DESCRIPTION
    Replace mode sets the per-user shell override
        HKCU\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell
    (never the machine-wide HKLM value) to visor-session.exe and remembers the
    previous value so etc/uninstall.ps1 can put it back.

    Hosted mode (-Hosted) leaves Explorer as the shell and adds a Run entry
        HKCU\Software\Microsoft\Windows\CurrentVersion\Run\visor-shell
    that starts `visor-session --mode hosted` at sign-in, the way Windows
    starts any app at sign-in (it shows in Settings > Apps > Startup, where it
    can be turned off). visor-session starts visor-shell and restarts it if it
    crashes; visor-shell starts Visor and asks Explorer's taskbar to auto-hide.
    With -Tiling it also runs visor-wm, which takes the tiling keys from
    Explorer (the Tiling value under HKCU\Software\visor-shell; -Hosted
    without -Tiling turns it off again).

    The two are exclusive: each install removes the other's entry, since in
    replace mode visor-shell runs the Run entries itself. Either takes effect
    at the next sign-in.

    Refuses to run on a physical machine unless -AllowPhysicalMachine is given:
    both modes are tested in a VM first.

    Runs on Windows PowerShell 5.1 too, so it works inside a fresh VM.

.EXAMPLE
    .\install.ps1 -Path C:\visor\visor-session.exe
    .\install.ps1 -Path C:\visor\visor-session.exe -Hosted
    .\install.ps1 -Path C:\visor\visor-session.exe -Hosted -Tiling
#>
[CmdletBinding()]
param(
    # visor-session.exe; defaults to the one next to this script. The other
    # programs are expected next to it.
    [string] $Path = (Join-Path $PSScriptRoot 'visor-session.exe'),
    # Hosted mode: Explorer stays the shell, visor-shell starts from a Run entry.
    [switch] $Hosted,
    # Hosted mode with visor-wm (tiling, and Omarchy's keys over Explorer's).
    [switch] $Tiling,
    [switch] $AllowPhysicalMachine
)

$ErrorActionPreference = 'Stop'

$WinlogonKey = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
$RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$RunValue = 'visor-shell'
$StateKey = 'HKCU:\Software\visor-shell'
$NoPrevious = '<none>'

$cs = Get-CimInstance Win32_ComputerSystem
$isVm = $cs.Model -match 'Virtual Machine|VMware|VirtualBox|KVM'
if (-not $isVm -and -not $AllowPhysicalMachine) {
    throw "This looks like a physical machine ($($cs.Manufacturer) $($cs.Model)). " +
        'Test visor-shell in a VM; pass -AllowPhysicalMachine only once you have a tested recovery path.'
}

# Never point Winlogon (or a Run entry) at something that can't start: check
# the whole set.
$Path = (Resolve-Path $Path).Path
$dir = Split-Path -Parent $Path
foreach ($file in 'visor-session.exe', 'visor-shell.exe', 'visor.exe', 'Qt6Core.dll') {
    if (-not (Test-Path (Join-Path $dir $file))) {
        throw "$file is missing from $dir"
    }
}

$current = (Get-ItemProperty $WinlogonKey -Name Shell -ErrorAction SilentlyContinue).Shell

if ($Hosted) {
    # Explorer must be the shell again: put back what the replace-mode
    # install saved (as uninstall.ps1 does).
    if ($current -like '*visor-session.exe*') {
        $previous = (Get-ItemProperty $StateKey -Name PreviousShell -ErrorAction SilentlyContinue).PreviousShell
        if ($previous -and $previous -ne $NoPrevious -and $previous -notlike '*visor-session.exe*') {
            Set-ItemProperty $WinlogonKey -Name Shell -Value $previous
        } else {
            Remove-ItemProperty $WinlogonKey -Name Shell
        }
        Remove-ItemProperty $StateKey -Name PreviousShell -ErrorAction SilentlyContinue
        Write-Host 'Removed the replace-mode shell override; Explorer is the shell again at next sign-in.'
    }
    $value = "`"$Path`" --mode hosted"
    New-Item $RunKey -Force | Out-Null
    Set-ItemProperty $RunKey -Name $RunValue -Value $value
    New-Item $StateKey -Force | Out-Null
    if ($Tiling) {
        Set-ItemProperty $StateKey -Name Tiling -Value 1 -Type DWord
    } else {
        Remove-ItemProperty $StateKey -Name Tiling -ErrorAction SilentlyContinue
    }
    Write-Host "Run entry for $env:USERNAME set to $value (starts at next sign-in; or run it now)."
    Write-Host "Tiling (visor-wm) is $(if ($Tiling) { 'on' } else { 'off' })."
    Write-Host 'Undo with uninstall.ps1. Ctrl+Alt+Q quits visor-shell and brings the taskbar back.'
    return
}

# Replace mode. A leftover Run entry would start a second, hosted visor-shell
# from our own startup runner.
Remove-ItemProperty $RunKey -Name $RunValue -ErrorAction SilentlyContinue

$value = "`"$Path`""

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
