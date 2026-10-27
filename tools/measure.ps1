# Roda o jogo num cenário fixo, mede e imprime só um relatório curto (tools/run_report.py).
# Feito para economizar tokens de IA: a saída inteira cabe em ~20 linhas.
#
#   .\tools\measure.ps1                         # gameplay (tutorial), GS em software, ~10 min
#   .\tools\measure.ps1 -Gpu                    # idem com o GS na GPU (paraLLEl-GS)
#   .\tools\measure.ps1 -Scenario menu -Gpu     # só até o menu (~3 min)
#   .\tools\measure.ps1 -Gpu -SetBaseline       # grava o resultado como referência do cenário
#   .\tools\measure.ps1 -Env 'PS2X_GS_NODRAW=1,PS2X_VU1_COMPILED=0'   # variáveis extras
#
# Compara automaticamente com logs\baseline_<cenario>_<gs>.json, se existir.
# ATENÇÃO: o roteiro manda teclas para a janela do jogo e a traz para a frente; não use o PC durante a medição.
# Saídas: logs\run_<data>_drive.log (log completo), logs\report_<data>.{txt,json,png} (relatório, dados, última tela).
param(
    [ValidateSet('gameplay', 'menu')][string]$Scenario = 'gameplay',
    [switch]$Gpu,
    [int]$Seconds = 0,
    [string]$Env = '',
    [switch]$SetBaseline,
    [int]$Last = 5
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$logs = Join-Path $root 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

if ($Seconds -le 0) { $Seconds = if ($Scenario -eq 'gameplay') { 600 } else { 170 } }
# Teclas a partir da detecção do menu: X (0x20) em 6 s e 20 s, depois a cada 25 s (livro → perfil → capítulo → Begin → diálogos)
$keys = ''
if ($Scenario -eq 'gameplay') {
    $keys = (@(6, 20) + (0..16 | ForEach-Object { 85 + 25 * $_ }) | ForEach-Object { "${_}:20:400" }) -join ','
}

$gsName = if ($Gpu) { 'gpu' } else { 'software' }
if ($Gpu) { $env:PS2X_GS_BACKEND = 'parallel' } else { Remove-Item Env:PS2X_GS_BACKEND -ErrorAction SilentlyContinue }
foreach ($kv in ($Env -split ',' | Where-Object { $_ })) {
    $k, $v = $kv -split '=', 2
    Set-Item -Path "Env:$($k.Trim())" -Value $v
}

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$shots = Join-Path $logs "shots_$stamp"
$driveOut = Join-Path $logs "report_${stamp}_drive.txt"
$driveArgs = @{ Seconds = $Seconds; OutDir = $shots }
if ($keys) { $driveArgs.MenuKeys = $keys }
& (Join-Path $PSScriptRoot 'drive.ps1') @driveArgs *> $driveOut

$log = (Select-String -Path $driveOut -Pattern '^log: (.+)$' | Select-Object -Last 1).Matches.Groups[1].Value
if (-not $log -or -not (Test-Path $log)) { "ERRO: log da execução não encontrado (veja $driveOut)"; exit 1 }

$json = Join-Path $logs "report_$stamp.json"
$txt = Join-Path $logs "report_$stamp.txt"
$baseline = Join-Path $logs "baseline_${Scenario}_$gsName.json"
$reportArgs = @((Join-Path $PSScriptRoot 'run_report.py'), $log, '--last', $Last, '--json', $json)
if ((Test-Path $baseline) -and -not $SetBaseline) { $reportArgs += @('--baseline', $baseline) }
$header = "Cenário: $Scenario | GS: $gsName | $Seconds s | extras: $(if ($Env) { $Env } else { '-' })"
$report = & python -I @reportArgs
($header, $report) | Out-File -Encoding utf8 $txt

$lastShot = Get-ChildItem $shots -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne 'cur.png' } |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($lastShot) { Copy-Item $lastShot.FullName (Join-Path $logs "report_$stamp.png") }
if ($SetBaseline) { Copy-Item $json $baseline -Force }

$header
$report
"Última tela: $(if ($lastShot) { Join-Path $logs "report_$stamp.png" } else { '(nenhuma)' })"
if ($SetBaseline) { "Referência gravada: $baseline" }
