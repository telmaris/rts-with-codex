param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",

    [string]$BuildDir = "build-tests-coverage",

    [string]$OpenCppCoveragePath = "",

    [switch]$OpenReport
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
$WorkspaceRoot = Split-Path -Parent $RepoRoot
$RaylibRoot = if ($env:RAYLIB_ROOT) { $env:RAYLIB_ROOT } else { Join-Path $WorkspaceRoot "work\local\raylib" }

$RaylibConfig = Join-Path $RaylibRoot "lib\cmake\raylib"
$RaylibInclude = Join-Path $RaylibRoot "include"
$RaylibLibrary = Join-Path $RaylibRoot "lib\raylib.lib"
$BuildPath = Join-Path $RepoRoot $BuildDir

if (-not (Test-Path $RaylibLibrary)) {
    throw "raylib.lib not found. Set RAYLIB_ROOT or build raylib into: $RaylibRoot"
}

if (-not $OpenCppCoveragePath) {
    $command = Get-Command OpenCppCoverage -ErrorAction SilentlyContinue
    if ($command) {
        $OpenCppCoveragePath = $command.Source
    }
}

if (-not $OpenCppCoveragePath) {
    $defaultPath = "C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe"
    if (Test-Path $defaultPath) {
        $OpenCppCoveragePath = $defaultPath
    }
}

if (-not (Test-Path $OpenCppCoveragePath)) {
    throw "OpenCppCoverage.exe not found. Install it or pass -OpenCppCoveragePath `"C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe`""
}

Write-Host "Configuring coverage build..." -ForegroundColor Cyan
cmake -S $RepoRoot -B $BuildPath `
    -DBUILD_TESTING=ON `
    -DENABLE_COVERAGE=ON `
    "-DOPENCPPCOVERAGE_EXECUTABLE=$OpenCppCoveragePath" `
    "-Draylib_DIR=$RaylibConfig" `
    "-Draylib_INCLUDE_DIR=$RaylibInclude" `
    "-Draylib_LIBRARY=$RaylibLibrary"

Write-Host "Running coverage target ($Config)..." -ForegroundColor Cyan
cmake --build $BuildPath --config $Config --target coverage

$HtmlReport = Join-Path $BuildPath "coverage\index.html"
$XmlReport = Join-Path $BuildPath "coverage.xml"

if (-not (Test-Path $HtmlReport)) {
    throw "Coverage target finished, but HTML report was not found: $HtmlReport"
}

Write-Host ""
if (Test-Path $XmlReport) {
    [xml]$CoverageXml = Get-Content $XmlReport
    $Root = $CoverageXml.coverage
    $LineRate = [double]$Root.'line-rate'
    $LinesCovered = [int]$Root.'lines-covered'
    $LinesValid = [int]$Root.'lines-valid'
    $Percent = [math]::Round($LineRate * 100.0, 2)

    Write-Host "Coverage summary:" -ForegroundColor Cyan
    Write-Host ("  Line coverage: {0}% ({1}/{2} lines)" -f $Percent, $LinesCovered, $LinesValid)

    $Classes = @($CoverageXml.coverage.packages.package.classes.class)
    if ($Classes.Count -gt 0) {
        Write-Host ""
        Write-Host "Lowest covered files:" -ForegroundColor Cyan
        $Classes |
            Where-Object { $_.filename -and $_.lines.line.Count -gt 0 } |
            Sort-Object { [double]$_.'line-rate' } |
            Select-Object -First 10 |
            ForEach-Object {
                $filePercent = [math]::Round(([double]$_.'line-rate') * 100.0, 2)
                $lineCount = @($_.lines.line).Count
                $coveredCount = @($_.lines.line | Where-Object { [int]$_.hits -gt 0 }).Count
                Write-Host ("  {0,6}%  {1,4}/{2,-4}  {3}" -f $filePercent, $coveredCount, $lineCount, $_.name)
            }
    }

    Write-Host ""
}

Write-Host "Coverage report generated:" -ForegroundColor Green
Write-Host "  HTML: $HtmlReport"
Write-Host "  XML:  $XmlReport"

if ($OpenReport) {
    Start-Process $HtmlReport
}
