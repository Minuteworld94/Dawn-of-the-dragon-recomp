# A/B tests renderer settings against the flicker, with no human in the loop.
# For each setting: launch the game, wait for the title screen, measure the alternation
# ratio, close it. Compare the ratios at the end.
#   .\flicker-ab-test.ps1
param([int]$WaitSeconds = 30, [int]$Samples = 120)

$proj = Split-Path $PSScriptRoot -Parent
$cases = @(
    @{ name = 'baseline';                 args = @() },
    @{ name = 'gamma_rt_unorm16=false';   args = @('--gamma_render_target_as_unorm16=false') },
    @{ name = 'present_non_ui=false';     args = @('--host_present_from_non_ui_thread=false') },
    @{ name = 'vsync=false';              args = @('--vsync=false') },
    @{ name = 'present_dither=true';      args = @('--present_dither=true') }
)

$results = @()
foreach ($c in $cases) {
    Get-Process spyrodotd -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep 2
    Write-Host "`n=== $($c.name)"
    & "$proj\run-game.ps1" -Name ("flick_" + ($c.name -replace '[^A-Za-z0-9]', '_')) -Extra $c.args | Out-Null
    Start-Sleep $WaitSeconds
    $out = & "$PSScriptRoot\measure-flicker.ps1" -Samples $Samples -Patch 600 2>&1
    $out | ForEach-Object { Write-Host "   $_" }
    $line = $out | Where-Object { $_ -match 'ALTERNATION RATIO ([\d.]+)' }
    $ratio = if ($line -match 'ALTERNATION RATIO ([\d.]+)') { [double]$Matches[1] } else { $null }
    $results += [pscustomobject]@{ Setting = $c.name; Ratio = $ratio }
}
Get-Process spyrodotd -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host "`n==== SUMMARY (lower ratio = less per-frame alternation)"
$results | Format-Table -AutoSize
