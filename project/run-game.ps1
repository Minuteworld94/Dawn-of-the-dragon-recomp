# Launches the recompiled game, pointing it at the extracted game folder (read-only mount)
# and keeping saves/cache/logs in project\run. Usage:  .\project\run-game.ps1 [-Seconds 40] [-Name boot3]
#   -Seconds N : wait N seconds, then print the log tail (the game keeps running if still alive)
#   -Name X    : log file name  -> project\run\logs\X.log
#   -UserDir D : saves/profile folder name under project\run (default "user"; test runs use
#                "testuser", a copy, so scripted input never touches the player's saves)
param([int]$Seconds = 0, [string]$Name = ("run_" + (Get-Date -Format 'HHmmss')), [string[]]$Extra = @(),
      [string]$UserDir = "user")

$root = Split-Path $PSScriptRoot -Parent
$gameDir = Get-ChildItem "$root\extracted game" -Directory | Select-Object -First 1 -ExpandProperty FullName
$run = "$PSScriptRoot\run"
$exe = "$PSScriptRoot\spyro_recomp\out\build\win-amd64-release\spyrodotd.exe"
New-Item -ItemType Directory -Force "$run\$UserDir", "$run\logs" | Out-Null

# Start-Process in Windows PowerShell 5.1 does not quote arguments, so build the line by hand.
$argLine = '--gpu_plugin xenos --game_data_root "{0}" --user_data_root "{1}\{3}" --log_level info --log_file "{1}\logs\{2}.log" --fullscreen=false' -f $gameDir, $run, $Name, $UserDir
if ($Extra) { $argLine += ' ' + ($Extra -join ' ') }

$p = Start-Process -FilePath $exe -ArgumentList $argLine -WorkingDirectory $run -PassThru
Write-Host "started pid $($p.Id), log: $run\logs\$Name.log"
if ($Seconds -gt 0) {
    $null = $p.WaitForExit($Seconds * 1000)
    if ($p.HasExited) { Write-Host ("exited, code 0x{0:X8}" -f $p.ExitCode) } else { Write-Host "still running after $Seconds s" }
    Get-Content "$run\logs\$Name.log" -Tail 25 -ErrorAction SilentlyContinue
}
