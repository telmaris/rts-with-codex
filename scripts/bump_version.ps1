# Increments the patch (last) component of the repository-root VERSION file.
#
# Versioning policy:
#   MAJOR.MINOR -> bumped manually by a human (edit VERSION directly).
#   PATCH       -> bumped automatically on every local build by this script.
#
# Invoked by build_and_run.ps1 before configuring CMake so the produced binary
# carries the freshly bumped version. Can also be run standalone.

param(
    [string]$VersionFile = (Join-Path (Split-Path -Parent $PSScriptRoot) "VERSION")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $VersionFile)) {
    throw "VERSION file not found at: $VersionFile"
}

$raw = (Get-Content $VersionFile -Raw).Trim()
if ($raw -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
    throw "VERSION file malformed: '$raw' (expected MAJOR.MINOR.PATCH)"
}

$major = $Matches[1]
$minor = $Matches[2]
$patch = [int]$Matches[3] + 1
$new = "$major.$minor.$patch"

# Write ASCII (no BOM) so CMake file(STRINGS) reads a clean version string.
Set-Content -Path $VersionFile -Value $new -Encoding ascii

Write-Host "Version bumped: $raw -> $new"
