# Testing: saves PNGs of the game window itself (name tags and other overlays the frame capture of a scenario
# leaves out) every few seconds, while a scenario runs.
#   powershell -File scripts\windows\window_shots.ps1 -Out D:\shots -ExeFolder <the app folder> -Seconds 120
# Only reads a window (PrintWindow); it starts and stops nothing.
param([string]$Out, [string]$ExeFolder, [int]$Seconds = 120, [double]$Every = 3, [int]$WaitSeconds = 1500)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class WinShot {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
New-Item -ItemType Directory -Force $Out | Out-Null
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$proc = $null
while ((Get-Date) -lt $deadline -and -not $proc) {
  $proc = Get-CimInstance Win32_Process -Filter "Name='boat-of-daphnes.exe'" | Where-Object { $_.ExecutablePath -like "$ExeFolder*" } | Select-Object -First 1
  if (-not $proc) { Start-Sleep -Milliseconds 500 }
}
if (-not $proc) { "no game started"; exit 1 }
$end = (Get-Date).AddSeconds($Seconds)
$n = 0
while ((Get-Date) -lt $end) {
  $p = Get-Process -Id $proc.ProcessId -ErrorAction SilentlyContinue
  if (-not $p) { break }
  if ($p.MainWindowHandle -ne 0) {
    $r = New-Object WinShot+RECT
    [WinShot]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($w -gt 100 -and $h -gt 100) {
      $bmp = New-Object System.Drawing.Bitmap $w, $h
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $dc = $g.GetHdc()
      [WinShot]::PrintWindow($p.MainWindowHandle, $dc, 2) | Out-Null
      $g.ReleaseHdc($dc); $g.Dispose()
      $bmp.Save((Join-Path $Out ("win-{0:D3}.png" -f $n)), [System.Drawing.Imaging.ImageFormat]::Png)
      $bmp.Dispose(); $n++
    }
  }
  Start-Sleep -Milliseconds ([int]($Every * 1000))
}
"saved $n window shots"
