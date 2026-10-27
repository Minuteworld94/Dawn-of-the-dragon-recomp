# Assembles <root>\ship from the current build:
#   ship\release\SpyroRecomp-<version>\ + .zip   what players download (a GitHub Release)
#   ship\repo\                                  the source to push to the GitHub repository
#   ship\HOW_TO_PUBLISH.md                      the steps
# Never copies game files (ISO, default.xex, extracted data, generated code, dumps, saves).
# Usage: project\make-ship.ps1 [-Version v0.1.0-prealpha]
param([string]$Version = "v0.1.0-prealpha")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$project = $PSScriptRoot
$template = "$project\ship_template"
$build = "$project\spyro_recomp\out\build\win-amd64-release"
$tools = "$root\TOOLS"
$ship = "$root\ship"
$git = Join-Path $tools "git\cmd\git.exe"

function Copy-To([string]$from, [string]$to) {
    New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
    Copy-Item $from $to -Force
}
function Fill([string]$from, [string]$to) {  # template with {VERSION}
    New-Item -ItemType Directory -Force (Split-Path $to -Parent) | Out-Null
    [IO.File]::WriteAllText($to, ([IO.File]::ReadAllText($from) -replace '\{VERSION\}', $Version))
}

foreach ($f in "SpyroRecomp.exe", "spyrodotd.exe", "rexruntime.dll", "rexgpu-xenos.dll", "dxcompiler.dll", "dxil.dll") {
    if (-not (Test-Path "$build\$f")) { throw "$build\$f is missing - build the project first" }
}
foreach ($d in "release", "repo") { if (Test-Path "$ship\$d") { Remove-Item "$ship\$d" -Recurse -Force -Confirm:$false } }

# ---- release ----
$name = "SpyroRecomp-$Version"
$rel = "$ship\release\$name"
Copy-To "$build\SpyroRecomp.exe" "$rel\SpyroRecomp.exe"
foreach ($f in "spyrodotd.exe", "rexruntime.dll", "rexgpu-xenos.dll", "dxcompiler.dll", "dxil.dll") { Copy-To "$build\$f" "$rel\bin\$f" }
$crt = Get-ChildItem "$tools\msvc\VC\Tools\MSVC" -Directory | Sort-Object Name | Select-Object -Last 1
foreach ($f in "msvcp140.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll", "vcruntime140_1.dll") {
    Copy-To "$($crt.FullName)\bin\Hostx64\x64\$f" "$rel\bin\$f"
}
Copy-To "$project\run\spyro_shaders.bin" "$rel\bin\spyro_shaders.bin"
Fill "$template\README.txt" "$rel\README.txt"
Copy-To "$template\LICENSE" "$rel\licenses\LICENSE.txt"
Copy-To "$template\THIRD_PARTY_NOTICES.md" "$rel\licenses\THIRD_PARTY_NOTICES.md"
Copy-To "$tools\src\rexglue-sdk\LICENSE" "$rel\licenses\reXglue-sdk.txt"
Copy-To "$tools\src\plume\LICENSE" "$rel\licenses\plume.txt"
Copy-To "$tools\src\XenosRecomp\LICENSE.md" "$rel\licenses\XenosRecomp.txt"
Copy-To "$tools\dxc\LICENSE-LLVM.txt" "$rel\licenses\DirectXShaderCompiler-LLVM.txt"
Copy-To "$tools\dxc\LICENSE-MS.txt" "$rel\licenses\DirectXShaderCompiler-MS.txt"
$d3d12ma = Get-ChildItem "$tools\src\plume\contrib\D3D12MemoryAllocator" -Filter "LICENSE*" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($d3d12ma) { Copy-To $d3d12ma.FullName "$rel\licenses\D3D12MemoryAllocator.txt" }
Fill "$template\RELEASE_NOTES.md" "$ship\release\RELEASE_NOTES.md"
Compress-Archive -Path $rel -DestinationPath "$ship\release\$name.zip" -CompressionLevel Optimal -Force

# ---- repository ----
$repo = "$ship\repo"
Fill "$template\README.md" "$repo\README.md"
Copy-To "$template\LICENSE" "$repo\LICENSE"
Copy-To "$template\THIRD_PARTY_NOTICES.md" "$repo\THIRD_PARTY_NOTICES.md"
Copy-To "$template\gitignore" "$repo\.gitignore"
Copy-To "$template\gitattributes" "$repo\.gitattributes"
# Our changes to the third-party sources, as patches against the pinned commits.
New-Item -ItemType Directory -Force "$repo\patches" | Out-Null
& $git -C "$tools\src\rexglue-sdk" diff --binary "--output=$repo\patches\rexglue-sdk.patch"
& $git -C "$tools\src\plume" diff --binary "--output=$repo\patches\plume.patch"
# Toolchain scripts (the toolchain itself is downloaded by them).
foreach ($f in "env.ps1", "README.md") { Copy-To "$tools\$f" "$repo\TOOLS\$f" }
foreach ($f in "fetch-binaries.ps1", "fetch-sources.ps1", "fix-symlinks.ps1", "portable-msvc.py") {
    Copy-To "$tools\_scripts\$f" "$repo\TOOLS\_scripts\$f"
}
Copy-To "$template\apply-patches.ps1" "$repo\TOOLS\_scripts\apply-patches.ps1"
# Project sources.
foreach ($f in "PLAY.bat", "run-game.ps1", "make-ship.ps1") { Copy-To "$project\$f" "$repo\project\$f" }
Copy-Item "$template" "$repo\project\ship_template" -Recurse
foreach ($f in "CMakeLists.txt", "CMakePresets.json", "spyrodotd_manifest.toml", "functions_auto.toml") {
    Copy-To "$project\spyro_recomp\$f" "$repo\project\spyro_recomp\$f"
}
Copy-Item "$project\spyro_recomp\src" "$repo\project\spyro_recomp\src" -Recurse
Copy-Item "$project\launcher" "$repo\project\launcher" -Recurse
Copy-Item "$project\shader_recomp" "$repo\project\shader_recomp" -Recurse
# Research scripts only (not the decompiled-code dumps or memory scans).
Get-ChildItem "$project\analysis" -File | Where-Object { $_.Extension -in ".py", ".ps1" -or $_.Name -eq "D3D_FUNCTION_MAP.txt" } |
    ForEach-Object { Copy-To $_.FullName "$repo\project\analysis\$($_.Name)" }
if (Test-Path "$project\analysis\ghidra_scripts") {
    Get-ChildItem "$project\analysis\ghidra_scripts" -File -Filter "*.java" |
        ForEach-Object { Copy-To $_.FullName "$repo\project\analysis\ghidra_scripts\$($_.Name)" }
}
Get-ChildItem "$project\run\tests" -File -Filter "*.txt" | ForEach-Object { Copy-To $_.FullName "$repo\project\run\tests\$($_.Name)" }
Copy-To "$root\READ_WHEN_YOU_LOSE_CONTEXT_WORK_LOG.txt" "$repo\docs\WORK_LOG.txt"
Copy-To "$root\READ_WHEN_YOU_LOSE_CONTEXT_PROJECT_OVERVIEW.txt" "$repo\docs\PROJECT_OVERVIEW.txt"

# Safety net: nothing from the game may end up in ship\.
$bad = Get-ChildItem $ship -Recurse -File | Where-Object {
    $_.Extension -in ".iso", ".xex", ".xcp" -or $_.Name -in "image_dump.bin" -or $_.FullName -match '\\generated\\|\\extracted game\\|\\assets\\'
}
if ($bad) { throw "game files found in ship: $($bad.FullName -join ', ')" }

Fill "$template\HOW_TO_PUBLISH.md" "$ship\HOW_TO_PUBLISH.md"
$zip = Get-Item "$ship\release\$name.zip"
"release: $($zip.FullName) ($([math]::Round($zip.Length / 1MB, 1)) MB)"
"repo:    $repo ($((Get-ChildItem $repo -Recurse -File -Force).Count) files)"
