# Perfil por amostragem da thread do jogo (cdb do WinDbg), sem instalar profiler.
# Roda o cenário com tools/drive.ps1, espera chegar ao regime, captura N pilhas de todas as threads e
# imprime o resumo de tools/profile_report.py. O cdb pausa o processo a cada amostra: os números de FPS
# desta execução não valem como medição (use tools/measure.ps1 para isso).
#
#   .\tools\profile.ps1 -Gpu                         # gameplay, amostras a partir de 420 s
#   .\tools\profile.ps1 -Gpu -Scenario menu -WaitSeconds 150
#   python tools/profile_report.py logs\prof_<data> --all   # depois: outras threads
param(
    [ValidateSet('gameplay', 'menu')][string]$Scenario = 'gameplay',
    [switch]$Gpu,
    [int]$WaitSeconds = 0,
    [int]$Samples = 60,
    [string]$Env = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cdb = "$env:LOCALAPPDATA\Microsoft\WindowsApps\cdbX64.exe"
if (-not (Test-Path $cdb)) { $cdb = (Get-Command cdb.exe -ErrorAction SilentlyContinue).Source }
if (-not $cdb) { 'ERRO: cdb não encontrado (instale o WinDbg: winget install Microsoft.WinDbg)'; exit 1 }
if ($WaitSeconds -le 0) { $WaitSeconds = if ($Scenario -eq 'gameplay') { 420 } else { 150 } }

if ($Gpu) { $env:PS2X_GS_BACKEND = 'parallel' } else { $env:PS2X_GS_BACKEND = 'software' }
foreach ($kv in ($Env -split ',' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; Set-Item -Path "Env:$($k.Trim())" -Value $v }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$out = Join-Path $root "logs\prof_$stamp"
New-Item -ItemType Directory -Force $out | Out-Null
$keys = ''
if ($Scenario -eq 'gameplay') { $keys = (@(6, 20) + (0..16 | ForEach-Object { 85 + 25 * $_ }) | ForEach-Object { "${_}:20:400" }) -join ',' }
$driveSeconds = $WaitSeconds + [int]($Samples * 3) + 30
$driveArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'drive.ps1'),
    '-Seconds', $driveSeconds, '-OutDir', (Join-Path $out 'shots'))
if ($keys) { $driveArgs += @('-MenuKeys', $keys) }
$drive = Start-Process powershell -ArgumentList $driveArgs -RedirectStandardOutput (Join-Path $out 'drive.txt') -PassThru -WindowStyle Hidden

Start-Sleep $WaitSeconds
$p = Get-Process ps2EntryRunner -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { 'ERRO: o jogo não está rodando (veja ' + (Join-Path $out 'drive.txt') + ')'; exit 1 }
for ($i = 0; $i -lt $Samples; $i++) {
    & $cdb -pv -p $p.Id -y "$root\out\build\ps2xRuntime" -c '.symopt+0x40; ~*k 30; q' 2>&1 | Out-File -Encoding utf8 (Join-Path $out "sample_$i.txt")
    Start-Sleep -Milliseconds 500
}
$drive.WaitForExit()
"Perfil: $Scenario | GS: $(if ($Gpu) { 'gpu' } else { 'software' }) | $Samples amostras a partir de $WaitSeconds s | $out"
python -I (Join-Path $PSScriptRoot 'profile_report.py') $out
