#Requires -Version 5.1
<#
.SYNOPSIS
    Restores the current user's shell to what it was before install.ps1.

.DESCRIPTION
    Puts back the per-user Winlogon Shell value saved by install.ps1, or removes
    it (so the machine-wide default, explorer.exe, applies). Safe to run any
    time, including when visor-shell was never installed. Takes effect at the
    next sign-in; run explorer.exe to get a desktop right away.

.EXAMPLE
    .\uninstall.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$WinlogonKey = 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon'
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

Remove-Item $StateKey -Recurse -ErrorAction SilentlyContinue
