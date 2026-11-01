# Abre o jogo para jogar (build de out/preview). Padrao: maior qualidade (SSAA 16 = 4x por eixo) na resolucao do
# monitor, em tela cheia sem borda. -Ssaa 4 = 2x por eixo; 0 = nativo do PS2. -Window abre em janela.
# Ex.: .\tools\play.ps1
#      .\tools\play.ps1 -Ssaa 4 -Size 1920x1080 -Window
#      .\tools\play.ps1 -SafeIop          (IOP na thread do jogo: mais lento; teste do loading travado)
param([int]$Ssaa = 16, [string]$Size = "", [string]$Aspect = "16:9", [switch]$SafeIop, [switch]$Window)
Add-Type -AssemblyName System.Windows.Forms
if (-not $Size) { $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; $Size = "$($b.Width)x$($b.Height)" }
if (-not $Window) { $env:PS2X_FULLSCREEN = "1" } else { Remove-Item Env:PS2X_FULLSCREEN -ErrorAction SilentlyContinue }
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "out\preview\ps2EntryRunner.exe"
$disc = Join-Path $root "game\disc"
$built = Join-Path $root "out\build\ps2xRuntime\ps2EntryRunner.exe"
if ((Test-Path $built) -and (-not (Test-Path $exe) -or (Get-Item $built).LastWriteTime -gt (Get-Item $exe).LastWriteTime)) {
    try { Copy-Item $built $exe -Force -ErrorAction Stop; "build novo copiado para out\preview" }
    catch { "aviso: feche o jogo aberto para atualizar out\preview (usando o build antigo)" }
}
$env:PS2X_WINDOW_SIZE = $Size
$env:PS2X_ASPECT = $Aspect   # 16:9 com "widescreen" em config/params.txt; 4:3 sem ele
if ($Ssaa -gt 0) { $env:PS2X_GS_SSAA = "$Ssaa" } else { Remove-Item Env:PS2X_GS_SSAA -ErrorAction SilentlyContinue }
$env:PS2X_CD_TRACE = "1"   # leituras do disco no log (diagnostico de loading travado)
if ($SafeIop) { $env:PS2X_IOP_THREAD = "0" } else { Remove-Item Env:PS2X_IOP_THREAD -ErrorAction SilentlyContinue }
$log = Join-Path $root ("logs\play_{0}.log" -f (Get-Date -Format yyyyMMdd_HHmmss))
Start-Process -FilePath $exe -ArgumentList "`"$(Join-Path $disc 'SLUS_205.00')`"" -WorkingDirectory $disc `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err"
"jogo aberto ($Size, SSAA $Ssaa). F11 = menu de debug. Log: $log"
