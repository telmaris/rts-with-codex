param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",

    [string]$BuildDir = "build-tests",

    [switch]$List
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT }
              elseif (Test-Path (Join-Path $RepoRoot "deps\raylib\lib\raylib.lib")) { Join-Path $RepoRoot "deps\raylib" }
              else { Join-Path (Split-Path -Parent $RepoRoot) "work\local\raylib" }

$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
$RayguiInclude = Join-Path $RepoRoot "deps\raygui"
$BuildPath = Join-Path $RepoRoot $BuildDir

function Assert-LastCommandSucceeded([string]$StepName) {
    if ($LASTEXITCODE -ne 0) {
        throw "$StepName failed with exit code $LASTEXITCODE"
    }
}

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

Write-Host "Configuring tests..." -ForegroundColor Cyan
cmake -S $RepoRoot -B $BuildPath `
    -DBUILD_TESTING=ON `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary" `
    "-Draygui_INCLUDE_DIR=$RayguiInclude"
Assert-LastCommandSucceeded "CMake configure"

Write-Host "Building rts_tests ($Config)..." -ForegroundColor Cyan
cmake --build $BuildPath --config $Config --target rts_tests
Assert-LastCommandSucceeded "CMake build"

$TestExe = Join-Path $BuildPath "tests\$Config\rts_tests.exe"
if (-not (Test-Path $TestExe)) {
    throw "Test executable was not found: $TestExe"
}

if ($List) {
    Write-Host "Available tests:" -ForegroundColor Cyan
    & $TestExe --gtest_list_tests
    Assert-LastCommandSucceeded "Test listing"
}

Write-Host "Running tests..." -ForegroundColor Cyan
& $TestExe --gtest_color=yes --gtest_brief=0
Assert-LastCommandSucceeded "Test run"
