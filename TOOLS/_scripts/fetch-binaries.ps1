# Downloads the portable binary toolchain into TOOLS\. Re-runnable: skips anything already extracted.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = 'Tls12'

$Tools = Split-Path $PSScriptRoot -Parent
$Dl = Join-Path $Tools '_downloads'
New-Item -ItemType Directory -Force $Dl | Out-Null

function Get-File($url, $name) {
    $out = Join-Path $Dl $name
    if (-not (Test-Path $out)) {
        Write-Host "downloading $name"
        Invoke-WebRequest $url -OutFile "$out.part" -UseBasicParsing -Headers @{ 'User-Agent' = 'ps' }
        Move-Item "$out.part" $out
    }
    $out
}

function Expand-To($archive, $dest) {
    if (Test-Path $dest) { Write-Host "skip $dest"; return }
    Write-Host "extracting -> $dest"
    $tmp = "$dest._tmp"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null
    & $script:SevenZ x $archive "-o$tmp" -y | Out-Null
    if ($LASTEXITCODE) { throw "7z failed on $archive" }
    # .tar.zst / .tar.xz: unpack the inner tar
    $inner = Get-ChildItem $tmp
    if ($inner.Count -eq 1 -and $inner[0].Extension -eq '.tar') {
        $tar = $inner[0].FullName
        & $script:SevenZ x $tar "-o$tmp" -y | Out-Null
        Remove-Item $tar
        $inner = Get-ChildItem $tmp
    }
    # Flatten a single top-level folder
    if ($inner.Count -eq 1 -and $inner[0].PSIsContainer) {
        Move-Item $inner[0].FullName $dest
        Remove-Item $tmp
    } else {
        Move-Item $tmp $dest
    }
}

# 7-Zip first (bootstraps everything else)
$sevenDir = Join-Path $Tools '7zip'
if (-not (Test-Path "$sevenDir\7za.exe")) {
    New-Item -ItemType Directory -Force $sevenDir | Out-Null
    $zr = Get-File 'https://www.7-zip.org/a/7zr.exe' '7zr.exe'
    $extra = Get-File 'https://github.com/ip7z/7zip/releases/download/26.05/7z2605-extra.7z' '7z2605-extra.7z'
    & $zr x $extra "-o$sevenDir" -y | Out-Null
}
$script:SevenZ = Get-ChildItem $sevenDir -Recurse -Filter 7za.exe | Where-Object { $_.DirectoryName -match 'x64' } | Select-Object -First 1 -ExpandProperty FullName
if (-not $script:SevenZ) { $script:SevenZ = "$sevenDir\7za.exe" }

$items = @(
    @{ url = 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.3/MinGit-2.56.0.3-64-bit.zip'; name = 'MinGit-2.56.0.3-64-bit.zip'; dest = 'git' }
    @{ url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip'; name = 'cmake-4.4.4-windows-x86_64.zip'; dest = 'cmake' }
    @{ url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'; name = 'ninja-win.zip'; dest = 'ninja' }
    @{ url = 'https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.4/clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz'; name = 'clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.xz'; dest = 'llvm' }
    @{ url = 'https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip'; name = 'dxc_2026_09_29.zip'; dest = 'dxc' }
    @{ url = 'https://github.com/rexglue/rexglue-sdk/releases/download/v0.10.0/rexglue-sdk-0.10.0-win-amd64.zip'; name = 'rexglue-sdk-0.10.0-win-amd64.zip'; dest = 'rexglue-sdk-0.10.0-prebuilt' }
    @{ url = 'https://github.com/xenia-canary/xenia-canary-releases/releases/download/02d2cb5/xenia_canary_windows_.zip'; name = 'xenia_canary_windows.zip'; dest = 'xenia-canary-reference' }
    @{ url = 'https://github.com/XboxDev/extract-xiso/releases/download/build-202609111233/extract-xiso-Win64_Release.zip'; name = 'extract-xiso-Win64_Release.zip'; dest = 'extract-xiso' }
    # Pinned to 12.1.3 because XEXLoaderWV's latest build targets exactly that version
    @{ url = 'https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.3_build/ghidra_12.1.3_PUBLIC_20260817.zip'; name = 'ghidra_12.1.3_PUBLIC_20260817.zip'; dest = 'ghidra' }
    @{ url = 'https://github.com/zeroKilo/XEXLoaderWV/releases/download/12.1.3/ghidra_12.1.3_PUBLIC_20260920_XEXLoaderWV.zip'; name = 'XEXLoaderWV.zip'; dest = 'ghidra-XEXLoaderWV' }
    @{ url = 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse'; name = 'temurin-jdk21-win-x64.zip'; dest = 'jdk21' }
    @{ url = 'https://renderdoc.org/stable/1.46/RenderDoc_1.46_64.zip'; name = 'RenderDoc_1.46_64.zip'; dest = 'renderdoc' }
)

$only = $args
foreach ($i in $items) {
    if ($only -and ($only -notcontains $i.dest)) { continue }
    try {
        $a = Get-File $i.url $i.name
        Expand-To $a (Join-Path $Tools $i.dest)
    } catch {
        Write-Warning "FAILED $($i.dest): $($_.Exception.Message)"
    }
}
Write-Host 'done'
