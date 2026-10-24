# Dev check of the first-run setup: a fresh folder with only what a release has beside the program (its DLLs, its
# import library, the bundled compiler, the randomizer's generator), started on the disc: the setup builds both game
# modules, then the game starts (and is stopped once it does).
param([string]$Dir = "build\setuptest")
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$src = "build\windows-clang"
if (Test-Path $Dir) { cmd /c rmdir /s /q (Resolve-Path $Dir).Path }
New-Item -ItemType Directory -Force $Dir | Out-Null
foreach ($f in 'boat-of-daphnes.exe','boat-of-daphnes.lib','SDL3.dll','webgpu_dawn.dll','dxcompiler.dll','dxil.dll','libpng16.dll','zlib1.dll') {
    Copy-Item "$src\$f" "$Dir\$f"
}
New-Item -ItemType Directory -Force "$Dir\tools" | Out-Null
New-Item -ItemType Junction -Path "$Dir\tools\llvm" -Target $env:BOD_LLVM  # a folder with clang.exe and lld-link.exe (set BOD_LLVM) | Out-Null
New-Item -ItemType Directory -Force "$Dir\rando" | Out-Null
foreach ($d in 'python','site-packages','wwrando','gclib') { New-Item -ItemType Junction -Path "$Dir\rando\$d" -Target "$root\build\windows\rando_runtime\$d" | Out-Null }
New-Item -ItemType Directory -Force "$Dir\assets" | Out-Null
Copy-Item -Recurse "$src\assets\fonts" "$Dir\assets\fonts"
foreach ($f in 'generate_seed.py','patch_game.py','describe_options.py','prepare_extras.py','link_colors.py','swift_sail.py','swift_sail_data.json','variant_sites.py') {
    Copy-Item "tools\rando\$f" "$Dir\rando\$f"
}
Copy-Item "$src\rando\reference-options.json" "$Dir\rando\"
Copy-Item "$src\rando\manifest.json" "$Dir\rando\"
$p = Start-Process -FilePath "$Dir\boat-of-daphnes.exe" -ArgumentList "--iso `"$root\build\windows-clang\game\GZLE01.iso`"" -WorkingDirectory $Dir -PassThru
$null = $p.Handle
while (-not $p.HasExited) {
    Start-Sleep 20
    if (Test-Path "$Dir\tww_game_rando.txt") { Start-Sleep 30; $p.Kill(); break }
}
Get-Content "$Dir\setup.log" -Tail 25
Get-ChildItem "$Dir\*.dll", "$Dir\*.txt", "$Dir\*.diff", "$Dir\game\rando" -ErrorAction SilentlyContinue | Select-Object Name, Length
