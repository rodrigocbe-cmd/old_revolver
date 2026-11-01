# Repete tools/measure.ps1 N vezes e imprime uma linha por rodada: FPS em regime, se travou e o log.
# Serve para medir a taxa de travamentos intermitentes com poucos tokens.
#   .\tools\soak.ps1 -Runs 3 -Gpu [-Env 'K=V,...']
# "travou" = as 3 ultimas janelas com gs.finish = 0 (o jogo parou de apresentar quadros).
param([int]$Runs = 3, [switch]$Gpu, [string]$Env = '', [ValidateSet('gameplay', 'menu')][string]$Scenario = 'gameplay')
$root = Split-Path -Parent $PSScriptRoot
$hangs = 0
for ($i = 1; $i -le $Runs; $i++) {
    $args2 = @{ Scenario = $Scenario }
    if ($Gpu) { $args2.Gpu = $true }
    if ($Env) { $args2.Env = $Env }
    & (Join-Path $PSScriptRoot 'measure.ps1') @args2 *> $null
    $json = Get-ChildItem (Join-Path $root 'logs\report_*.json') | Sort-Object LastWriteTime | Select-Object -Last 1
    $r = Get-Content $json.FullName -Raw | ConvertFrom-Json
    $series = @($r.finish_series)
    $tail = $series[-3..-1]
    $hung = ($tail | Where-Object { $_ -ne 0 }).Count -eq 0
    if ($hung) { $hangs++ }
    "rodada {0}: regime={1} travou={2} log={3}" -f $i, $r.steady.'gs.finish', $(if ($hung) { 'SIM' } else { 'nao' }), $r.log
}
"travamentos: $hangs de $Runs"
