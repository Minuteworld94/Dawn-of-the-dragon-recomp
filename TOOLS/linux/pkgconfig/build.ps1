# Builds TOOLS\linux\pkgconfig\pkg-config.exe (our minimal pkg-config for the Linux cross build).
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
. "$root\TOOLS\env.ps1" | Out-Null
& clang-cl /nologo /O2 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS "$PSScriptRoot\pkg-config.cpp" "/Fe:$PSScriptRoot\pkg-config.exe" "/Fo:$PSScriptRoot\pkg-config.obj"
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Remove-Item "$PSScriptRoot\pkg-config.obj" -ErrorAction SilentlyContinue
"built $PSScriptRoot\pkg-config.exe"
