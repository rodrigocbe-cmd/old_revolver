param([int]$Seconds = 420, [string]$OutDir = "", [int]$TraceAt = 0, [string]$MenuKeys = "", [switch]$KeepOpen)
# Roteiro guiado pela tela (usado por tools/measure.ps1): classifica a janela a cada 2 s e responde com as teclas do fluxo de boot.
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $root "logs\shots" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class D { [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra); }
'@
[void][D]::SetProcessDPIAware()
$env:PS2X_STATS = '1'   # contadores do [run:draws] (desligados por padrao no runtime)
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = "$root\logs\run_${stamp}_drive.log"
$p = Start-Process "$root\out\build\ps2xRuntime\ps2EntryRunner.exe" -ArgumentList "`"$root\game\disc\SLUS_205.00`"" `
    -WorkingDirectory "$root\game\disc" -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
$t0 = Get-Date
function Elapsed { ((Get-Date) - $t0).TotalSeconds }
function Key([byte]$vk, [int]$hold) {
    [void][D]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 100
    [D]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $hold; [D]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
}
function Shot([string]$path) {
    $r = New-Object D+R; [void][D]::GetWindowRect($p.MainWindowHandle, [ref]$r)
    $w = $r.Rt - $r.L; $h = $r.B - $r.T; if ($w -le 0 -or $h -le 0) { return $false }
    $bmp = New-Object System.Drawing.Bitmap $w, $h; $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); return $true
}
while ((Elapsed) -lt 15) { $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break }; Start-Sleep -Milliseconds 200 }
Start-Sleep 2; Key 0x70 80   # F1: esconde o painel de debug
$traced = $false; $demoAt = 0; $menuAt = 0; $steps = @(); if ($MenuKeys) { $steps = $MenuKeys.Split(",") | % { $p2 = $_.Split(":"); @{ t = [double]$p2[0]; vk = [byte]([Convert]::ToInt32($p2[1], 16)); hold = [int]$p2[2]; done = $false } } }; $startPressed = $false; $afterDialog = $false; $demoStarted = $false; $cool = 0; $prev = ''; $lastSave = 0
$tmp = Join-Path $OutDir 'cur.png'
while ((Elapsed) -lt $Seconds) {
    $p.Refresh(); if ($p.HasExited) { "processo saiu"; break }
    [void][D]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 150
    if (-not (Shot $tmp)) { Start-Sleep 2; continue }
    $cls = (& python -I "$PSScriptRoot\classify_screen.py" $tmp)
    $state = $cls.Split(' ')[0]
    $t = [int](Elapsed)
    if ($state -ne $prev -or ($t - $lastSave) -ge 20) {
        Copy-Item $tmp (Join-Path $OutDir ("{0}_{1:D3}s_{2}.png" -f $stamp, $t, $state)); $lastSave = $t
        "{0,4}s {1}" -f $t, $cls
    }
    $prev = $state
    if ($demoStarted -and $menuAt -eq 0 -and $state -eq 'scene' -and ((Elapsed) - $demoAt) -gt 35) { $menuAt = Elapsed; "{0,4}s menu detectado" -f $t }
    if ($menuAt -gt 0) {
        foreach ($st in $steps) { if (-not $st.done -and (Elapsed) - $menuAt -ge $st.t) { Key $st.vk $st.hold; $st.done = $true; "{0,4}s -> tecla 0x{1:X2}" -f $t, $st.vk; Start-Sleep -Milliseconds 300; [void](Shot (Join-Path $OutDir ("{0}_{1:D3}s_key{2:X2}.png" -f $stamp, [int](Elapsed), $st.vk))) } }
        if (($t - $lastSave) -ge 5) { Copy-Item $tmp (Join-Path $OutDir ("{0}_{1:D3}s_m_{2}.png" -f $stamp, $t, $state)); $lastSave = $t }
    }
    if ($TraceAt -gt 0 -and $t -ge $TraceAt -and -not $traced) { Key 0x78 80; $traced = $true; "{0,4}s -> F9 (rastreio GS)" -f $t }
    if ($cool -gt (Elapsed)) { Start-Sleep -Milliseconds 1800; continue }
    if ($menuAt -gt 0) { Start-Sleep -Milliseconds 1800; continue }
    switch ($state) {
        'scene'    { if (-not $startPressed -and $t -gt 60) { Key 0x0D 400; $startPressed = $true; $cool = (Elapsed) + 20; "{0,4}s -> Start (sair do video)" -f $t }
                     elseif (-not $demoStarted -and $t -gt 110) { Key 0x0D 400; $demoStarted = $true; $demoAt = Elapsed; $cool = (Elapsed) + 12; "{0,4}s -> Start (demo/menu)" -f $t } }
        'dialog'   { if (-not $afterDialog) { Key 0x53 500; Start-Sleep -Milliseconds 1500; Key 0x20 400; $afterDialog = $true; $cool = (Elapsed) + 6; "{0,4}s -> No + X (memory card)" -f $t }
                     elseif (-not $demoStarted) { Key 0x20 400; $cool = (Elapsed) + 6; "{0,4}s -> X (aviso)" -f $t } }
        'autosave' { Key 0x20 400; $cool = (Elapsed) + 6; "{0,4}s -> X (autosave)" -f $t }
        'demotext' { if (-not $demoStarted -and $t -gt 100) { Key 0x0D 400; $demoStarted = $true; $demoAt = Elapsed; $cool = (Elapsed) + 12; "{0,4}s -> Start (demo)" -f $t } }
    }
    Start-Sleep -Milliseconds 1800
}
if ($KeepOpen -and -not $p.HasExited) { "jogo aberto pid=$($p.Id) log=$log"; exit 0 }
if (-not $p.HasExited) { Stop-Process $p.Id -Force; $p.WaitForExit() }
Get-Content "$log.err" | Add-Content $log; Remove-Item "$log.err"
"log: $log"
