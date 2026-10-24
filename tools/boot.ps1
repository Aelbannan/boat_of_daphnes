# Dev helper: rebuild the runtime and run the game headless for a number of frames; the log goes to
# build\boot.txt (aurora's display-copy stub notes left out of the summary printed).
#   tools\boot.ps1 [-Frames 600] [-Seconds 120] [-Window] [-Build]
param([int]$Frames = 600, [int]$Seconds = 120, [switch]$Window, [switch]$NoBuild)
$root = Split-Path -Parent $PSScriptRoot
if (-not $NoBuild) {
    . (Join-Path $PSScriptRoot 'devenv.ps1')
    Push-Location $root
    $out = cmake --build build/windows-clang --target tww 2>&1
    $code = $LASTEXITCODE
    Pop-Location
    if ($code) { $out | Select-String ' error' -Context 0, 1 | Select-Object -First 20; exit 1 }
}
$iso = $env:BOD_DISC  # your disc image (set BOD_DISC)
$args = @('--iso', "`"$iso`"", '--frames', "$Frames")
if (-not $Window) { $args += '--headless' }
$log = Join-Path $root 'build\boot.txt'
$p = Start-Process -FilePath (Join-Path $root 'build\windows-clang\boat-of-daphnes.exe') -ArgumentList $args -WorkingDirectory $root `
    -RedirectStandardOutput $log -RedirectStandardError (Join-Path $root 'build\boot_err.txt') -PassThru -NoNewWindow
if (-not $p.WaitForExit($Seconds * 1000)) { $p.Kill(); Write-Output "(stopped after $Seconds s)" }
Get-Content $log | Select-String -NotMatch 'display copy is not implemented|\[mmio\] first' | ForEach-Object { $_.Line }
