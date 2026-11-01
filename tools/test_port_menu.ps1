# Teste do menu do port (FUNCOES.md #66): abre o jogo, F10, print, "Exit to Windows" e confere se o processo saiu.
# Saída: uma linha OK/FALHA + caminho do print. Não use o PC durante o teste (manda teclas para a janela).
param([int]$BootSeconds = 25, [switch]$Gpu)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class TM { [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Rt, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra); }
'@
[void][TM]::SetProcessDPIAware()
if ($Gpu) { $env:PS2X_GS_BACKEND = 'parallel' } else { $env:PS2X_GS_BACKEND = 'software' }
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = "$root\logs\run_${stamp}_portmenu.log"
$p = Start-Process "$root\out\build\ps2xRuntime\ps2EntryRunner.exe" -ArgumentList "`"$root\game\disc\SLUS_205.00`"" `
    -WorkingDirectory "$root\game\disc" -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
function Key([byte]$vk) {
    [void][TM]::SetForegroundWindow($p.MainWindowHandle); Start-Sleep -Milliseconds 150
    [TM]::keybd_event($vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120; [TM]::keybd_event($vk, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 400
}
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 20) { $p.Refresh(); if ($p.MainWindowHandle -ne 0) { break }; Start-Sleep -Milliseconds 200 }
Start-Sleep $BootSeconds
Key 0x70   # F1: esconde o painel de debug
Key 0x79   # F10
Start-Sleep 1
$shot = "$root\logs\portmenu_$stamp.png"
$r = New-Object TM+R; [void][TM]::GetWindowRect($p.MainWindowHandle, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.Rt - $r.L), ($r.B - $r.T); $g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $bmp.Save($shot); $g.Dispose(); $bmp.Dispose()
Key 0x53   # S (= seta para baixo; keybd_event sem flag estendida mandaria a seta como teclado numerico)
Key 0x0D   # Enter
$exited = $p.WaitForExit(15000)
if (-not $exited) { Stop-Process $p.Id -Force; $p.WaitForExit() }
Get-Content "$log.err" | Add-Content $log; Remove-Item "$log.err"
$msg = Select-String -Path $log -Pattern 'Exit to Windows' -Quiet
if ($exited -and $msg) { "OK: o jogo fechou pelo menu (print: $shot)" } else { "FALHA: saiu=$exited log_tem_exit=$msg (print: $shot, log: $log)" }
