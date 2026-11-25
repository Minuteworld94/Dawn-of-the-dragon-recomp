# Assembles the Linux release (WORK_LOG 158) from the cross-compiled build (preset linux-cross-release):
#   ship\release-linux\SpyroRecomp-<version>-linux\ + .tar.gz (Linux file modes kept)
# Layout like the Windows one: SpyroRecomp (launcher) at the top, the game and its libraries in bin\.
# The SDK's Xenos GPU plugin is NOT shipped: on Linux plume (Vulkan) is the only renderer.
# Never copies game files.
# Usage: project\make-ship-linux.ps1 [-Version v0.2.6-prealpha-linux-test]
param([string]$Version = "linux-test")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$project = $PSScriptRoot
$build = "$project\spyro_recomp\out\build\linux-cross-release"
$template = "$project\ship_template"
$tools = "$root\TOOLS"
$out = "$root\ship\release-linux"
$name = "SpyroRecomp-$Version-linux"
$rel = "$out\$name"

foreach ($f in "SpyroRecomp", "spyrodotd", "libdxcompiler.so") {
    if (-not (Test-Path "$build\$f")) { throw "$build\$f is missing - build the preset linux-cross-release first" }
}
if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force "$rel\bin", "$rel\licenses" | Out-Null

Copy-Item "$build\SpyroRecomp" "$rel\SpyroRecomp"
Copy-Item "$build\spyrodotd" "$rel\bin\spyrodotd"
# Shared libraries next to the game (rpath $ORIGIN): libdxcompiler (copied into the build folder)
# and the SDK's runtime (built in the SDK's own output folder) - not the Xenos GPU plugin.
$sdk_out = "$tools\src\rexglue-sdk\out\linux-amd64"
if (-not (Test-Path "$sdk_out\librexruntime.so")) { throw "$sdk_out\librexruntime.so is missing" }
foreach ($dir in $build, $sdk_out) {
    Get-ChildItem $dir -File | Where-Object { $_.Name -match '\.so(\.|$)' -and $_.Name -notmatch 'xenos' } |
        ForEach-Object { Copy-Item $_.FullName "$rel\bin\$($_.Name)" -Force }
}
Copy-Item "$project\run\spyro_shaders.bin" "$rel\bin\spyro_shaders.bin"
[IO.File]::WriteAllText("$rel\README.txt", ([IO.File]::ReadAllText("$template\README_LINUX.txt") -replace '\{VERSION\}', $Version).Replace("`r`n", "`n"))
# The Recomp Settings with the Linux defaults, editable before the first start (WORK_LOG 164; the
# launcher points the game at this file, the game rewrites it in its own words).
[IO.File]::WriteAllText("$rel\recomp_settings.ini", [IO.File]::ReadAllText("$template\recomp_settings_linux.ini").Replace("`r`n", "`n"))
# Release notes for the GitHub release page.
if (Test-Path "$template\RELEASE_NOTES_LINUX.md") {
    [IO.File]::WriteAllText("$out\RELEASE_NOTES.md", ([IO.File]::ReadAllText("$template\RELEASE_NOTES_LINUX.md") -replace '\{VERSION\}', $Version))
}
Copy-Item "$template\LICENSE" "$rel\licenses\LICENSE.txt"
Copy-Item "$template\THIRD_PARTY_NOTICES.md" "$rel\licenses\THIRD_PARTY_NOTICES.md"
Copy-Item "$tools\src\rexglue-sdk\LICENSE" "$rel\licenses\reXglue-sdk.txt"
Copy-Item "$tools\src\plume\LICENSE" "$rel\licenses\plume.txt"
Copy-Item "$tools\src\XenosRecomp\LICENSE.md" "$rel\licenses\XenosRecomp.txt"
Copy-Item "$tools\dxc-linux\LICENSE-LLVM.txt" "$rel\licenses\DirectXShaderCompiler-LLVM.txt"
Copy-Item "$tools\dxc-linux\LICENSE-MS.txt" "$rel\licenses\DirectXShaderCompiler-MS.txt"

# Safety net: nothing from the game may end up in the package.
$bad = Get-ChildItem $rel -Recurse -File | Where-Object { $_.Extension -in ".iso", ".xex", ".xcp", ".kxe" }
if ($bad) { throw "game files found: $($bad.FullName -join ', ')" }

# The exact binaries of this version, kept outside release-linux (which the next run wipes): a crash
# stack from the player's game_output.log ("spyrodotd(+0x...)") is looked up in these (WORK_LOG 161).
$syms = "$root\ship\symbols-linux\$Version"
New-Item -ItemType Directory -Force $syms | Out-Null
Copy-Item "$rel\SpyroRecomp", "$rel\bin\spyrodotd", "$rel\bin\librexruntime.so" $syms -Force

$exes = @("SpyroRecomp", "bin/spyrodotd")
& "$tools\python\python.exe" -I "$project\package_tar.py" $rel "$out\$name.tar.gz" @exes
Get-ChildItem $rel -Recurse -File | ForEach-Object { "  {0,-40} {1,12:N0}" -f $_.FullName.Replace("$rel\", ""), $_.Length }
