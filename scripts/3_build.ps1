# Compila o runner nativo (ps2EntryRunner) com o C++ gerado em out/generated.
# O runtime do PS2Recomp compila tudo que estiver em ps2xRuntime/src/runner/*.cpp e inclui
# os headers gerados de ps2xRuntime/include (ambos ignorados pelo git do PS2Recomp).
param([string]$Config = 'RelWithDebInfo')

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"
$ErrorActionPreference = 'Continue'

$src = Join-Path $RDR_ROOT 'external\PS2Recomp'
$gen = Join-Path $RDR_ROOT 'out\generated'
$runner = Join-Path $src 'ps2xRuntime\src\runner'
$include = Join-Path $src 'ps2xRuntime\include'
$build = Join-Path $RDR_ROOT 'out\build'

if (-not (Test-Path (Join-Path $gen 'register_functions.cpp'))) { throw 'out/generated vazio: rode 2_recompile.ps1' }

# Sincroniza o codigo gerado + overrides do projeto
Get-ChildItem $runner -Filter *.cpp | Remove-Item
Copy-Item (Join-Path $gen '*.cpp') $runner
Copy-Item (Join-Path $gen '*.h') $include
Get-ChildItem (Join-Path $RDR_ROOT 'src') -Filter *.cpp -ErrorAction SilentlyContinue | Copy-Item -Destination $runner
"{0} arquivos .cpp em runner" -f (Get-ChildItem $runner -Filter *.cpp).Count

cmake -S $src -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" `
    -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
if ($LASTEXITCODE) { throw 'configuracao do CMake falhou' }

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$log = Join-Path $RDR_ROOT "logs\build_$stamp.log"
cmake --build $build --target ps2EntryRunner 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $log
$code = $LASTEXITCODE
Get-Content $log -Tail 20
"build exit $code (log: $log)"
if ($code) { exit $code }
Get-ChildItem $build -Recurse -Filter ps2EntryRunner.exe | Select-Object -ExpandProperty FullName
