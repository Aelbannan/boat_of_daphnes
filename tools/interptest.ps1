# Dev helper: checks in-between frames in game. Loads the save on the card (file 1), runs Link left
# for 10 s (the camera swings), and renders game frames -From.. every -Every (presented frames) at
# t = 0, 0.5 and 1 (SA2_INTERP_TEST), side by side in <Out>\tri_<frame>.png.
#   tools\interptest.ps1 [-Out build\interpgame] [-From 1700] [-Every 25] [-NoBuild]
#   -Steps n: each dumped frame at t = 0, 1/n, ... instead (with -Every 1, a continuous sequence); -Max: dumps
param([string]$Out = 'build\interpgame', [int]$From = 1700, [int]$Every = 25, [int]$Frames = 3700, [switch]$NoBuild,
      [int]$Steps = 0, [int]$Max = 300)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $NoBuild) {
    Get-Process tww -ErrorAction SilentlyContinue | Stop-Process -Force
    & "$env:SystemRoot\System32\cmd.exe" /c (Join-Path $PSScriptRoot 'build.cmd') | Select-Object -Last 1
}
$dir = Join-Path $root $Out
if (Test-Path $dir) { Remove-Item -Recurse -Force -LiteralPath $dir }
New-Item -ItemType Directory -Force $dir | Out-Null
$p = @('1200:START', '1500:A', '1700:A', '1900:A', '2100:A', '2300:A', '2500:A')
for ($f = 3000; $f -le 3600; $f += 6) { $p += "${f}:SL" }
$env:SA2_PRESS = ($p -join ',')
$env:SA2_INTERP_TEST = '0.5'
$env:SA2_INTERP_STATS = '1'
$env:SA2_DUMP_DIR = $dir
$env:SA2_DUMP_FROM = "$From"
$env:SA2_DUMP_EVERY = "$Every"
$env:SA2_MUTE = '1'
if ($Steps) { $env:SA2_INTERP_STEPS = "$Steps" }
$env:SA2_DUMP_MAX = "$Max"
$iso = "`"$env:BOD_DISC`""  # your disc image (set BOD_DISC)
$pr = Start-Process -FilePath build\windows-clang\boat-of-daphnes.exe -ArgumentList "--iso $iso --headless --frames $Frames" -PassThru -NoNewWindow `
    -RedirectStandardOutput "$dir\log.txt"
$pr.WaitForExit(400000) | Out-Null
foreach ($v in 'SA2_PRESS', 'SA2_INTERP_TEST', 'SA2_INTERP_STATS', 'SA2_DUMP_DIR', 'SA2_DUMP_FROM', 'SA2_DUMP_EVERY', 'SA2_MUTE', 'SA2_INTERP_STEPS', 'SA2_DUMP_MAX') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
python tools\rgba2png.py $dir | Out-Null
if ($Steps) { return }
python -c @"
from PIL import Image; import glob, os, sys
d = sys.argv[1]
for f in sorted(glob.glob(os.path.join(d, 'frame_*_t05.png'))):
    n = os.path.basename(f)[6:11]
    ims = [Image.open(os.path.join(d, f'frame_{n}_t{t}.png')).convert('RGB') for t in ('00', '05', '10')]
    s = Image.new('RGB', (1920, 360))
    for i, im in enumerate(ims): s.paste(im.resize((640, 360)), (i * 640, 0))
    s.save(os.path.join(d, f'tri_{n}.png')); print(n)
"@ $dir
