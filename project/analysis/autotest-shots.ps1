# Runs the game in test mode (SPYRO_AUTOTEST=1: opens Display Options at ~15 s, the Recomp
# menu at ~20 s, highlights row 3 at ~25 s) and captures the game window with PrintWindow
# (works even when the window is behind others). PNGs (half size) go to -OutDir.
# -InputScript <file>: instead of the menu test, play a scripted controller (src\test_input.cpp,
# env SPYRO_INPUTSCRIPT; path relative to project\run or absolute).
# Usage: autotest-shots.ps1 -OutDir <folder> [-Name autotest] [-Shots 19,25,30] [-InputScript f]
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Name = "autotest",
    [int[]]$Shots = @(19, 25, 30),
    [string]$InputScript = "",
    [int]$Renderer = 2,  # 0 Xenos, 1 Xenos - Vulkan, 2 plume
    [int]$Resolution = 0, # 0 720p, 1 1080p, 2 1440p, 3 4K
    [int]$MinimizeAt = 0,  # seconds: minimise the game window then restore it 3 s later (Alt-Tab test)
    [int]$DisplayMode = 0,  # 0 windowed, 1 borderless, 2 fullscreen
    [int]$FrameRate = 1,    # 0 30, 1 60, 2 120, 3 Unlimited
    [int]$VSync = 0,        # 0 on, 1 off
    [string]$SaveBase = "testuser_base"  # saves snapshot in project\run\tests the run starts from
)
# Test runs use their own settings file (project\run\tests\test_settings.ini, windowed) so the
# player's recomp_settings.ini is never touched.
New-Item -ItemType Directory -Force "$PSScriptRoot\..\run\tests" | Out-Null
@"
# test run settings (analysis\autotest-shots.ps1)
resolution=$Resolution
renderer=$Renderer
frame_rate=$FrameRate
vsync=$VSync
display_mode=$DisplayMode
motion_blur=0
anti_aliasing=0
online_coop=0
"@ | Set-Content "$PSScriptRoot\..\run\tests\test_settings.ini" -Encoding ascii
$env:SPYRO_SETTINGS_FILE = "tests\test_settings.ini"
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class SpyroShot {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public struct R { public int L, T, Ri, B; }
}
'@
[SpyroShot]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force $OutDir | Out-Null

# Only the game this script starts is captured and closed: a copy the player has open (their
# PIDs, noted before launch) is never touched.
$script:preexisting = @(Get-Process spyrodotd -ErrorAction SilentlyContinue | % { $_.Id })
function TestGame { Get-Process spyrodotd -ErrorAction SilentlyContinue | ? { $script:preexisting -notcontains $_.Id } | Select-Object -First 1 }

function Snap([string]$file) {
    $p = TestGame
    if (-not $p) { return "$file : game not running" }
    $r = New-Object SpyroShot+R
    [SpyroShot]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
    if ($r.Ri -le 0 -or $r.B -le 0) { return "$file : window has no picture yet" }
    $bmp = New-Object System.Drawing.Bitmap $r.Ri, $r.B
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [SpyroShot]::PrintWindow($p.MainWindowHandle, $dc, 3) | Out-Null
    $g.ReleaseHdc($dc)
    $small = New-Object System.Drawing.Bitmap $bmp, ([int]($r.Ri / 2)), ([int]($r.B / 2))
    $small.Save((Join-Path $OutDir $file), [System.Drawing.Imaging.ImageFormat]::Png)
    return "$file ($($r.Ri)x$($r.B))"
}

if ($InputScript) { $env:SPYRO_INPUTSCRIPT = $InputScript } else { $env:SPYRO_AUTOTEST = "1" }
# Test runs keep their saves in project\run\testuser, reset before every run from the fixed
# snapshot project\run\tests\testuser_base (first made as a copy of the player's saves), so
# runs on different renderers start in the same place and autosaves never pile up.
$base = "$root\project\run\tests\$SaveBase"
if (-not (Test-Path $base)) { Copy-Item "$root\project\run\user" $base -Recurse }
if (Test-Path "$root\project\run\testuser") { Remove-Item "$root\project\run\testuser" -Recurse -Force -Confirm:$false }
Copy-Item $base "$root\project\run\testuser" -Recurse
$job = Start-Job -ScriptBlock { param($r, $n, $s) & "$r\project\run-game.ps1" -Seconds $s -Name $n -UserDir testuser } `
    -ArgumentList $root, $Name, (($Shots | Measure-Object -Maximum).Maximum + 8)
Remove-Item Env:SPYRO_AUTOTEST, Env:SPYRO_INPUTSCRIPT, Env:SPYRO_SETTINGS_FILE -ErrorAction SilentlyContinue
$start = Get-Date
$events = @($Shots | % { [pscustomobject]@{ At = $_; Kind = 'shot' } })
if ($MinimizeAt -gt 0) {
    $events += [pscustomobject]@{ At = $MinimizeAt; Kind = 'min' }
    $events += [pscustomobject]@{ At = $MinimizeAt + 3; Kind = 'restore' }
}
# try/finally: the test game is closed even when a capture fails (a failed screenshot used to end
# the script and leave its game running).
try {
    foreach ($e in ($events | Sort-Object At)) {
        $wait = $e.At - ((Get-Date) - $start).TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
        $p = TestGame
        switch ($e.Kind) {
            'shot' { try { Snap "$Name`_$($e.At).png" } catch { "$Name`_$($e.At).png : capture failed ($_)" } }
            'min' { if ($p) { [SpyroShot]::ShowWindow($p.MainWindowHandle, 6) | Out-Null; "minimised at $($e.At) s" } }  # SW_MINIMIZE
            'restore' { if ($p) { [SpyroShot]::ShowWindow($p.MainWindowHandle, 9) | Out-Null; "restored at $($e.At) s" } }  # SW_RESTORE
        }
    }
} finally {
    $p = TestGame
    if ($p) { $p | Stop-Process -Force -Confirm:$false }
    Remove-Job $job -Force
}
"log: $root\project\run\logs\$Name.log"
