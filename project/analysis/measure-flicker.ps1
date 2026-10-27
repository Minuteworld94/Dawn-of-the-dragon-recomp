# Measures frame-to-frame brightness of the running game window, so flicker can be judged
# objectively instead of by eye. Captures a small centre patch many times and reports the
# luminance sequence plus how much it swings.
#   .\measure-flicker.ps1 [-Samples 60] [-Patch 240]
# The game must already be running and its window VISIBLE (not minimised or fully covered).
param([int]$Samples = 60, [int]$Patch = 240)

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
'@
[void][Win]::SetProcessDPIAware()

$proc = Get-Process spyrodotd -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Error "spyrodotd is not running (or has no window). Start it first."; exit 1 }
$h = $proc.MainWindowHandle
[void][Win]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 600

$rc = New-Object Win+RECT
[void][Win]::GetClientRect($h, [ref]$rc)
$origin = New-Object Win+POINT
[void][Win]::ClientToScreen($h, [ref]$origin)
$w = $rc.R - $rc.L; $hh = $rc.B - $rc.T
if ($w -le 0 -or $hh -le 0) { Write-Error "window has no client area"; exit 1 }
$side = [Math]::Min($Patch, [Math]::Min($w, $hh))
$x = $origin.X + [int](($w - $side) / 2)
$y = $origin.Y + [int](($hh - $side) / 2)
Write-Host "client ${w}x${hh}, sampling ${side}x${side} patch at ($x,$y), $Samples frames"

$bmp = New-Object Drawing.Bitmap $side, $side
$g = [Drawing.Graphics]::FromImage($bmp)
$lums = New-Object System.Collections.Generic.List[double]
$times = New-Object System.Collections.Generic.List[double]
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $Samples; $i++) {
    $g.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
    $data = $bmp.LockBits((New-Object Drawing.Rectangle 0, 0, $side, $side),
        [Drawing.Imaging.ImageLockMode]::ReadOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $side)
    [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $sum = 0.0
    for ($p = 0; $p -lt $bytes.Length; $p += 64) {       # every 16th pixel is plenty
        $sum += 0.114 * $bytes[$p] + 0.587 * $bytes[$p + 1] + 0.299 * $bytes[$p + 2]
    }
    $lums.Add($sum / ($bytes.Length / 64))
    $times.Add($sw.Elapsed.TotalMilliseconds)
}
$g.Dispose(); $bmp.Dispose()

$mean = ($lums | Measure-Object -Average).Average
$interval = ($times[$times.Count - 1] - $times[0]) / ($times.Count - 1)

# Flicker alternates every frame, so NEIGHBOURING captures differ while captures two apart
# agree. Moving content changes both equally. The ratio separates the two.
$adj = (1..($lums.Count - 1) | ForEach-Object { [Math]::Abs($lums[$_] - $lums[$_ - 1]) } |
    Measure-Object -Average).Average
$skip = (2..($lums.Count - 1) | ForEach-Object { [Math]::Abs($lums[$_] - $lums[$_ - 2]) } |
    Measure-Object -Average).Average
$ratio = $adj / [Math]::Max($skip, 0.0001)

Write-Host ("mean luminance {0:N2}   capture interval {1:N1} ms" -f $mean, $interval)
Write-Host ("neighbouring-frame change {0:N3} ({1:N2}% of mean)" -f $adj, (100 * $adj / [Math]::Max($mean, 0.01)))
Write-Host ("two-apart change          {0:N3}" -f $skip)
Write-Host ("ALTERNATION RATIO {0:N2}   (~1 = no flicker, >1.5 = per-frame alternation)" -f $ratio)
Write-Host "sequence: $(($lums | Select-Object -First 20 | ForEach-Object { '{0:N1}' -f $_ }) -join ' ')"
