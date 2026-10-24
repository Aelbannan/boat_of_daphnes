# Dev helper: a scripted new game - title, a fresh formatted card, a save file, the name, then A every
# 2 seconds (the prologue, Outset's opening) - headless, dumping a frame every -Every frames from
# -From into -Out; then a contact sheet of the frames (sheet.png there).
#   tools\newgame.ps1 [-Frames 16000] [-From 8000] [-Every 200] [-Out build\newgame]
#   -ATo: A presses only until then (0: to the end); -Extra: more presses ('frame:BUTTON,...').
#   Frames count video retraces (60 a second).
param([int]$Frames = 16000, [int]$From = 8000, [int]$Every = 200, [string]$Out = 'build\newgame', [int]$Seconds = 600,
      [int]$ATo = 0, [string]$Extra = '')
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
Remove-Item saves\MemoryCardA.raw -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force $Out -ErrorAction SilentlyContinue
New-Item -ItemType Directory $Out | Out-Null
$env:SA2_CARD_FORMATTED = '1'
$env:SA2_DUMP_DIR = (Resolve-Path $Out).Path
$env:SA2_DUMP_EVERY = "$Every"
$env:SA2_DUMP_FROM = "$From"
$p = @('1200:START')
for ($f = 1500; $f -le 2900; $f += 200) { $p += "${f}:LEFT"; $p += "$($f + 40):A" }  # card prompts: Yes
$p += '3500:START'; $p += '3560:A'                                                   # name: END
$aEnd = if ($ATo) { $ATo } else { $Frames }
for ($f = 3800; $f -le $aEnd; $f += 120) { $p += "${f}:A" }
if ($Extra) { $p += $Extra }
$env:SA2_PRESS = ($p -join ',')
& (Join-Path $PSScriptRoot 'boot.ps1') -Frames $Frames -Seconds $Seconds -NoBuild | Out-Null
foreach ($v in 'SA2_DUMP_DIR', 'SA2_DUMP_EVERY', 'SA2_DUMP_FROM', 'SA2_PRESS', 'SA2_CARD_FORMATTED') { Remove-Item "Env:$v" }
Get-Content build\boot.txt | Where-Object { $_ -notmatch 'display copy' -and $_ -match 'FATAL|CRASH|Failed|exiting|stall\] no|setSceneName' } |
    Select-Object -Last 12
python tools\rgba2png.py $Out | Out-Null
python -c "from PIL import Image; import glob,sys; fs=sorted(glob.glob(sys.argv[1]+'/frame_*.png')); fs=fs[::max(1,len(fs)//12)][:12]; print([f[-9:-4] for f in fs]); ims=[Image.open(f).resize((480,270)) for f in fs]; s=Image.new('RGB',(1920,810)); [s.paste(im,((i%4)*480,(i//4)*270)) for i,im in enumerate(ims)]; s.save(sys.argv[1]+'/sheet.png')" $Out
