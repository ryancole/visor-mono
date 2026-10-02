#Requires -Version 7
<#
.SYNOPSIS
    Saves a screenshot of the test VM's screen as a PNG, via Hyper-V.

.DESCRIPTION
    Uses Hyper-V's thumbnail API, so it needs no VM window and no input focus,
    and works whatever the guest is doing. Needs Hyper-V admin rights.

    While an Enhanced Session is connected the console shows the lock screen,
    so the thumbnail is useless; -Inside captures the signed-in user's own
    screen instead (a scheduled task in their session, see etc/vm/run.ps1).
    That one needs the saved VM login, not Hyper-V rights.

.EXAMPLE
    pwsh etc/vm/screenshot.ps1                      # -> build/vm-screen.png
    pwsh etc/vm/screenshot.ps1 -Path shot.png -Width 1920 -Height 1080
    pwsh etc/vm/screenshot.ps1 -Inside              # from within the user's session
#>
[CmdletBinding()]
param(
    [string] $Path,
    [string] $Name = 'visor-test',
    # Defaults to the guest's current resolution.
    [int] $Width,
    [int] $Height,
    # Capture from inside the signed-in user's session (works during an
    # Enhanced Session, when the console is locked).
    [switch] $Inside
)

$ErrorActionPreference = 'Stop'
if (-not $Path) {
    $Path = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'build\vm-screen.png'
}

if ($Inside) {
    $capture = @'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$b = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
[System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
$bmp.Save('C:\visor\screen.png', [System.Drawing.Imaging.ImageFormat]::Png)
"$($b.Width)x$($b.Height)"
'@
    $size = & (Join-Path $PSScriptRoot 'run.ps1') -Script $capture -Name $Name
    $session = New-PSSession -VMName $Name -Credential (Import-Clixml (Join-Path $env:LOCALAPPDATA 'visor-shell\vm-cred.xml'))
    try {
        New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
        Copy-Item -FromSession $session 'C:\visor\screen.png' $Path
    } finally {
        Remove-PSSession $session
    }
    Write-Host "Saved $Path ($size, from inside the session)"
    return
}

$vm = Get-CimInstance -Namespace root\virtualization\v2 -ClassName Msvm_ComputerSystem -Filter "ElementName='$Name'"
if (-not $vm) { throw "No VM named '$Name'." }
$settings = Get-CimAssociatedInstance -InputObject $vm -ResultClassName Msvm_VirtualSystemSettingData |
    Where-Object VirtualSystemType -eq 'Microsoft:Hyper-V:System:Realized'

if (-not $Width -or -not $Height) {
    $video = Get-CimAssociatedInstance -InputObject $vm -ResultClassName Msvm_VideoHead | Select-Object -First 1
    $Width = [int]$video.CurrentHorizontalResolution
    $Height = [int]$video.CurrentVerticalResolution
}

$service = Get-CimInstance -Namespace root\virtualization\v2 -ClassName Msvm_VirtualSystemManagementService
$result = Invoke-CimMethod -InputObject $service -MethodName GetVirtualSystemThumbnailImage -Arguments @{
    TargetSystem = $settings
    WidthPixels  = [uint16]$Width
    HeightPixels = [uint16]$Height
}
if ($result.ReturnValue -ne 0) { throw "GetVirtualSystemThumbnailImage failed ($($result.ReturnValue))." }

# The image comes back as raw RGB565.
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format16bppRgb565)
$rect = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
$data = $bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bitmap.PixelFormat)
try {
    $bytes = [byte[]]$result.ImageData
    for ($y = 0; $y -lt $Height; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy($bytes, $y * $Width * 2, [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $Width * 2)
    }
} finally {
    $bitmap.UnlockBits($data)
}
New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
$bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Host "Saved $Path (${Width}x$Height)"
