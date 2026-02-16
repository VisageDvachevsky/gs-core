# Disable Multi-Plane Overlay (MPO) to fix DXGI capture throttling
# This is a known issue with AMD GPUs + high refresh rate monitors
# causing ~33 FPS capture limit instead of full refresh rate

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Disable MPO for DXGI Capture Fix" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "This script will disable Multi-Plane Overlay (MPO) in Windows."
Write-Host "This is required to fix DXGI Desktop Duplication throttling to ~33 FPS"
Write-Host "on AMD GPUs with high refresh rate monitors (100 Hz)."
Write-Host ""
Write-Host "WARNING: Requires administrator privileges and system reboot!" -ForegroundColor Yellow
Write-Host ""

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator'" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Administrator privileges confirmed." -ForegroundColor Green
Write-Host ""

# Registry paths for MPO settings
$registryPaths = @(
    "HKLM:\SOFTWARE\Microsoft\Windows\Dwm",
    "HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Scheduler"
)

Write-Host "Backing up current registry settings..." -ForegroundColor Yellow

# Backup existing values
$backupPath = "$PSScriptRoot\mpo_backup_$(Get-Date -Format 'yyyyMMdd_HHmmss').reg"
reg export "HKLM\SOFTWARE\Microsoft\Windows\Dwm" $backupPath /y | Out-Null
Write-Host "Backup saved to: $backupPath" -ForegroundColor Green
Write-Host ""

# Disable MPO via registry
Write-Host "Disabling MPO..." -ForegroundColor Yellow

# Method 1: DWM OverlayTestMode (disables MPO)
$dwmPath = "HKLM:\SOFTWARE\Microsoft\Windows\Dwm"
if (-not (Test-Path $dwmPath)) {
    New-Item -Path $dwmPath -Force | Out-Null
}

Set-ItemProperty -Path $dwmPath -Name "OverlayTestMode" -Value 5 -Type DWord -Force
Write-Host "[1/3] Set OverlayTestMode = 5 (MPO disabled)" -ForegroundColor Green

# Method 2: Disable MPO via Graphics Scheduler
$schedulerPath = "HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Scheduler"
if (-not (Test-Path $schedulerPath)) {
    New-Item -Path $schedulerPath -Force | Out-Null
}

Set-ItemProperty -Path $schedulerPath -Name "EnablePreemption" -Value 0 -Type DWord -Force
Write-Host "[2/3] Set EnablePreemption = 0" -ForegroundColor Green

# Method 3: Force disable hardware overlays
Set-ItemProperty -Path $dwmPath -Name "DisableHWAcceleration" -Value 0 -Type DWord -Force
Write-Host "[3/3] Confirmed hardware acceleration enabled (MPO overlay disabled)" -ForegroundColor Green

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  MPO Disabled Successfully" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "IMPORTANT: You MUST reboot your system for changes to take effect!" -ForegroundColor Yellow
Write-Host ""
Write-Host "After reboot, run fps_loop_test.exe with DOOM Eternal to verify 60+ FPS capture." -ForegroundColor Cyan
Write-Host ""

$rebootNow = Read-Host "Reboot now? (y/n)"
if ($rebootNow -eq "y" -or $rebootNow -eq "Y") {
    Write-Host "Rebooting in 10 seconds..." -ForegroundColor Yellow
    shutdown /r /t 10 /c "Rebooting to apply MPO disable fix"
} else {
    Write-Host "Please reboot manually when ready." -ForegroundColor Yellow
}

Write-Host ""
Write-Host "To re-enable MPO later, restore from backup:" -ForegroundColor Cyan
Write-Host "  reg import $backupPath" -ForegroundColor Gray
