#Requires -Version 7
<#
.SYNOPSIS
    Clicks or presses keys on the test VM's screen, from inside the VM.

.DESCRIPTION
    Runs SendInput in the signed-in user's session (through a scheduled
    task, over PowerShell Direct), so it needs no VM window or focus on the
    host. Coordinates are guest screen pixels, as in etc/vm/screenshot.ps1.
    Keys are virtual-key names joined with '+', e.g. 'ctrl+alt+r', 'f11',
    'escape', 'win+r'.

.EXAMPLE
    pwsh etc/vm/input.ps1 -Click 948,16 -Button right
    pwsh etc/vm/input.ps1 -Click 100,200 -Double
    pwsh etc/vm/input.ps1 -Key ctrl+alt+r
#>
[CmdletBinding(DefaultParameterSetName = 'Click')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Click')] [string] $Click, # "x,y"
    [Parameter(ParameterSetName = 'Click')] [ValidateSet('left', 'right', 'middle')] [string] $Button = 'left',
    [Parameter(ParameterSetName = 'Click')] [switch] $Double,
    [Parameter(Mandatory, ParameterSetName = 'Key')] [string] $Key,
    [string] $Name = 'visor-test'
)

$ErrorActionPreference = 'Stop'
$credFile = Join-Path $env:LOCALAPPDATA 'visor-shell\vm-cred.xml'
if (-not (Test-Path $credFile)) { throw 'No saved VM login. Run: pwsh etc/vm/save-credential.ps1' }

$prelude = @'
Add-Type -Namespace W -Name U -MemberDefinition '
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, System.UIntPtr e);
[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, System.UIntPtr e);'
'@

if ($PSCmdlet.ParameterSetName -eq 'Click') {
    $point = $Click -split ',' | ForEach-Object { [int]$_.Trim() }
    if ($point.Count -ne 2) { throw '-Click takes x,y.' }
    $flags = @{ left = '0x0002, 0x0004'; right = '0x0008, 0x0010'; middle = '0x0020, 0x0040' }[$Button]
    $clicks = if ($Double) { 2 } else { 1 }
    $body = @"
[W.U]::SetCursorPos($($point[0]), $($point[1])) | Out-Null
Start-Sleep -Milliseconds 150
`$down, `$up = $flags
for (`$i = 0; `$i -lt $clicks; `$i++) {
    [W.U]::mouse_event(`$down, 0, 0, 0, [System.UIntPtr]::Zero)
    [W.U]::mouse_event(`$up, 0, 0, 0, [System.UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
}
"@
} else {
    $names = @{
        ctrl = 0x11; alt = 0x12; shift = 0x10; win = 0x5B; escape = 0x1B; esc = 0x1B; enter = 0x0D; tab = 0x09
        space = 0x20; left = 0x25; up = 0x26; right = 0x27; down = 0x28; delete = 0x2E; backspace = 0x08
        return = 0x0D; minus = 0xBD; equal = 0xBB
        volume_mute = 0xAD; volume_down = 0xAE; volume_up = 0xAF
    }
    $vks = foreach ($part in $Key.ToLower() -split '\+') {
        if ($names.ContainsKey($part)) { $names[$part] }
        elseif ($part -match '^f(\d{1,2})$') { 0x6F + [int]$Matches[1] }
        elseif ($part.Length -eq 1) { [int][char]$part.ToUpper() }
        else { throw "Unknown key '$part'." }
    }
    $down = ($vks | ForEach-Object { "[W.U]::keybd_event($_, 0, 0, [System.UIntPtr]::Zero)" }) -join "`n"
    [array]::Reverse($vks)
    $up = ($vks | ForEach-Object { "[W.U]::keybd_event($_, 0, 2, [System.UIntPtr]::Zero)" }) -join "`n"
    $body = "$down`nStart-Sleep -Milliseconds 50`n$up"
}

$c = Import-Clixml $credFile
Invoke-Command -VMName $Name -Credential $c -ArgumentList "$prelude`n$body" {
    param($script)
    New-Item -ItemType Directory -Force C:\visor | Out-Null
    Set-Content C:\visor\input.ps1 $script
    $user = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $user) {
        # Only the console user is in Win32_ComputerSystem; in an Enhanced
        # Session (RDP) the session list says who is active.
        foreach ($line in (quser 2>$null)) {
            if ($line -match '^\s*>?(\S+)\s+\S+\s+\d+\s+Active') { $user = $Matches[1]; break }
        }
    }
    if (-not $user) { throw 'Nobody is signed in to the VM.' }
    # conhost --headless: no console window. A plain powershell.exe would be
    # handed to Windows Terminal, whose window takes the foreground and spoils
    # whatever the input was meant for.
    $action = New-ScheduledTaskAction -Execute conhost.exe -Argument '--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\visor\input.ps1'
    Register-ScheduledTask -TaskName visor-input -Action $action -Principal (New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive) -Force | Out-Null
    Start-ScheduledTask visor-input
    # Wait for it to finish so callers can screenshot the result.
    for ($i = 0; $i -lt 40 -and (Get-ScheduledTask visor-input).State -eq 'Running'; $i++) { Start-Sleep -Milliseconds 250 }
}
