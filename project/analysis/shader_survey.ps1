# Survey: translate every captured game shader (project\run\shaders\*.bin) with XenosRecomp and
# compile each result with DXC the way XenosRecomp does; report how many pass and the most
# common errors. Usage: shader_survey.ps1 [-XenosRecomp <exe>] [-Out <dir>]
param(
    [string]$XenosRecomp = "",
    [string]$Out = ""
)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $XenosRecomp) { $XenosRecomp = "$root\TOOLS\build\XenosRecomp\XenosRecomp\XenosRecomp.exe" }
if (-not $Out) { $Out = "$root\project\analysis\shader_survey" }
$include = "$root\TOOLS\src\XenosRecomp\XenosRecomp\shader_common.h"
$dxc = "$root\TOOLS\dxc\bin\x64\dxc.exe"
New-Item -ItemType Directory -Force $Out | Out-Null
$results = @()
foreach ($f in Get-ChildItem "$root\project\run\shaders" -Filter *.bin) {
    $hlsl = Join-Path $Out ($f.BaseName + ".hlsl")
    & $XenosRecomp $f.FullName $hlsl $include *> $null
    $translated = $LASTEXITCODE -eq 0 -and (Test-Path $hlsl)
    $err = ""
    if ($translated) {
        # container flags bit 0: 1 = vertex shader, 0 = pixel shader
        $bytes = [System.IO.File]::ReadAllBytes($f.FullName)
        $target = if ($bytes[3] -band 1) { "vs_6_0" } else { "ps_6_0" }
        $msg = & $dxc -T $target -HV 2021 -all-resources-bound -Wno-ignored-attributes $hlsl -Fo NUL 2>&1
        if ($LASTEXITCODE -ne 0) {
            $err = ($msg | Where-Object { $_ -match "error:" } | Select-Object -First 1) -replace '^.*?error: ', ''
            if (-not $err) { $err = "dxc failed" }
        }
    } else { $err = "XenosRecomp failed" }
    $results += [pscustomobject]@{ Shader = $f.BaseName; Ok = ($err -eq ""); Error = $err }
}
$ok = ($results | Where-Object Ok).Count
"shaders: $($results.Count)  compiled: $ok  failed: $($results.Count - $ok)"
$results | Where-Object { -not $_.Ok } | Group-Object { $_.Error -replace "'[^']*'", "'X'" } |
    Sort-Object Count -Descending | Select-Object -First 15 |
    ForEach-Object { "{0,5}  {1}   e.g. {2}" -f $_.Count, $_.Name, $_.Group[0].Shader }
$results | Export-Csv -NoTypeInformation (Join-Path $Out "survey.csv")
