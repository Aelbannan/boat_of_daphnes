# Dev helper: a windowed run that loads the save on the card (file 1) and runs Link left for 16 s,
# with a CPU profile (-GxTiming: the game thread's timing, SA2_GX_TIMING - costly itself) (SA2_PROFILE); prints the frame
# rate lines and the profile's top functions.
#   tools\perf.ps1 [-Out build\perf] [-NoBuild] [-Extra '--no-interp']
param([string]$Out = 'build\perf', [switch]$NoBuild, [string]$Extra = '', [switch]$GxTiming)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $NoBuild) {
    Get-Process tww -ErrorAction SilentlyContinue | Stop-Process -Force
    & "$env:SystemRoot\System32\cmd.exe" /c (Join-Path $PSScriptRoot 'build.cmd') | Select-String -Pattern ' error|Linking|FAILED' | Select-Object -Last 5
}
$dir = Join-Path $root $Out
if (Test-Path $dir) { Remove-Item -Recurse -Force -LiteralPath $dir }
New-Item -ItemType Directory -Force $dir | Out-Null
$p = @('1200:START', '1500:A', '1700:A', '1900:A', '2100:A', '2300:A', '2500:A')
for ($f = 3000; $f -le 4000; $f += 6) { $p += "${f}:SL" }
$env:SA2_PRESS = ($p -join ',')
if ($GxTiming) { $env:SA2_GX_TIMING = '1' }
$env:SA2_MUTE = '1'
$env:SA2_PROFILE = '20'
$iso = "`"$env:BOD_DISC`""  # your disc image (set BOD_DISC)
$pr = Start-Process -FilePath build\windows-clang\boat-of-daphnes.exe -ArgumentList "--iso $iso --frames 4200 $Extra" -PassThru -NoNewWindow `
    -RedirectStandardOutput "$dir\log.txt"
$pr.WaitForExit(200000) | Out-Null
foreach ($v in 'SA2_PRESS', 'SA2_GX_TIMING', 'SA2_MUTE', 'SA2_PROFILE') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
Get-Content "$dir\log.txt" | Where-Object { $_ -match '\[gx\] last|\[frontend\] \d+ frames|\[interp\] timing' } | Select-Object -Last 9
Get-Content "$dir\log.txt" | Where-Object { $_ -match '\[profile\]  ' } | Select-Object -First 18
