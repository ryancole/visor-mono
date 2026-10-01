#Requires -Version 7
<#
.SYNOPSIS
    Copies a visor-shell build into the test VM, and installs, restarts or
    removes it there.

.DESCRIPTION
    Uses PowerShell Direct, so it works without networking and even when the
    VM's desktop is black. Needs Hyper-V admin rights on the host and the login
    saved by etc/vm/save-credential.ps1.

    The build goes to C:\visor-shell in the VM (Visor, with -Visor, to
    C:\visor-shell\visor). Running visor processes are stopped first so their
    files can be replaced, and restarted afterwards on the VM's desktop.

.EXAMPLE
    pwsh etc/vm/deploy.ps1 -Install        # first time: copy + make it the shell
    pwsh etc/vm/deploy.ps1                 # later: copy + restart the running shell
    pwsh etc/vm/deploy.ps1 -Visor          # also ship ../visor/build/release
    pwsh etc/vm/deploy.ps1 -Restore        # emergency: back to Explorer, now
#>
[CmdletBinding(DefaultParameterSetName = 'Deploy')]
param(
    [Parameter(ParameterSetName = 'Deploy')]
    [ValidateSet('release')] # debug builds need the debug CRT, which the VM lacks
    [string] $Preset = 'release',

    # Also run install.ps1 in the VM (sets the per-user shell).
    [Parameter(ParameterSetName = 'Deploy')]
    [switch] $Install,

    # Also deploy Visor from its release build.
    [Parameter(ParameterSetName = 'Deploy')]
    [switch] $Visor,

    [Parameter(ParameterSetName = 'Deploy')]
    [string] $VisorBuild,

    # Undo install.ps1 in the VM, stop visor-shell, and start Explorer on the
    # VM's desktop. Copies nothing.
    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [switch] $Restore,

    [string] $Name = 'visor-test'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Target = 'C:\visor-shell'

$credFile = Join-Path $env:LOCALAPPDATA 'visor-shell\vm-cred.xml'
if (-not (Test-Path $credFile)) {
    throw "No saved VM login. Run: pwsh etc/vm/save-credential.ps1"
}
$session = New-PSSession -VMName $Name -Credential (Import-Clixml $credFile)

# Runs in the VM: helpers shared by every step below.
$vmHelpers = {
    # Processes started over PowerShell Direct don't appear on the user's
    # desktop, so go through a scheduled task that runs in their interactive
    # session instead.
    function Start-OnDesktop([string] $Exe, [string] $Arguments) {
        $user = (Get-CimInstance Win32_ComputerSystem).UserName
        if (-not $user) {
            Write-Warning 'Nobody is signed in to the VM; not starting anything.'
            return
        }
        $actionArgs = @{ Execute = $Exe }
        if ($Arguments) { $actionArgs.Argument = $Arguments }
        $task = @{
            TaskName  = 'visor-shell-start'
            Action    = New-ScheduledTaskAction @actionArgs
            Principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
            # No time limit: the default (72 h) would kill the shell. Parallel,
            # since an earlier run (e.g. Explorer after -Restore) may still be
            # alive and would otherwise block this one.
            Settings  = New-ScheduledTaskSettingsSet -ExecutionTimeLimit 0 -MultipleInstances Parallel `
                -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
            Force     = $true
        }
        Register-ScheduledTask @task | Out-Null
        Start-ScheduledTask -TaskName 'visor-shell-start'
        Write-Host "Started $Exe on $user's desktop."
    }

    # visor-session first, so it can't restart visor-shell itself. Note that
    # Winlogon restarts the configured shell when visor-shell dies, so when
    # installed the shell comes straight back (with whatever files are there).
    function Stop-Visor {
        foreach ($name in 'visor-session', 'visor-shell', 'visor') {
            Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
        }
        Start-Sleep -Milliseconds 500
    }

    # Running exes and loaded DLLs can't be overwritten, but they can be
    # renamed. Move them aside so new files can be copied in while the old
    # shell keeps running; the leftovers are deleted on a later deploy.
    function Move-Aside([string] $Dir, [string[]] $Names) {
        Get-ChildItem $Dir -Filter '*.old' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
        foreach ($name in $Names) {
            $path = Join-Path $Dir $name
            if (Test-Path $path) {
                Rename-Item $path "$name.$([guid]::NewGuid().ToString('N').Substring(0, 8)).old"
            }
        }
    }

    function Wait-Shell([int] $Seconds) {
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            if (Get-Process visor-shell -ErrorAction SilentlyContinue) { return $true }
            Start-Sleep -Milliseconds 250
        }
        return $false
    }

    function Test-Installed {
        $shell = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' -Name Shell -ErrorAction SilentlyContinue).Shell
        return $shell -like '*visor-session.exe*'
    }
}

try {
    Invoke-Command -Session $session -ScriptBlock $vmHelpers

    if ($Restore) {
        Invoke-Command -Session $session -FilePath "$Root\etc\uninstall.ps1"
        Invoke-Command -Session $session -ScriptBlock {
            Stop-Visor | Out-Null
            # With no shell window left, a bare explorer.exe becomes the shell.
            # (If Explorer already is the shell, this just opens a folder.)
            Start-OnDesktop 'C:\Windows\explorer.exe'
        }
        return
    }

    $build = Join-Path $Root "build\$Preset"
    foreach ($file in 'visor-session.exe', 'visor-shell.exe', 'Qt6Core.dll') {
        if (-not (Test-Path (Join-Path $build $file))) {
            throw "$file not found in $build. Build first: pwsh etc/build.ps1 $Preset"
        }
    }

    $wasRunning = Invoke-Command -Session $session -ScriptBlock { [bool](Get-Process visor-shell -ErrorAction SilentlyContinue) }

    # The exes, the Qt runtime, and PDBs for symbolising crashes.
    $files = Get-ChildItem $build -File | Where-Object Extension -in '.exe', '.dll', '.pdb'
    $names = @($files.Name) + 'install.ps1', 'uninstall.ps1'
    Invoke-Command -Session $session -ScriptBlock {
        New-Item -ItemType Directory -Force $using:Target | Out-Null
        Move-Aside $using:Target $using:names
    }
    Copy-Item -ToSession $session -Path $files.FullName -Destination $Target -Force
    foreach ($script in 'install.ps1', 'uninstall.ps1') {
        Copy-Item -ToSession $session -Path "$Root\etc\$script" -Destination $Target -Force
    }
    Write-Host "Copied visor-shell ($Preset) to $Target in '$Name'."

    if ($Visor) {
        if (-not $VisorBuild) {
            $VisorBuild = Join-Path (Split-Path -Parent $Root) 'visor\build\release'
        }
        if (-not (Test-Path "$VisorBuild\visor.exe")) {
            throw "visor.exe not found in $VisorBuild. Build Visor's release preset first."
        }
        # Everything windeployqt produced, minus CMake/Ninja's own files.
        $skip = 'CMakeFiles', 'src', '.qt', 'CMakeCache.txt', 'build.ninja', 'cmake_install.cmake',
            'compile_commands.json', '.ninja_deps', '.ninja_log'
        $items = Get-ChildItem $VisorBuild -Force | Where-Object Name -notin $skip
        Invoke-Command -Session $session -ScriptBlock {
            # Nothing restarts Visor on its own, so it can simply be stopped.
            Get-Process visor -ErrorAction SilentlyContinue | Stop-Process -Force
            Start-Sleep -Milliseconds 500
            $dir = Join-Path $using:Target 'visor'
            if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
            New-Item -ItemType Directory $dir | Out-Null
        }
        Copy-Item -ToSession $session -Path $items.FullName -Destination "$Target\visor" -Recurse -Force
        Write-Host "Copied Visor to $Target\visor."
    }

    if ($Install) {
        Invoke-Command -Session $session -FilePath "$Root\etc\install.ps1" -ArgumentList "$Target\visor-session.exe"
    }

    $installed = Invoke-Command -Session $session -ScriptBlock { Test-Installed }
    if ($installed -and $wasRunning) {
        # Restart onto the new build: Winlogon relaunches the shell once
        # visor-shell exits. Start it ourselves if that doesn't happen.
        Invoke-Command -Session $session -ScriptBlock {
            Stop-Visor
            if (Wait-Shell 5) {
                Write-Host 'visor-shell restarted on the new build.'
            } else {
                Start-OnDesktop "$using:Target\visor-session.exe"
            }
        }
    } elseif ($installed) {
        Write-Host "Installed. Sign out of the VM and back in to start visor-shell as the shell."
    }
} finally {
    Remove-PSSession $session
}
