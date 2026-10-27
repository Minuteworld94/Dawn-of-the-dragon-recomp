# Git on Windows without Developer Mode checks symlinks out as one-line text files holding the
# link target. This replaces each such stub (in a repo and all its submodules) with a real copy
# of the target, and marks it skip-worktree so git status stays clean.
param([Parameter(Mandatory)][string]$RepoRoot)
$Git = Join-Path (Split-Path $PSScriptRoot -Parent) 'git\cmd\git.exe'

$roots = @((Resolve-Path $RepoRoot).Path)
$roots += & $Git -C $RepoRoot submodule foreach --quiet --recursive 'pwd -W'

foreach ($root in $roots) {
    $links = & $Git -C $root ls-files -s | Where-Object { $_ -match '^120000 ' } | ForEach-Object { ($_ -split "`t", 2)[1] }
    foreach ($rel in $links) {
        $stub = Join-Path $root $rel
        if (-not (Test-Path $stub -PathType Leaf)) { continue }
        $target = (Get-Content $stub -Raw).Trim()
        $resolved = Join-Path (Split-Path $stub -Parent) $target
        if (-not (Test-Path $resolved)) { Write-Warning "unresolved link $root\$rel -> $target"; continue }
        Remove-Item $stub -Force
        Copy-Item $resolved $stub -Recurse -Force
        & $Git -C $root update-index --skip-worktree -- $rel
        Write-Host "fixed $rel"
    }
}
