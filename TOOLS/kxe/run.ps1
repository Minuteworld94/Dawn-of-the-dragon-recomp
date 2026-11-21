# Runs kxe_unpack.exe with the game build folder (rexruntime.dll and friends) on PATH.
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$env:PATH = "$root\project\spyro_recomp\out\build\win-amd64-release;$env:PATH"
& "$PSScriptRoot\kxe_unpack.exe" @args
exit $LASTEXITCODE
