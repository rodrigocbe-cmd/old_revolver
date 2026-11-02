# Mede andar x correr (docs/CONTROLES.md): chega ao tutorial (drive.ps1, como measure.ps1 -Scenario gameplay), e
# para cada walk_tilt segura W por 3 s e mede o deslocamento do Red pela memoria (tools/ee_mem.py). No fim, com
# Shift (correr). Resultado: uma linha por inclinacao. Nao use o PC durante o teste (manda teclas para a janela).
# Uso: .\tools\walk_test.ps1 [-Tilts 0.2,0.3,...] [-Addr 0x199CC50]
param([double[]]$Tilts = @(0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.85, 1.0), [int]$Hold = 3000)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$logs = Join-Path $root 'logs'
$ini = Join-Path $logs 'walk_test_controls.ini'
Remove-Item $ini -ErrorAction SilentlyContinue
$save = Join-Path $logs 'walk_save'; Remove-Item $save -Recurse -ErrorAction SilentlyContinue; New-Item -ItemType Directory $save | Out-Null
$env:RDR_SAVE_DIR = $save   # save vazio: o fluxo do measure.ps1 leva ao tutorial
$env:RDR_CONTROLS = 'rdr'; $env:RDR_CONTROLS_INI = $ini; $env:PS2X_GS_BACKEND = 'parallel'; $env:PS2X_MOUSE = '0'
$keys = (@(6, 20) + (0..16 | ForEach-Object { 85 + 25 * $_ }) | ForEach-Object { "${_}:20:400" }) -join ','
$out = & (Join-Path $PSScriptRoot 'drive.ps1') -Seconds 540 -MenuKeys $keys -KeepOpen -OutDir (Join-Path $logs 'walk_shots') *>&1
$line = $out | Where-Object { "$_" -match 'jogo aberto pid=(\d+)' } | Select-Object -Last 1
if (-not $line) { "FALHA: o jogo nao ficou aberto"; exit 1 }
$gamePid = [int]([regex]::Match("$line", 'pid=(\d+)').Groups[1].Value)
$proc = Get-Process -Id $gamePid

Add-Type @'
using System; using System.Runtime.InteropServices;
public class WK { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra); }
'@
function Pos { (& python -I (Join-Path $PSScriptRoot 'ee_mem.py') read $Addr --n 3 --type f32) -join ' ' }
function Floats($s) { [regex]::Matches($s, '-?\d+\.\d+(e[-+]\d+)?') | ForEach-Object { [double]$_.Value } }
function Walk([double]$tilt, [bool]$run) {
    (Get-Content $ini) -replace '^walk_tilt = .*', ("walk_tilt = {0:0.00}" -f $tilt).Replace(',', '.') | Set-Content $ini
    Start-Sleep -Milliseconds 1500
    # Espaco fecha dicas/dialogos do tutorial (que prendem o Red); se ele pular, espera pousar
    [void][WK]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 300
    [WK]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200; [WK]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 2500
    $a = Floats (Pos)
    if ($run) { [WK]::keybd_event(0xA0, 0, 0, [UIntPtr]::Zero) }
    [WK]::keybd_event(0x57, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $Hold; [WK]::keybd_event(0x57, 0, 2, [UIntPtr]::Zero)
    if ($run) { [WK]::keybd_event(0xA0, 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 400
    $b = Floats (Pos)
    $d = 0.0; for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) { if ($i -ne 1) { $d += [Math]::Pow($b[$i] - $a[$i], 2) } }
    "tilt={0:0.00} run={1} desloc={2:0.00}  antes=[{3}] depois=[{4}]" -f $tilt, $run, [Math]::Sqrt($d), ($a -join ' '), ($b -join ' ')
    # volta (S) para nao sair da area do tutorial
    [WK]::keybd_event(0x53, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds $Hold; [WK]::keybd_event(0x53, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 600
}
# Acha a posicao (tools/find_pos.py): parado, anda, parado, anda
function Snap($n) { & python -I (Join-Path $PSScriptRoot 'ee_mem.py') snap $n | Out-Null }
function HoldW { [void][WK]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 300; [WK]::keybd_event(0x57, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 2000; [WK]::keybd_event(0x57, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 1500 }
function Space { [void][WK]::SetForegroundWindow($proc.MainWindowHandle); Start-Sleep -Milliseconds 300; [WK]::keybd_event(0x20, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 300; [WK]::keybd_event(0x20, 0, 2, [UIntPtr]::Zero) }
# O save pode parar no menu antes da fase (Start / Weapon Selection / Shop): Start e espera carregar
$first = $null
for ($try = 0; $try -lt 4 -and -not $first; $try++) {
    if ($try -gt 0) { Space; Start-Sleep -Seconds 20 }
    Snap s0; HoldW; Snap s1; Start-Sleep -Milliseconds 1500; Snap s2; HoldW; Snap s3
    $found = & python -I (Join-Path $PSScriptRoot 'find_pos.py') (Join-Path $logs 'ee_snap_s0.bin') (Join-Path $logs 'ee_snap_s1.bin') (Join-Path $logs 'ee_snap_s2.bin') (Join-Path $logs 'ee_snap_s3.bin')
    "tentativa ${try}:"; $found | Select-Object -First 6
    $first = $found | Where-Object { $_ -match '^0x' } | Select-Object -First 1
}
if (-not $first) { "FALHA: posicao nao encontrada (fora da fase?)"; Stop-Process -Id $gamePid -Force; exit 1 }
$Addr = ($first -split ' ')[0]
"usando $Addr"
"posicao inicial: $(Pos)"
foreach ($t in $Tilts) { Walk $t $false }
Walk 0.45 $true
Stop-Process -Id $gamePid -Force
