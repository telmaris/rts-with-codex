param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",

    [string]$BuildDir = "build-tests",

    [switch]$List
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $RepoRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT } else { Join-Path $WorkspaceRoot "work\local\raylib" }

$RaylibConfig = Join-Path $RaylibRoot "lib\cmake\raylib"
$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
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
    "-Draylib_DIR=$RaylibConfig" `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary"
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
