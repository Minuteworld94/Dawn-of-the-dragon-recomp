# Dot-source this to put the portable toolchain on PATH for the current PowerShell session:
#   . .\TOOLS\env.ps1
$T = $PSScriptRoot
$Msvc = Join-Path $T 'msvc'
$VcVer = (Get-ChildItem "$Msvc\VC\Tools\MSVC" | Sort-Object Name | Select-Object -Last 1).Name
$SdkVer = (Get-ChildItem "$Msvc\Windows Kits\10\Include" | Sort-Object Name | Select-Object -Last 1).Name
$Vc = "$Msvc\VC\Tools\MSVC\$VcVer"
$Kit = "$Msvc\Windows Kits\10"

$env:VCToolsInstallDir = "$Vc\"
$env:VCToolsVersion = $VcVer
$env:WindowsSdkDir = "$Kit\"
$env:WindowsSDKVersion = "$SdkVer\"
$env:WindowsSdkBinPath = "$Kit\bin\"
$env:INCLUDE = "$Vc\include;$Kit\Include\$SdkVer\ucrt;$Kit\Include\$SdkVer\shared;$Kit\Include\$SdkVer\um;$Kit\Include\$SdkVer\winrt;$Kit\Include\$SdkVer\cppwinrt"
$env:LIB = "$Vc\lib\x64;$Kit\Lib\$SdkVer\ucrt\x64;$Kit\Lib\$SdkVer\um\x64"
$env:JAVA_HOME = "$T\jdk21"
$env:VCPKG_ROOT = "$T\src\vcpkg"

$env:PATH = @(
    "$T\llvm\bin"
    "$T\cmake\bin"
    "$T\ninja"
    "$T\git\cmd"
    "$T\7zip"
    "$T\dxc\bin\x64"
    "$T\jdk21\bin"
    "$T\extract-xiso"
    "$Vc\bin\Hostx64\x64"
    "$Kit\bin\$SdkVer\x64"
    $env:PATH
) -join ';'

$env:CC = 'clang-cl'
$env:CXX = 'clang-cl'
Write-Host "Spyro recomp toolchain: clang $((& clang --version)[0] -replace '.*version ([\d.]+).*','$1'), MSVC $VcVer, SDK $SdkVer"
