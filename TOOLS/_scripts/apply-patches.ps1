# Pins the third-party sources (fetched by fetch-sources.ps1) to the commits this project is built
# and tested with, then applies our changes from patches\*.patch. Safe to run again: a patch that
# is already applied is skipped.
$ErrorActionPreference = 'Stop'
$Tools = Split-Path $PSScriptRoot -Parent
$Root = Split-Path $Tools -Parent
$Git = Join-Path $Tools 'git\cmd\git.exe'
if (-not (Test-Path $Git)) { $Git = 'git' }

$pins = @(
    @{ Dir = 'rexglue-sdk'; Commit = 'bd833a2ab69d305f99ddf57418fc38ffec8af93d'; Patch = 'rexglue-sdk.patch' }
    @{ Dir = 'plume';       Commit = '0ce184b19270e49236afbb74052a6b267494b86b'; Patch = 'plume.patch' }
    @{ Dir = 'XenosRecomp'; Commit = '990d03b28a27b50277ee5d8d942e1c5f873869d1'; Patch = $null }
)
foreach ($p in $pins) {
    $src = Join-Path $Tools "src\$($p.Dir)"
    if (-not (Test-Path $src)) { throw "$src is missing - run TOOLS\_scripts\fetch-sources.ps1 first" }
    $patch = if ($p.Patch) { Join-Path $Root "patches\$($p.Patch)" } else { $null }
    if ($patch) {
        & $Git -C $src apply --check --reverse $patch 2>$null
        if ($LASTEXITCODE -eq 0) { Write-Host "$($p.Dir): already pinned and patched"; continue }
    }
    & $Git -C $src fetch -q origin
    & $Git -C $src checkout -q $p.Commit
    & $Git -C $src submodule update -q --init --recursive
    # Windows checkouts without symlink support get stub files instead of links.
    & (Join-Path $PSScriptRoot 'fix-symlinks.ps1') -RepoRoot $src
    if ($patch) {
        & $Git -C $src apply $patch
        if ($LASTEXITCODE -ne 0) { throw "$($p.Patch) did not apply to $($p.Dir)" }
    }
    Write-Host "$($p.Dir): $($p.Commit.Substring(0, 7))$(if ($patch) { ' + ' + $p.Patch })"
}
