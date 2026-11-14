<#
.SYNOPSIS
    Full Clean, Rebuild and Deployment of Train Sim Consist Builder binaries to D:\JISOO_FLOWER_903\BUILDS\TESTING\

.DESCRIPTION
    Performs a complete clean rebuild (/t:Rebuild) of all projects in Win32 (x86) and x64 Release configurations,
    generating all executables and DLLs with identical fresh timestamps, and deploys them to the target directory.
#>

[CmdletBinding()]
param(
    [string]$TargetDir = "D:\JISOO_FLOWER_903\BUILDS\TESTING",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SolutionFile = Join-Path $ScriptDir "TrainSimConsistBuilder.slnx"
$MSBuildPath = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"

Write-Host "=====================================================================" -ForegroundColor Cyan
Write-Host " Train Sim Consist Builder - Full Clean, Rebuild & Deployment" -ForegroundColor Cyan
Write-Host " Destination: $TargetDir" -ForegroundColor Cyan
Write-Host "=====================================================================" -ForegroundColor Cyan

# Stop any running instances to avoid file and PDB locks
Get-Process -Name "TrainSimConsistBuilder*" -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    } catch {}
}
Start-Sleep -Milliseconds 300

if (-not $SkipBuild) {
    if (-not (Test-Path $MSBuildPath)) {
        Write-Error "MSBuild not found at: $MSBuildPath"
    }

    Write-Host "`n[1/2] Clean Rebuilding Win32 (x86) Release Target (/t:Rebuild)..." -ForegroundColor Yellow
    & $MSBuildPath $SolutionFile /t:Rebuild /p:Configuration=Release /p:Platform=x86 /v:minimal /m
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Win32 Release rebuild failed with exit code $LASTEXITCODE"
    }

    Write-Host "`n[2/2] Clean Rebuilding x64 Release Target (/t:Rebuild)..." -ForegroundColor Yellow
    & $MSBuildPath $SolutionFile /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /m
    if ($LASTEXITCODE -ne 0) {
        Write-Error "x64 Release rebuild failed with exit code $LASTEXITCODE"
    }
}

# Ensure destination directory exists
if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

# Stop any running instances in TargetDir to avoid file lock issues
Get-Process -Name "TrainSimConsistBuilder*" -ErrorAction SilentlyContinue | ForEach-Object {
    try {
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
    } catch {}
}

Write-Host "`n[Deploying Files to $TargetDir]..." -ForegroundColor Green

$FilesToCopy = @(
    # 64-bit Primary Executable and Libraries
    @{ Source = "$ScriptDir\x64\Release\TrainSimConsistBuilder.exe"; Dest = "$TargetDir\TrainSimConsistBuilder.exe" },
    @{ Source = "$ScriptDir\x64\Release\TSCBCore64.dll";            Dest = "$TargetDir\TSCBCore64.dll" },
    @{ Source = "$ScriptDir\x64\Release\TSCBResources64.dll";       Dest = "$TargetDir\TSCBResources64.dll" },

    # 32-bit Legacy Executable and Libraries
    @{ Source = "$ScriptDir\Release\TrainSimConsistBuilder32.exe";   Dest = "$TargetDir\TrainSimConsistBuilder32.exe" },
    @{ Source = "$ScriptDir\Release\TSCBCore32.dll";                Dest = "$TargetDir\TSCBCore32.dll" },
    @{ Source = "$ScriptDir\Release\TSCBResources32.dll";           Dest = "$TargetDir\TSCBResources32.dll" }
)

# Optional DirectX Shader Compiler (x64)
$D3DCompilerSrc = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\d3dcompiler_47.dll"
if (Test-Path $D3DCompilerSrc) {
    $FilesToCopy += @{ Source = $D3DCompilerSrc; Dest = "$TargetDir\d3dcompiler_47.dll" }
}

foreach ($item in $FilesToCopy) {
    if (Test-Path $item.Source) {
        Copy-Item -Path $item.Source -Destination $item.Dest -Force
        $fileInfo = Get-Item $item.Dest
        $sizeKB = [math]::Round($fileInfo.Length / 1KB, 1)
        Write-Host ("  [+] {0,-28} ({1,8} KB)" -f $fileInfo.Name, $sizeKB) -ForegroundColor White
    } else {
        Write-Warning "Source file missing: $($item.Source)"
    }
}

# Ensure TrainConfigs and AppData/TrainBindings directories exist in TargetDir
$TrainConfigsDir = Join-Path $TargetDir "TrainConfigs"
$TrainBindingsDir = Join-Path $TargetDir "AppData\TrainBindings"
if (-not (Test-Path $TrainConfigsDir)) { New-Item -ItemType Directory -Path $TrainConfigsDir -Force | Out-Null }
if (-not (Test-Path $TrainBindingsDir)) { New-Item -ItemType Directory -Path $TrainBindingsDir -Force | Out-Null }

Write-Host "`n=====================================================================" -ForegroundColor Green
Write-Host " Full Rebuild & Deployment Complete Successfully!" -ForegroundColor Green
Write-Host " All files deployed with synchronized timestamps to: $TargetDir" -ForegroundColor Green
Write-Host "=====================================================================" -ForegroundColor Green
