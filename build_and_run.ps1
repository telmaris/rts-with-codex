param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT }
              elseif (Test-Path (Join-Path $RepoRoot "deps\raylib\lib\raylib.lib")) { Join-Path $RepoRoot "deps\raylib" }
              else { Join-Path (Split-Path -Parent $RepoRoot) "work\local\raylib" }

$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
$RayguiInclude = Join-Path $RepoRoot "deps\raygui"

function Assert-LastCommandSucceeded([string]$StepName) {
    if ($LASTEXITCODE -ne 0) {
        throw "$StepName failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

# Auto-increment the patch component on every build (MAJOR.MINOR stay human-driven).
& (Join-Path $RepoRoot "scripts\bump_version.ps1")
if (-not $?) { throw "Version bump failed" }

cmake -S $RepoRoot -B (Join-Path $RepoRoot "build") `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary" `
    "-Draygui_INCLUDE_DIR=$RayguiInclude"
Assert-LastCommandSucceeded "CMake configure"

cmake --build (Join-Path $RepoRoot "build") --config $Config
Assert-LastCommandSucceeded "CMake build"

$Exe = Join-Path $RepoRoot "build\$Config\rts.exe"
if (-not (Test-Path $Exe)) {
    throw "Build finished, but executable was not found: $Exe"
}

Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe)
