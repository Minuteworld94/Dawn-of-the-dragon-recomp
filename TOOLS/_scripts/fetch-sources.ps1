# Clones (or updates) the source repos into TOOLS\src. Re-runnable: pulls if already cloned.
$Tools = Split-Path $PSScriptRoot -Parent
$Git = Join-Path $Tools 'git\cmd\git.exe'
$Src = Join-Path $Tools 'src'
New-Item -ItemType Directory -Force $Src | Out-Null
& $Git config --global core.longpaths true

# name, url, submodules?, shallow?
$repos = @(
    ,@('rexglue-sdk',     'https://github.com/rexglue/rexglue-sdk.git',        $true,  $false)  # the SDK, main branch (bleeding edge)
    ,@('plume',           'https://github.com/renderbag/plume.git',            $true,  $false)  # RHI over D3D12/Vulkan used by Unleashed
    ,@('XenosRecomp',     'https://github.com/hedge-dev/XenosRecomp.git',      $true,  $false)  # Xenos shader microcode -> HLSL
    ,@('XenonRecomp',     'https://github.com/hedge-dev/XenonRecomp.git',      $true,  $false)  # reference PPC recompiler
    ,@('UnleashedRecomp', 'https://github.com/hedge-dev/UnleashedRecomp.git',  $false, $true)   # reference: D3D-layer replacement renderer
    ,@('xenia-canary',    'https://github.com/xenia-canary/xenia-canary.git',  $false, $true)   # reference only: Xenos/PM4 docs-in-code
    ,@('vcpkg',           'https://github.com/microsoft/vcpkg.git',            $false, $true)
)

foreach ($r in $repos) {
    $name, $url, $subs, $shallow = $r
    $dir = Join-Path $Src $name
    if (Test-Path (Join-Path $dir '.git')) {
        Write-Host "== update $name"
        & $Git -C $dir pull --ff-only
        if ($subs) { & $Git -C $dir submodule update --init --recursive --depth 1 }
        continue
    }
    Write-Host "== clone $name"
    $cloneArgs = @('clone')
    if ($shallow) { $cloneArgs += '--depth=1' }
    $cloneArgs += @($url, $dir)
    & $Git @cloneArgs
    if ($subs) { & $Git -C $dir submodule update --init --recursive --depth 1 }
}

# Bleeding edge: rexglue's newest work lands on 'development' before 'main'
& $Git -C (Join-Path $Src 'rexglue-sdk') checkout -q -B development origin/development
& $Git -C (Join-Path $Src 'rexglue-sdk') submodule update --init --recursive --depth 1

foreach ($name in 'rexglue-sdk', 'plume', 'XenosRecomp', 'XenonRecomp') {
    & (Join-Path $PSScriptRoot 'fix-symlinks.ps1') -RepoRoot (Join-Path $Src $name)
}
Write-Host 'done'
