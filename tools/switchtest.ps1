# Dev check of the in-place randomizer switch: boots headless, switches to the randomizer at the file select's n-th
# frame (BLUEWAKE_TEST_RANDO_SWITCH), loads the card's first file and prints what matters from the log.
#   tools\switchtest.ps1 [-Switch 30] [-Frames 2400] [-Build]
param([string]$Switch = '30', [int]$Frames = 2400, [switch]$Build)
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if ($Build) {
    & "$env:SystemRoot\System32\cmd.exe" /c "$root\tools\build_target.cmd tww" *> build\tww_build.txt
    Get-Content build\tww_build.txt | Select-String -Pattern ' error|FAILED|unresolved' | Select-Object -First 10
}
$env:BLUEWAKE_TEST_RANDO_SWITCH = $Switch
$env:SA2_MUTE = '1'
$env:BLUEWAKE_FAST_FORWARD = '0'  # (headless runs have no launch settings: Faster Transitions off, as its default)
$env:SA2_PRESS = '450:START,520:START,700:A,780:A,860:A,940:A,1020:A'
$p = Start-Process -FilePath build\windows-clang\boat-of-daphnes.exe -ArgumentList "--iso build\windows-clang\game\GZLE01.iso --headless --frames $Frames" `
    -PassThru -NoNewWindow -RedirectStandardError build\sw_err.txt -RedirectStandardOutput build\sw.txt
if (-not $p.WaitForExit(400000)) { $p.Kill(); 'timeout' }
$env:BLUEWAKE_TEST_RANDO_SWITCH = $null
$env:SA2_PRESS = $null
Get-Content build\sw.txt, build\sw_err.txt |
    Select-String -Pattern 'modswitch\] the|profile list|FATAL|assert|Failed|exiting|stage=|call to|= 00000000|linked module 1 ' |
    Select-Object -First 25
