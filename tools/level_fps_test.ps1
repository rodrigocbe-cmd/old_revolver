# Mede o FPS de cada fase: roteiro "gameplay" do measure.ps1 (Novo jogo -> 1a fase) com RDR_LEVEL_REDIRECT
# trocando a 1a fase do Novo jogo (-From, padrao perfnew = tutorial com save vazio) pela escolhida. Imprime uma linha por fase (gs.finish por 600 ticks: 300 = 30 fps).
# Usa o teclado/foco da janela: nao usar o PC durante o teste.
# Uso: .\tools\level_fps_test.ps1 [-Only bridge,coach] [-Seconds 360] [-Env 'PS2X_GS_SSAA=16,...']
param([string[]]$Only, [int]$Seconds = 360, [string]$Env = '', [string]$From = 'perfnew')
$root = Split-Path $PSScriptRoot -Parent
$levels = Get-ChildItem (Join-Path $root "game\disc\LVLS_SNG") -Filter *.DAT | ForEach-Object { $_.BaseName.ToLower() }
if ($Only) { $sel = $Only -split ","; $levels = $levels | Where-Object { $sel -contains $_ } }
foreach ($lv in $levels) {
    $extra = "RDR_LEVEL_REDIRECT=$From=$lv"
    if ($Env) { $extra = "$Env,$extra" }
    & (Join-Path $PSScriptRoot "measure.ps1") -Gpu -Scenario gameplay -Seconds $Seconds -Env $extra *> $null
    # A troca so vale se o log mostrar "[rdr] fase $From -> $lv"
    $json = Get-ChildItem (Join-Path $root 'logs\report_*.json') | Sort-Object LastWriteTime | Select-Object -Last 1
    $r = Get-Content $json.FullName -Raw | ConvertFrom-Json
    $series = @($r.finish_series)
    $hung = (@($series[-3..-1]) | Where-Object { $_ -ne 0 }).Count -eq 0
    $min = ($series | Select-Object -Last 10 | Measure-Object -Minimum).Minimum
    $fases = (Select-String -Path $r.log -Pattern '\[rdr\] fase' | ForEach-Object { $_.Line -replace '.*\[rdr\] fase ', '' }) -join ' > '
    "{0,-9} regime={1,3} min10={2,3} idle_ms={3,5} travou={4} | {5} | {6}" -f $lv, $r.steady.'gs.finish', $min,
        $r.steady.'ee.idle_ms', $(if ($hung) { 'SIM' } else { 'nao' }), $fases, (Split-Path $r.log -Leaf)
}
