# Testa o som de cada fase: o roteiro "gameplay" do measure.ps1 (Novo jogo -> tutorial) com RDR_LEVEL_REDIRECT
# trocando a 1a fase do Novo jogo (hng) pela fase escolhida; grava a saida do SPU2 e imprime uma linha por fase.
# Usa o teclado/foco da janela: nao usar o PC durante o teste.
# Uso: .\tools\level_audio_test.ps1 [-Only bridge,coach] [-Seconds 420]
param([string[]]$Only, [int]$Seconds = 420)
$root = Split-Path $PSScriptRoot -Parent
$outDir = Join-Path $root "logs\level_audio"
New-Item -ItemType Directory -Force $outDir | Out-Null
$levels = Get-ChildItem (Join-Path $root "game\disc\LVLS_SNG") -Filter *.DAT | ForEach-Object { $_.BaseName.ToLower() }
if ($Only) { $sel = $Only -split ","; $levels = $levels | Where-Object { $sel -contains $_ } }
foreach ($lv in $levels) {
    $pcm = Join-Path $outDir "$lv.pcm"
    Remove-Item $pcm -ErrorAction SilentlyContinue
    $before = Get-ChildItem (Join-Path $root "logs") -Filter "run_*_drive.log" | Select-Object -ExpandProperty FullName
    & (Join-Path $PSScriptRoot "measure.ps1") -Gpu -Scenario gameplay -Seconds $Seconds `
        -Env "RDR_LEVEL_REDIRECT=hng=$lv,PS2X_SPU2_PCM=$pcm,PS2X_SPU2_LOG=1" *> $null
    $log = Get-ChildItem (Join-Path $root "logs") -Filter "run_*_drive.log" | Where-Object { $before -notcontains $_.FullName } |
        Select-Object -Last 1
    $fases = if ($log) { (Select-String -Path $log.FullName -Pattern '\[rdr\] fase' | ForEach-Object { $_.Line -replace '.*\[rdr\] fase ', '' }) -join ' | ' } else { '?' }
    $audio = python (Join-Path $PSScriptRoot "audio_report.py") $pcm --summary --label "$lv (ultimos 120 s)" --last 120
    "$audio | fases: $fases"
}
