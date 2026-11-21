# Builds TOOLS\kxe\kxe_unpack.exe (research tool, WORK_LOG 154). Needs the toolchain (TOOLS\env.ps1)
# and the built reXglue SDK (its rexruntime.lib / .dll provide lzx_decompress).
# Run it with the game build folder on PATH (rexruntime.dll):  TOOLS\kxe\run.ps1 <args>
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. "$root\TOOLS\env.ps1" | Out-Null
$lib = "$root\TOOLS\src\rexglue-sdk\out\win-amd64\rexruntime.lib"
& clang-cl /nologo /O2 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS "$PSScriptRoot\kxe_unpack.cpp" "/Fe:$PSScriptRoot\kxe_unpack.exe" "/Fo:$PSScriptRoot\kxe_unpack.obj" /link $lib
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Remove-Item "$PSScriptRoot\kxe_unpack.obj" -ErrorAction SilentlyContinue
"built $PSScriptRoot\kxe_unpack.exe"
