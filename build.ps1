# build.ps1 - Build script for Matrix Tetris Screen Saver
# Usage: .\build.ps1 [-Configuration Debug|Release] [-Clean] [-Install]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [switch]$Clean,
    [switch]$Install
)

$ErrorActionPreference = "Stop"

# Find MSBuild
$msbuildPaths = @(
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Professional\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe"
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $null
foreach ($path in $msbuildPaths) {
    if (Test-Path $path) {
        $msbuild = $path
        break
    }
}

# Try vswhere as fallback
if (-not $msbuild) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        }
    }
}

if (-not $msbuild -or -not (Test-Path $msbuild)) {
    Write-Host "ERROR: MSBuild not found. Please install Visual Studio or Build Tools." -ForegroundColor Red
    exit 1
}

Write-Host "Using MSBuild: $msbuild" -ForegroundColor Cyan

# Project paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$solution = Join-Path $scriptDir "MatrixTetris.sln"
$outputDir = Join-Path $scriptDir "bin\$Configuration"

# Clean if requested
if ($Clean) {
    Write-Host "`nCleaning..." -ForegroundColor Yellow
    & $msbuild $solution /t:Clean /p:Configuration=$Configuration /p:Platform=x64 /v:minimal
    $dirsToClean = @("bin", "obj")
    foreach ($dir in $dirsToClean) {
        $fullPath = Join-Path $scriptDir $dir
        if (Test-Path $fullPath) {
            Remove-Item -Recurse -Force $fullPath
            Write-Host "Removed: $dir" -ForegroundColor Gray
        }
    }
}

# Build
Write-Host "`nBuilding Matrix Tetris Screen Saver..." -ForegroundColor Green
Write-Host "Configuration: $Configuration" -ForegroundColor Gray
Write-Host "Platform: x64" -ForegroundColor Gray
Write-Host ""

& $msbuild $solution /p:Configuration=$Configuration /p:Platform=x64 /v:minimal /m

if ($LASTEXITCODE -eq 0) {
    $scr = Join-Path $outputDir "MatrixTetris.scr"
    Write-Host "`n========================================" -ForegroundColor Green
    Write-Host "BUILD SUCCEEDED" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Output: $scr" -ForegroundColor Cyan

    if (Test-Path $scr) {
        $fileInfo = Get-Item $scr
        Write-Host "Size: $([math]::Round($fileInfo.Length / 1KB, 2)) KB" -ForegroundColor Gray

        # Also produce an .exe copy for command-line use
        # (.scr shell handler strips custom args like /m in PowerShell)
        $exe = Join-Path $outputDir "MatrixTetris.exe"
        Copy-Item -Path $scr -Destination $exe -Force
        Write-Host "Also: $exe" -ForegroundColor Cyan
    }

    # Install if requested
    if ($Install) {
        $sysDir = [System.Environment]::GetFolderPath("System")
        $dest = Join-Path $sysDir "MatrixTetris.scr"
        Write-Host "`nInstalling to: $dest" -ForegroundColor Cyan
        Write-Host "(Requires Administrator privileges)" -ForegroundColor DarkYellow
        try {
            Copy-Item -Path $scr -Destination $dest -Force
            Write-Host "Installed successfully!" -ForegroundColor Green
            Write-Host "`nTo activate: Right-click desktop -> Personalize -> Lock screen -> Screen saver settings" -ForegroundColor Gray
            Write-Host "Select 'MatrixTetris' from the dropdown" -ForegroundColor Gray
        }
        catch {
            Write-Error "Failed to install. Run as Administrator.`n$_"
            exit 1
        }
    } else {
        $exe = Join-Path $outputDir "MatrixTetris.exe"
        Write-Host "`nTo test:    & `"$exe`" /s" -ForegroundColor Yellow
        Write-Host "Monitor N:  & `"$exe`" /s /m 0" -ForegroundColor Yellow
        Write-Host "To install: .\build.ps1 -Install" -ForegroundColor Yellow
    }
} else {
    Write-Host "`n========================================" -ForegroundColor Red
    Write-Host "BUILD FAILED" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    exit 1
}
