# Testa o som de cada video do disco: troca o logo do boot (RSLOGO.PSS) pelo video escolhido
# (RDR_FMV_REDIRECT, src/rdr_overrides.cpp), grava a saida do SPU2 e imprime uma linha por video (segundos com som, RMS medio).
# So os primeiros <duracao do video> s da gravacao entram na conta (depois o boot segue para o TITLES).
# Uso: .\tools\fmv_audio_test.ps1 [-Only BRIDGE01,TITLES] [-Extra 20]
param([string[]]$Only, [int]$Extra = 20)
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "out\build\ps2xRuntime\ps2EntryRunner.exe"
$disc = Join-Path $root "game\disc"
$outDir = Join-Path $root "logs\fmv_audio"
New-Item -ItemType Directory -Force $outDir | Out-Null
$videos = Get-ChildItem (Join-Path $disc "PSS") -Filter *.PSS | ForEach-Object { $_.BaseName }
if ($Only) { $sel = $Only -split "," ; $videos = $videos | Where-Object { $sel -contains $_ } }
$env:PS2X_WINDOW_SIZE = "640x448"
Remove-Item Env:PS2X_GS_SSAA -ErrorAction SilentlyContinue
$env:PS2X_SPU2_LOG = "1"
foreach ($v in $videos) {
    $pcm = Join-Path $outDir "$v.pcm"
    $log = Join-Path $outDir "$v.log"
    Remove-Item $pcm -ErrorAction SilentlyContinue
    $env:RDR_FMV_REDIRECT = "rslogo=$($v.ToLower())"
    $env:PS2X_SPU2_PCM = $pcm
    $p = Start-Process -FilePath $exe -ArgumentList "`"$(Join-Path $disc 'SLUS_205.00')`"" -WorkingDirectory $disc `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru
    $dur = [int](python (Join-Path $PSScriptRoot "pss_duration.py") (Join-Path $disc "PSS\$v.PSS"))
    Start-Sleep -Seconds ($dur + $Extra)
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
    python (Join-Path $PSScriptRoot "audio_report.py") $pcm --summary --label "$v (${dur}s)" --first $dur
}
