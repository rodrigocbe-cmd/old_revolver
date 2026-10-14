# Compila o runner nativo (ps2EntryRunner) com o C++ gerado em out/generated.
# O runtime do PS2Recomp compila tudo que estiver em ps2xRuntime/src/runner/*.cpp e inclui
# os headers gerados de ps2xRuntime/include (ambos ignorados pelo git do PS2Recomp).
# -NoSync: nao recopia out/generated (retomar build interrompido). -Jobs N: limita o paralelismo.
param([string]$Config = 'RelWithDebInfo', [switch]$NoSync, [int]$Jobs = 0)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
$ErrorActionPreference = 'Continue'

$src = Join-Path $RDR_ROOT 'external\PS2Recomp'
$gen = Join-Path $RDR_ROOT 'out\generated'
$runner = Join-Path $src 'ps2xRuntime\src\runner'
$include = Join-Path $src 'ps2xRuntime\include'
$build = Join-Path $RDR_ROOT 'out\build'

if (-not (Test-Path (Join-Path $gen 'register_functions.cpp'))) { throw 'out/generated vazio: rode 2_recompile.ps1' }

if (-not $NoSync) {
    # Sincroniza o codigo gerado + overrides do projeto
    Get-ChildItem $runner -Filter *.cpp | Remove-Item
    Copy-Item (Join-Path $gen '*.cpp') $runner
    Copy-Item (Join-Path $gen '*.h') $include
    Get-ChildItem (Join-Path $RDR_ROOT 'src') -Filter *.cpp -ErrorAction SilentlyContinue | Copy-Item -Destination $runner
    "{0} arquivos .cpp em runner" -f (Get-ChildItem $runner -Filter *.cpp).Count

    cmake -S $src -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
        -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
    if ($LASTEXITCODE) { throw 'configuracao do CMake falhou' }
}

# Impede o Windows de suspender durante o build (notebook: suspensao com CPU no maximo
# causou bugcheck 0x9F DRIVER_POWER_STATE_FAILURE em 2026-10-14). Vale so enquanto o script roda.
Add-Type -Namespace Win32 -Name Power -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);'
$ES_CONTINUOUS = [uint32]'0x80000000'; $ES_SYSTEM_REQUIRED = [uint32]1
[void][Win32.Power]::SetThreadExecutionState($ES_CONTINUOUS -bor $ES_SYSTEM_REQUIRED)

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $RDR_ROOT "logs\build_$stamp.log"
$buildArgs = @('--build', $build, '--target', 'ps2EntryRunner')
if ($Jobs -gt 0) { $buildArgs += @('--parallel', $Jobs) }
try {
    cmake @buildArgs 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
    $code = $LASTEXITCODE
} finally {
    [void][Win32.Power]::SetThreadExecutionState($ES_CONTINUOUS)
}
Get-Content $log -Tail 20
"build exit $code (log: $log)"
if ($code) { exit $code }
Get-ChildItem $build -Recurse -Filter ps2EntryRunner.exe | Select-Object -ExpandProperty FullName
