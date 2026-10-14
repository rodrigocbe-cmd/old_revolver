# Clona o PS2Recomp no commit fixado, aplica o patch do projeto e compila as ferramentas
# (ps2_recomp, ps2_analyzer). O runtime e compilado depois, em 3_build.ps1.

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\env.ps1"

$PS2RECOMP_URL = 'https://github.com/ran-j/PS2Recomp'
$PS2RECOMP_COMMIT = '2c5fbb9389e11dd95693385969490c9e8e6f57b4'   # 2026-10-05

$src = Join-Path $RDR_ROOT 'external\PS2Recomp'
$patch = Join-Path $RDR_ROOT 'patches\ps2recomp-runtime.patch'

if (-not (Test-Path $src)) {
    git clone $PS2RECOMP_URL $src
}
git -C $src fetch --quiet origin
git -C $src checkout --quiet $PS2RECOMP_COMMIT
if ($LASTEXITCODE) { throw 'checkout do PS2Recomp falhou' }

if ((Test-Path $patch) -and (Get-Item $patch).Length -gt 0) {
    git -C $src apply --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) { git -C $src apply $patch; 'patch aplicado' } else { 'patch ja aplicado (ou conflito: verifique)' }
}

$build = Join-Path $RDR_ROOT 'out\tools'
cmake -S $src -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
if ($LASTEXITCODE) { throw 'configuracao do CMake falhou' }
cmake --build $build
if ($LASTEXITCODE) { throw 'compilacao das ferramentas falhou' }

Get-ChildItem $build -Recurse -Include ps2_recomp.exe, ps2_analyzer.exe | Select-Object -ExpandProperty FullName
