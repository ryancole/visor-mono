#Requires -Version 7
<#
.SYNOPSIS
    Saves the test VM's login so the VM scripts can use PowerShell Direct.

.DESCRIPTION
    Prompts for the VM account's username and password and stores them in
    %LOCALAPPDATA%\visor-shell\vm-cred.xml. The password is encrypted with DPAPI,
    so only your Windows account on this machine can read it back. The file is
    kept outside the repo. Re-run to replace a saved login.

.EXAMPLE
    pwsh etc/vm/save-credential.ps1
#>
[CmdletBinding()]
param(
    [string] $Name = 'visor-test'
)

$ErrorActionPreference = 'Stop'

$dir = Join-Path $env:LOCALAPPDATA 'visor-shell'
$file = Join-Path $dir 'vm-cred.xml'

$credential = Get-Credential -Message "Login for the '$Name' VM (local account, e.g. ryan)"
if (-not $credential) {
    throw 'No credential entered.'
}

New-Item -ItemType Directory -Force $dir | Out-Null
$credential | Export-Clixml $file
Write-Host "Saved login for '$($credential.UserName)' to $file"
