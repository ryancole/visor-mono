#Requires -Version 5.1
<#
.SYNOPSIS
    Removes what install.ps1 set up for the current user, in either mode.

.DESCRIPTION
    Puts back the per-user Winlogon Shell value saved by install.ps1, or removes
    it (so the machine-wide default, explorer.exe, applies), and removes the
    hosted-mode Run entry. Safe to run any time, including when visor-shell was
    never installed. Takes effect at the next sign-in; run explorer.exe to get
    a desktop right away.

    A visor-shell running in hosted mode is asked to quit first (which puts
    Explorer's taskbar back). That only works from the user's own session; from
    outside it (PowerShell Direct), quit it with Ctrl+Alt+Q instead.

.EXAMPLE
    .\uninstall.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$WinlogonKey = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
$RunKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$RunValue = 'visor-shell'
$StateKey = 'HKCU:\Software\visor-shell'
$NoPrevious = '<none>'

$current = (Get-ItemProperty $WinlogonKey -Name Shell -ErrorAction SilentlyContinue).Shell
$previous = (Get-ItemProperty $StateKey -Name PreviousShell -ErrorAction SilentlyContinue).PreviousShell

if ($previous -and $previous -ne $NoPrevious -and $previous -notlike '*visor-session.exe*') {
    Set-ItemProperty $WinlogonKey -Name Shell -Value $previous
    Write-Host "Shell for $env:USERNAME restored to $previous."
} elseif ($current -like '*visor-session.exe*') {
    Remove-ItemProperty $WinlogonKey -Name Shell
    Write-Host "Per-user shell override removed for $env:USERNAME; the machine default (explorer.exe) applies."
} else {
    # Not ours (or not set): leave whatever the user has alone.
    Write-Host "visor-shell is not the shell for $env:USERNAME; nothing to restore."
}

$run = (Get-ItemProperty $RunKey -Name $RunValue -ErrorAction SilentlyContinue).$RunValue
if ($run) {
    Remove-ItemProperty $RunKey -Name $RunValue
    Write-Host "Run entry removed for $env:USERNAME ($run)."
    if ((Get-Process visor-shell -ErrorAction SilentlyContinue) -and $run -match '^"([^"]+)"') {
        # The entry names visor-session; visor-shell is next to it.
        & (Join-Path (Split-Path -Parent $Matches[1]) 'visor-shell.exe') --quit
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and (Get-Process visor-shell -ErrorAction SilentlyContinue)) {
            Start-Sleep -Milliseconds 250
        }
        if (Get-Process visor-shell -ErrorAction SilentlyContinue) {
            Write-Warning 'visor-shell is still running; press Ctrl+Alt+Q in its session to quit it.'
        }
    }
}

# PreviousShell, and the taskbar state a hosted visor-shell restores on
# exit. Only the latter is kept if a hosted shell is still running.
if (Get-Process visor-shell -ErrorAction SilentlyContinue) {
    Remove-ItemProperty $StateKey -Name PreviousShell -ErrorAction SilentlyContinue
} else {
    Remove-Item $StateKey -Recurse -ErrorAction SilentlyContinue
}
