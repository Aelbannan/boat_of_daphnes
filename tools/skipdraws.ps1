# Dev helper: runs headless captures of one frame in parallel, each dropping some draws
# (SA2_SKIP_DRAWS), and puts a crop of each side by side in build\skip_sheet.png.
#   tools\skipdraws.ps1 -At 2400 -Crop 900,250,1100,560 -Skips '4897-4908','4767-4779'
#   -Var SA2_SKIP_BLEND: drop by blend mode instead; -Serial: one run at a time (the title demo runs
#   on real time, so parallel runs reach different moments of it).
param([int]$At = 2400, [int[]]$Crop = @(900, 250, 1100, 560), [string[]]$Skips = @(''), [switch]$NoBuild,
      [string]$Var = 'SA2_SKIP_DRAWS', [switch]$Serial)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $NoBuild) {
    Get-Process tww -ErrorAction SilentlyContinue | Stop-Process -Force
    cmake --build build\windows-clang --target tww | Select-Object -Last 1
}
$iso = "`"$env:BOD_DISC`""  # your disc image (set BOD_DISC)
$procs = @()
for ($i = 0; $i -lt $Skips.Count; $i++) {
    $o = Join-Path $root "build\skip$i"
    if (Test-Path $o) { Remove-Item -Recurse -Force -LiteralPath $o }
    New-Item -ItemType Directory -Force $o | Out-Null
    Set-Item "Env:$Var" $Skips[$i]
    $env:SA2_DUMP_DIR = $o
    $env:SA2_DUMP_FROM = "$At"
    $env:SA2_DUMP_EVERY = '100000'
    $env:SA2_MUTE = '1'
    $procs += Start-Process -FilePath build\windows-clang\boat-of-daphnes.exe -ArgumentList "--iso $iso --headless --frames $($At + 10)" `
        -PassThru -NoNewWindow -RedirectStandardOutput "$o\log.txt"
    if ($Serial) { $procs[-1].WaitForExit(300000) | Out-Null }
}
foreach ($v in $Var, 'SA2_DUMP_DIR', 'SA2_DUMP_FROM', 'SA2_DUMP_EVERY', 'SA2_MUTE') { Remove-Item "Env:$v" }
$procs | ForEach-Object { $_.WaitForExit(300000) | Out-Null }
for ($i = 0; $i -lt $Skips.Count; $i++) { python tools\rgba2png.py "build\skip$i" | Out-Null }
python -c @"
from PIL import Image; import glob, sys
n, c = int(sys.argv[1]), [int(x) for x in sys.argv[2].split(',')]
w, h = c[2] - c[0], c[3] - c[1]
s = Image.new('RGB', (w * n, h))
for i in range(n):
    f = sorted(glob.glob(f'build/skip{i}/*.png'))
    if f: s.paste(Image.open(f[0]).crop(c), (i * w, 0))
s = s.resize((s.width * 2, s.height * 2), Image.NEAREST)
s.save('build/skip_sheet.png')
"@ $Skips.Count ($Crop -join ',')
