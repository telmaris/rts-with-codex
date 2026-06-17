param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $RepoRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT } else { Join-Path $WorkspaceRoot "work\local\raylib" }

$RaylibConfig = Join-Path $RaylibRoot "lib\cmake\raylib"
$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

cmake -S $RepoRoot -B (Join-Path $RepoRoot "build") `
    -Draylib_DIR=$RaylibConfig `
    -Draylib_INCLUDE_DIR=$RaylibInclude `
    -Draylib_LIBRARY=$RaylibLibrary

cmake --build (Join-Path $RepoRoot "build") --config $Config

$Exe = Join-Path $RepoRoot "build\$Config\rts.exe"
if (-not (Test-Path $Exe)) {
    throw "Build finished, but executable was not found: $Exe"
}

Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe)
