# build-windows.ps1 — Windows build helper for n0s-ryo-miner
#
# Usage:
#   .\scripts\build-windows.ps1 [-CudaEnable] [-OpenclEnable] [-BuildType Release]
#
# Prerequisites:
#   - Visual Studio 2022 (or Build Tools) with C++ workload
#   - CMake 3.18+ (ships with VS)
#   - CUDA Toolkit 11.8+ (if -CudaEnable)
#   - vcpkg (auto-bootstrapped if VCPKG_ROOT not set)
#   - OpenCL SDK or GPU driver (if -OpenclEnable)

param(
    [switch]$CudaEnable,
    [switch]$OpenclEnable,
    [string]$BuildType = "Release",
    [string]$CudaArch = "61;75;80;86;89",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Push-Location $ProjectRoot

Write-Host ""
Write-Host "  ╔═══════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "  ║       n0s-ryo-miner Windows Build         ║" -ForegroundColor Cyan
Write-Host "  ╚═══════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

# ─── vcpkg setup ──────────────────────────────────────────────────────────────

if (-not $env:VCPKG_ROOT) {
    $DefaultVcpkg = Join-Path $env:USERPROFILE "vcpkg"
    if (Test-Path (Join-Path $DefaultVcpkg "vcpkg.exe")) {
        $env:VCPKG_ROOT = $DefaultVcpkg
        Write-Host "  Found vcpkg at $DefaultVcpkg" -ForegroundColor Green
    } else {
        Write-Host "  vcpkg not found. Set VCPKG_ROOT or install to ~/vcpkg" -ForegroundColor Red
        Write-Host "  Quick setup:" -ForegroundColor Yellow
        Write-Host "    git clone https://github.com/microsoft/vcpkg ~/vcpkg"
        Write-Host "    ~/vcpkg/bootstrap-vcpkg.bat"
        Write-Host '    $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"'
        Pop-Location
        exit 1
    }
}

$VcpkgToolchain = Join-Path $env:VCPKG_ROOT "scripts" "buildsystems" "vcpkg.cmake"
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Host "  vcpkg toolchain not found at $VcpkgToolchain" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ─── Generate embedded GUI assets ────────────────────────────────────────────

Write-Host "  Generating embedded GUI assets..." -ForegroundColor Yellow
# Use PowerShell-native asset embedding for Windows
$GuiDir = Join-Path $ProjectRoot "gui"
$OutputHpp = Join-Path $ProjectRoot "n0s" "http" "embedded_assets.hpp"

if (Test-Path (Join-Path $ProjectRoot "scripts" "embed_assets.ps1")) {
    & powershell -File (Join-Path $ProjectRoot "scripts" "embed_assets.ps1") $GuiDir $OutputHpp
} elseif (Get-Command bash -ErrorAction SilentlyContinue) {
    & bash (Join-Path $ProjectRoot "scripts" "embed_assets.sh") $GuiDir $OutputHpp
} else {
    Write-Host "  WARNING: No embed script available. Using existing embedded_assets.hpp" -ForegroundColor Yellow
}

# ─── Clean build directory ────────────────────────────────────────────────────

$BuildDir = Join-Path $ProjectRoot "build"
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "  Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# ─── CMake configure ─────────────────────────────────────────────────────────

$CmakeArgs = @(
    "-G", "Ninja",
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain",
    "-DVCPKG_TARGET_TRIPLET=x64-windows-static",
    "-DCMAKE_LINK_STATIC=ON",
    "-DN0S_COMPILE=generic",
    "-DHWLOC_ENABLE=OFF"
)

if ($CudaEnable) {
    $CmakeArgs += "-DCUDA_ENABLE=ON"
    $CmakeArgs += "-DCUDA_ARCH=$CudaArch"
    Write-Host "  CUDA: ON (architectures: $CudaArch)" -ForegroundColor Green
} else {
    $CmakeArgs += "-DCUDA_ENABLE=OFF"
    Write-Host "  CUDA: OFF" -ForegroundColor DarkGray
}

if ($OpenclEnable) {
    $CmakeArgs += "-DOpenCL_ENABLE=ON"
    Write-Host "  OpenCL: ON" -ForegroundColor Green
} else {
    $CmakeArgs += "-DOpenCL_ENABLE=OFF"
    Write-Host "  OpenCL: OFF" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "  Configuring with CMake..." -ForegroundColor Yellow
& cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "  CMake configure failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ─── Build ────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "  Building..." -ForegroundColor Yellow
$NumProcs = (Get-CimInstance Win32_Processor).NumberOfLogicalProcessors
if (-not $NumProcs) { $NumProcs = 4 }
& cmake --build $BuildDir -j $NumProcs
if ($LASTEXITCODE -ne 0) {
    Write-Host "  Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ─── Verify ──────────────────────────────────────────────────────────────────

$Binary = Join-Path $BuildDir "bin" "n0s-ryo-miner.exe"
if (Test-Path $Binary) {
    $Size = (Get-Item $Binary).Length / 1MB
    Write-Host ""
    Write-Host "  ✓ Build successful!" -ForegroundColor Green
    Write-Host "    Binary: $Binary" -ForegroundColor Cyan
    Write-Host "    Size:   $([math]::Round($Size, 1)) MB" -ForegroundColor Cyan
    & $Binary --version
} else {
    Write-Host "  Binary not found at $Binary" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location
