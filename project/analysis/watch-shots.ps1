# Screenshots of the game the PLAYER is running, every -Every seconds for -Minutes, without
# touching it (PrintWindow: no focus change, no input). Waits for the game to start.
# PNGs (half size) go to -OutDir as <Name>_<n>.png. Draw-distance research, WORK_LOG 127.
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Name = "watch",
    [double]$Every = 2,
    [double]$Minutes = 10
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public class SpyroWatch {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct R { public int L, T, Ri, B; }
}
'@
[SpyroWatch]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force $OutDir | Out-Null

$end = (Get-Date).AddMinutes($Minutes)
$n = 0
while ((Get-Date) -lt $end) {
    $p = Get-Process spyrodotd -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($p -and $p.MainWindowHandle -ne 0) {
        $r = New-Object SpyroWatch+R
        [SpyroWatch]::GetClientRect($p.MainWindowHandle, [ref]$r) | Out-Null
        if ($r.Ri -gt 0 -and $r.B -gt 0) {
            $bmp = New-Object System.Drawing.Bitmap $r.Ri, $r.B
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $dc = $g.GetHdc()
            [SpyroWatch]::PrintWindow($p.MainWindowHandle, $dc, 3) | Out-Null
            $g.ReleaseHdc($dc)
            $small = New-Object System.Drawing.Bitmap $bmp, ([int]($r.Ri / 2)), ([int]($r.B / 2))
            $small.Save((Join-Path $OutDir ("{0}_{1:D3}.png" -f $Name, $n)), [System.Drawing.Imaging.ImageFormat]::Png)
            $small.Dispose(); $g.Dispose(); $bmp.Dispose()
            $n++
        }
    }
    Start-Sleep -Milliseconds ([int]($Every * 1000))
}
"$n screenshots in $OutDir"
