param(
    [string[]]$Extensions = @(".h", ".hpp", ".hh", ".hxx", ".c", ".cc", ".cpp", ".cxx")
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot

function Get-LineStats([string[]]$Directories) {
    $stats = [ordered]@{
        Files = 0
        Lines = 0
        Blank = 0
        LOC = 0
    }

    foreach ($directory in $Directories) {
        $path = Join-Path $RepoRoot $directory
        if (-not (Test-Path $path)) {
            continue
        }

        $files = Get-ChildItem -Path $path -Recurse -File |
            Where-Object { $Extensions -contains $_.Extension.ToLowerInvariant() }

        foreach ($file in $files) {
            $lines = Get-Content -LiteralPath $file.FullName
            $blankLines = ($lines | Where-Object { [string]::IsNullOrWhiteSpace($_) }).Count

            $stats.Files += 1
            $stats.Lines += $lines.Count
            $stats.Blank += $blankLines
            $stats.LOC += ($lines.Count - $blankLines)
        }
    }

    [pscustomobject]$stats
}

$mainStats = Get-LineStats @("inc", "src")
$testStats = Get-LineStats @("tests")

$rows = @(
    [pscustomobject]@{
        Scope = "inc + src"
        Files = $mainStats.Files
        LOC = $mainStats.LOC
        Lines = $mainStats.Lines
        Blank = $mainStats.Blank
    },
    [pscustomobject]@{
        Scope = "tests"
        Files = $testStats.Files
        LOC = $testStats.LOC
        Lines = $testStats.Lines
        Blank = $testStats.Blank
    }
)

Write-Host "Counting C/C++ lines in: $RepoRoot" -ForegroundColor Cyan
$rows | Format-Table -AutoSize
