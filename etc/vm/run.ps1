#Requires -Version 7
<#
.SYNOPSIS
    Runs a PowerShell script inside the test VM's interactive session and
    prints its output.

.DESCRIPTION
    PowerShell Direct runs outside the signed-in user's session, so anything
    that needs the desktop (window messages, WinRT notifications, screen
    capture) does nothing there. This runs the script as the signed-in user
    through a scheduled task (the same trick as etc/vm/input.ps1), under
    Windows PowerShell 5.1, and returns what it wrote.

.EXAMPLE
    pwsh etc/vm/run.ps1 -File spike.ps1
    pwsh etc/vm/run.ps1 -Script '[Environment]::UserName'
#>
[CmdletBinding(DefaultParameterSetName = 'File')]
param(
    [Parameter(Mandatory, ParameterSetName = 'File')] [string] $File,
    [Parameter(Mandatory, ParameterSetName = 'Script')] [string] $Script,
    # Seconds to wait for the script to finish.
    [int] $Timeout = 60,
    [string] $Name = 'visor-test'
)

$ErrorActionPreference = 'Stop'
$credFile = Join-Path $env:LOCALAPPDATA 'visor-shell\vm-cred.xml'
if (-not (Test-Path $credFile)) { throw 'No saved VM login. Run: pwsh etc/vm/save-credential.ps1' }
if ($PSCmdlet.ParameterSetName -eq 'File') { $Script = Get-Content -Raw $File }

$c = Import-Clixml $credFile
Invoke-Command -VMName $Name -Credential $c -ArgumentList $Script, $Timeout {
    param($script, $timeout)
    New-Item -ItemType Directory -Force C:\visor | Out-Null
    Set-Content C:\visor\run.ps1 $script
    Remove-Item C:\visor\run.out -ErrorAction SilentlyContinue
    $user = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $user) {
        # Only the console user is in Win32_ComputerSystem; in an Enhanced
        # Session (RDP) the session list says who is active.
        foreach ($line in (quser 2>$null)) {
            if ($line -match '^\s*>?(\S+)\s+\S+\s+\d+\s+Active') { $user = $Matches[1]; break }
        }
    }
    if (-not $user) { throw 'Nobody is signed in to the VM.' }
    # conhost --headless: no console window (see input.ps1).
    $action = New-ScheduledTaskAction -Execute conhost.exe -Argument '--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& C:\visor\run.ps1 *>&1 | Out-File C:\visor\run.out -Encoding utf8"'
    Register-ScheduledTask -TaskName visor-run -Action $action -Principal (New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive) -Force | Out-Null
    Start-ScheduledTask visor-run
    for ($i = 0; $i -lt $timeout * 4 -and (Get-ScheduledTask visor-run).State -eq 'Running'; $i++) { Start-Sleep -Milliseconds 250 }
    if ((Get-ScheduledTask visor-run).State -eq 'Running') { Write-Warning "still running after $timeout s" }
    if (Test-Path C:\visor\run.out) { Get-Content C:\visor\run.out } else { Write-Warning 'no output' }
}
