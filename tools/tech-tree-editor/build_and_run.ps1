param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release",
    [switch]$NoRun
)

$ErrorActionPreference = "Stop"

$ToolRoot = $PSScriptRoot
$RepoRoot = (Resolve-Path (Join-Path $ToolRoot "..\..")).Path

# Same raylib discovery order as the game's build_and_run.ps1.
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT }
              elseif (Test-Path (Join-Path $RepoRoot "deps\raylib\lib\raylib.lib")) { Join-Path $RepoRoot "deps\raylib" }
              else { Join-Path (Split-Path -Parent $RepoRoot) "work\local\raylib" }

$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"

function Assert-LastCommandSucceeded([string]$StepName) {
    if ($LASTEXITCODE -ne 0) {
        throw "$StepName failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

$BuildDir = Join-Path $ToolRoot "build"

cmake -S $ToolRoot -B $BuildDir `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary"
Assert-LastCommandSucceeded "CMake configure"

cmake --build $BuildDir --config $Config
Assert-LastCommandSucceeded "CMake build"

$Exe = Join-Path $BuildDir "$Config\tech_tree_editor.exe"
if (-not (Test-Path $Exe)) {
    $Exe = Join-Path $BuildDir "tech_tree_editor.exe"
}
if (-not (Test-Path $Exe)) {
    throw "Build finished, but executable was not found under: $BuildDir"
}

if ($NoRun) {
    Write-Output "Built: $Exe"
    return
}

& $Exe
