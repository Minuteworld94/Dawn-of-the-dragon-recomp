# Rebuilds project\spyro_recomp\functions_auto.toml from scratch and prunes bad candidates.
#   1. baseline codegen with an EMPTY auto file  -> partition.json = the functions analysis found itself
#   2. scan image_dump.bin for missed function pointers -> write functions_auto.toml
#   3. codegen again; ban candidates implicated in 'Unresolved branch' reports; repeat until none new
# Needs: project\run\image_dump.bin (written by the game on first boot).  Run after sourcing TOOLS\env.ps1.
param([int]$MaxRounds = 8)
$ErrorActionPreference = 'Continue'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$proj = "$root\project\spyro_recomp"
$ana = "$root\project\analysis"
$rex = "$root\TOOLS\src\rexglue-sdk\out\win-amd64\Release\rexglue.exe"
$toml = "$proj\functions_auto.toml"
$img = "$root\project\run\image_dump.bin"
$part = "$proj\generated\default\codegen.partition.json"

function Run-Codegen($logName) {
    Push-Location $proj
    & $rex codegen spyrodotd_manifest.toml *> "$proj\logs\$logName"
    Pop-Location
}

for ($round = 1; $round -le $MaxRounds; $round++) {
    Set-Content $toml "# (empty during baseline codegen)" -Encoding ascii
    Run-Codegen "refresh-baseline.txt"
    py -3 -I "$ana\find_func_pointers.py" $img $part $toml | Select-Object -Last 3
    Run-Codegen "refresh-round$round.txt"
    $res = py -3 -I "$ana\prune_candidates.py" "$proj\logs\refresh-round$round.txt" $toml "$ana\banned_candidates.txt"
    Write-Host "round ${round}: $res"
    if ($res -match 'newly banned: 0\b') { Write-Host "converged after $round round(s)"; break }
}
