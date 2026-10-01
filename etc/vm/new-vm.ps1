#Requires -Version 7
<#
.SYNOPSIS
    Creates the Hyper-V VM used to test visor-shell as the Windows shell.

.DESCRIPTION
    Gen 2 VM with vTPM + Secure Boot (Windows 11 requirements), booting from the
    given Windows 11 ISO. Automatic checkpoints are disabled so the "clean"
    checkpoint taken after setup is the only restore point. The Guest Service
    Interface is enabled so builds can be copied in with PowerShell Direct.

    Needs Hyper-V admin rights (an elevated prompt or membership in
    "Hyper-V Administrators"). Never modifies the host's own shell settings.

.EXAMPLE
    pwsh etc/vm/new-vm.ps1 -Iso "$env:USERPROFILE\Downloads\Windows11.iso"
    # ...install Windows 11 Pro in the VM, then:
    pwsh etc/vm/new-vm.ps1 -Checkpoint
#>
[CmdletBinding(DefaultParameterSetName = 'Create')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Create')]
    [string] $Iso,

    # Takes the "clean" checkpoint once Windows is installed in the VM.
    [Parameter(Mandatory, ParameterSetName = 'Checkpoint')]
    [switch] $Checkpoint,

    [string] $Name = 'visor-test',
    [int] $Cpus = 4,
    [long] $MemoryStartup = 4GB,
    [long] $MemoryMaximum = 8GB,
    [long] $DiskSize = 80GB
)

$ErrorActionPreference = 'Stop'

if ($Checkpoint) {
    $vm = Get-VM -Name $Name
    if (Get-VMSnapshot -VM $vm -Name 'clean' -ErrorAction SilentlyContinue) {
        throw "VM '$Name' already has a 'clean' checkpoint."
    }
    Checkpoint-VM -VM $vm -SnapshotName 'clean'
    Write-Host "Checkpoint 'clean' created for '$Name'."
    return
}

$Iso = (Resolve-Path $Iso).Path
if (Get-VM -Name $Name -ErrorAction SilentlyContinue) {
    throw "A VM named '$Name' already exists."
}

$vmRoot = Join-Path (Get-VMHost).VirtualMachinePath $Name
$vhd = Join-Path (Get-VMHost).VirtualHardDiskPath "$Name.vhdx"
if (Test-Path $vhd) {
    throw "Disk '$vhd' already exists; remove it or pick another -Name."
}

$vm = New-VM -Name $Name -Generation 2 -Path $vmRoot `
    -MemoryStartupBytes $MemoryStartup -NewVHDPath $vhd -NewVHDSizeBytes $DiskSize `
    -SwitchName 'Default Switch'

Set-VM -VM $vm -ProcessorCount $Cpus -DynamicMemory `
    -MemoryMinimumBytes 2GB -MemoryMaximumBytes $MemoryMaximum `
    -CheckpointType Standard -AutomaticCheckpointsEnabled $false `
    -AutomaticStopAction ShutDown -EnhancedSessionTransportType HvSocket

# Windows 11 requires TPM 2.0 and Secure Boot.
Set-VMKeyProtector -VM $vm -NewLocalKeyProtector
Enable-VMTPM -VM $vm
Set-VMFirmware -VM $vm -EnableSecureBoot On -SecureBootTemplate 'MicrosoftWindows'

$dvd = Add-VMDvdDrive -VM $vm -Path $Iso -Passthru
Set-VMFirmware -VM $vm -FirstBootDevice $dvd

Enable-VMIntegrationService -VM $vm -Name 'Guest Service Interface'

Start-VM -VM $vm
Write-Host @"
VM '$Name' created and started.
  1. vmconnect localhost '$Name'   (press a key quickly to boot from the ISO)
  2. Install Windows 11 Pro, "I don't have a product key", local account with a password.
  3. Install the VC++ x64 runtime: https://aka.ms/vs/17/release/vc_redist.x64.exe
  4. pwsh etc/vm/new-vm.ps1 -Checkpoint
"@
