# Gera o C++ a partir do ELF descomprimido.
#   1. (opcional, -Export) exporta o mapa de funcoes do projeto Ghidra (out/ghidra/RDR)
#   2. mescla config/ghidra_export.toml + config/project.toml -> out/recomp.toml
#   3. roda ps2_recomp -> out/generated
param([switch]$Export)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
# PS 5.1 transforma stderr de executaveis redirecionado em erro; checamos $LASTEXITCODE/arquivos.
$ErrorActionPreference = 'Continue'

$elf = Join-Path $RDR_ROOT 'game\SLUS_205.00.unpacked.elf'
$exportToml = Join-Path $RDR_ROOT 'config\ghidra_export.toml'
$csv = Join-Path $RDR_ROOT 'config\funcmap.csv'
$outDir = Join-Path $RDR_ROOT 'out\generated'
$recompToml = Join-Path $RDR_ROOT 'out\recomp.toml'

if (-not (Test-Path $elf)) {
    python -I (Join-Path $RDR_ROOT 'tools\rdr_unpack.py') (Join-Path $RDR_ROOT 'game\SLUS_205.00') $elf
    if ($LASTEXITCODE) { throw 'falha ao descomprimir o ELF' }
}

if ($Export -or -not (Test-Path $csv)) {
    $script = Join-Path $RDR_ROOT 'external\PS2Recomp\ps2xRecomp\tools\ghidra'
    $log = Join-Path $RDR_ROOT 'logs\ghidra_export.log'
    & "$GHIDRA_HOME\support\analyzeHeadless.bat" (Join-Path $RDR_ROOT 'out\ghidra') RDR `
        -process SLUS_205.00.unpacked.elf -noanalysis -readOnly `
        -scriptPath $script -postScript ExportPS2Functions.java $exportToml $csv 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
    if (-not (Test-Path $csv)) { throw "exportacao do Ghidra falhou (veja $log)" }
    Select-String -Path $log -Pattern 'Exported' | ForEach-Object { $_.Line.Trim() }
}

New-Item -ItemType Directory -Force $outDir | Out-Null
python -I (Join-Path $RDR_ROOT 'tools\make_recomp_config.py') $exportToml (Join-Path $RDR_ROOT 'config\project.toml') $elf $csv $outDir $recompToml
if ($LASTEXITCODE) { throw 'falha ao gerar out/recomp.toml' }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $RDR_ROOT "logs\recompile_$stamp.log"
& (Join-Path $RDR_ROOT 'out\tools\ps2xRecomp\ps2_recomp.exe') $recompToml 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
$code = $LASTEXITCODE
Get-Content $log -Tail 15
"ps2_recomp exit $code (log: $log)"
if ($code) { exit $code }
