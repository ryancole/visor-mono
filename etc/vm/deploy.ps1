#Requires -Version 7
<#
.SYNOPSIS
    Copies a build (visor, visor-shell, visor-wm, visor-session and the Qt runtime) into
    the test VM, and installs, restarts or removes it there.

.DESCRIPTION
    Uses PowerShell Direct, so it works without networking and even when the
    VM's desktop is black. Needs Hyper-V admin rights on the host and the login
    saved by etc/vm/save-credential.ps1.

    The build dir goes to C:\visor in the VM. Files of running processes are
    renamed aside rather than stopped first, then the shell is restarted onto
    the new build.

.EXAMPLE
    pwsh etc/vm/deploy.ps1 -Install        # first time: copy + make it the shell
    pwsh etc/vm/deploy.ps1 -Hosted         # copy + install hosted mode (Explorer stays) + start it
    pwsh etc/vm/deploy.ps1 -Hosted -Tiling # the same, with visor-wm (tiling) on
    pwsh etc/vm/deploy.ps1                 # later: copy + restart the running shell (either mode)
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

    # Also run install.ps1 -Hosted in the VM (a Run entry; Explorer stays the
    # shell, and the replace-mode override goes), then start visor-shell in
    # hosted mode on the VM's desktop.
    [Parameter(ParameterSetName = 'Deploy')]
    [switch] $Hosted,

    # With -Hosted: turn tiling (visor-wm) on in hosted mode.
    [Parameter(ParameterSetName = 'Deploy')]
    [switch] $Tiling,

    # Undo install.ps1 in the VM, stop visor-shell, and start Explorer on the
    # VM's desktop. Copies nothing.
    [Parameter(Mandatory, ParameterSetName = 'Restore')]
    [switch] $Restore,

    [string] $Name = 'visor-test'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Target = 'C:\visor'

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
            # Only the console user is in Win32_ComputerSystem; in an Enhanced
            # Session (RDP) the session list says who is active.
            foreach ($line in (quser 2>$null)) {
                if ($line -match '^\s*>?(\S+)\s+\S+\s+\d+\s+Active') { $user = $Matches[1]; break }
            }
        }
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
        foreach ($name in 'visor-session', 'visor-shell', 'visor-wm', 'visor') {
            Get-Process $name -ErrorAction SilentlyContinue | Stop-Process -Force
        }
        Start-Sleep -Milliseconds 500
    }

    # Running exes and loaded DLLs (in any subfolder) can't be overwritten,
    # but they can be renamed. Move every file about to be replaced aside so
    # the new ones can be copied in while the old processes keep running;
    # leftovers are deleted on a later deploy.
    function Move-Aside([string] $Dir, [string[]] $RelativePaths) {
        Get-ChildItem $Dir -Recurse -Filter '*.old' -ErrorAction SilentlyContinue |
            Remove-Item -Force -ErrorAction SilentlyContinue
        foreach ($relative in $RelativePaths) {
            $path = Join-Path $Dir $relative
            if (Test-Path $path -PathType Leaf) {
                $leaf = Split-Path -Leaf $path
                Rename-Item $path "$leaf.$([guid]::NewGuid().ToString('N').Substring(0, 8)).old"
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

    # A hosted visor-shell is asked to quit (it puts Explorer's taskbar back
    # and takes Visor and visor-wm with it, and visor-session sees a clean
    # exit); the ask has to come from the user's session.
    function Stop-HostedShell([string] $Dir) {
        if (-not (Get-Process visor-shell -ErrorAction SilentlyContinue)) { return }
        Start-OnDesktop "$Dir\visor-shell.exe" '--quit'
        $deadline = (Get-Date).AddSeconds(5)
        while ((Get-Date) -lt $deadline -and (Get-Process visor-shell -ErrorAction SilentlyContinue)) {
            Start-Sleep -Milliseconds 250
        }
        Stop-Visor # whatever didn't go
    }

    function Test-Installed {
        $shell = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' -Name Shell -ErrorAction SilentlyContinue).Shell
        return $shell -like '*visor-session.exe*'
    }

    function Test-InstalledHosted {
        $run = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name visor-shell -ErrorAction SilentlyContinue).'visor-shell'
        return [bool]$run
    }
}

try {
    Invoke-Command -Session $session -ScriptBlock $vmHelpers

    if ($Restore) {
        Invoke-Command -Session $session -ScriptBlock {
            if (Test-InstalledHosted) { Stop-HostedShell $using:Target }
        }
        Invoke-Command -Session $session -FilePath "$Root\etc\uninstall.ps1"
        Invoke-Command -Session $session -ScriptBlock {
            Stop-Visor | Out-Null
            # With no shell window left, a bare explorer.exe becomes the shell.
            # (Hosted, Explorer is there already.)
            if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) {
                Start-OnDesktop 'C:\Windows\explorer.exe'
            }
        }
        return
    }

    $build = Join-Path $Root "build\$Preset"
    foreach ($file in 'visor-session.exe', 'visor-shell.exe', 'visor.exe', 'Qt6Core.dll') {
        if (-not (Test-Path (Join-Path $build $file))) {
            throw "$file not found in $build. Build first: pwsh etc/build.ps1 $Preset"
        }
    }

    $wasRunning = Invoke-Command -Session $session -ScriptBlock { [bool](Get-Process visor-shell -ErrorAction SilentlyContinue) }
    # A hosted shell is stopped before the copy (a clean quit, so the
    # taskbar comes back) and started again on the new build below.
    $wasHosted = Invoke-Command -Session $session -ScriptBlock {
        if (Test-InstalledHosted) { Stop-HostedShell $using:Target; $true } else { $false }
    }

    # Everything the build put next to the exes (Qt runtime, QML modules,
    # plugins, the default config, PDBs), minus CMake/Ninja's own files.
    $skip = 'CMakeFiles', 'src', '.qt', '.rcc', 'CMakeCache.txt', 'build.ninja', 'cmake_install.cmake',
        'compile_commands.json', '.ninja_deps', '.ninja_log'
    $items = Get-ChildItem $build -Force | Where-Object { $_.Name -notin $skip -and $_.Extension -notin '.ilk', '.exp', '.lib' }
    $relative = foreach ($item in $items) {
        if ($item.PSIsContainer) {
            Get-ChildItem $item.FullName -Recurse -File | ForEach-Object { [IO.Path]::GetRelativePath($build, $_.FullName) }
        } else {
            $item.Name
        }
    }
    $relative = @($relative) + 'install.ps1', 'uninstall.ps1'
    Invoke-Command -Session $session -ScriptBlock {
        New-Item -ItemType Directory -Force $using:Target | Out-Null
        Move-Aside $using:Target $using:relative
    }
    Copy-Item -ToSession $session -Path $items.FullName -Destination $Target -Recurse -Force
    foreach ($script in 'install.ps1', 'uninstall.ps1') {
        Copy-Item -ToSession $session -Path "$Root\etc\$script" -Destination $Target -Force
    }
    Write-Host "Copied the $Preset build to $Target in '$Name'."

    if ($Install) {
        Invoke-Command -Session $session -FilePath "$Root\etc\install.ps1" -ArgumentList "$Target\visor-session.exe"
    } elseif ($Hosted) {
        # The copied script, so the switch can be passed (-ArgumentList can't).
        Invoke-Command -Session $session -ScriptBlock {
            Set-ExecutionPolicy -Scope Process Bypass -Force
            & "$using:Target\install.ps1" -Path "$using:Target\visor-session.exe" -Hosted -Tiling:$using:Tiling
        }
    }

    $installed = Invoke-Command -Session $session -ScriptBlock { Test-Installed }
    if ($Hosted -or ($wasHosted -and $wasRunning)) {
        Invoke-Command -Session $session -ScriptBlock {
            if ($using:wasRunning -and -not $using:wasHosted) {
                # A replace-mode shell was running. With the override gone,
                # Winlogon brings Explorer when it dies; give the taskbar a
                # moment before asking it to auto-hide.
                Stop-Visor
                $deadline = (Get-Date).AddSeconds(10)
                while ((Get-Date) -lt $deadline -and -not (Get-Process explorer -ErrorAction SilentlyContinue)) {
                    Start-Sleep -Milliseconds 250
                }
                if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-OnDesktop 'C:\Windows\explorer.exe' }
                Start-Sleep -Seconds 3
            }
            # As the Run entry does: visor-session supervises visor-shell.
            Start-OnDesktop "$using:Target\visor-session.exe" '--mode hosted'
        }
    } elseif ($installed -and $wasRunning) {
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
