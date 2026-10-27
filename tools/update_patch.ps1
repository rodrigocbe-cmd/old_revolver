# Regenera patches/ps2recomp-runtime.patch a partir do submódulo external/PS2Recomp.
# Arquivos novos (não rastreados) do runtime/recompilador/IOP entram no patch automaticamente.
# O C++ gerado a partir do ELF (ps2xRuntime/src/runner/, include/ps2_recompiled_*.h) é derivado do jogo
# e NUNCA entra: register_functions.cpp é rastreado no PS2Recomp e seria incluído por um "git diff" puro.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sub = Join-Path $root 'external\PS2Recomp'
$patch = Join-Path $root 'patches\ps2recomp-runtime.patch'
$exclude = @(':(exclude)ps2xRuntime/src/runner', ':(exclude)ps2xRuntime/include/ps2_recompiled_functions.h',
    ':(exclude)ps2xRuntime/include/ps2_recompiled_stubs.h')

$new = @(git -C $sub ls-files --others --exclude-standard -- ps2xRuntime ps2xRecomp ps2xIOP @exclude)
foreach ($f in $new) { git -C $sub add -N -- $f 2>$null }
$before = if (Test-Path $patch) { (Get-FileHash $patch).Hash } else { '' }
$diff = git -C $sub -c core.safecrlf=false diff -- . @exclude 2>$null
[System.IO.File]::WriteAllText($patch, (($diff -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding $false))

$files = (Select-String -Path $patch -Pattern '^diff --git' | Measure-Object).Count
$size = [math]::Round((Get-Item $patch).Length / 1KB)
$state = if ((Get-FileHash $patch).Hash -ne $before) { 'atualizado' } else { 'sem mudança' }
$newText = if ($new) { "novos: $($new -join ', ')" } else { 'nenhum arquivo novo' }
"patch ${state}: $files arquivos, $size KB ($newText)"
if (Select-String -Path $patch -Pattern '^diff --git a/ps2xRuntime/src/runner/' -Quiet) { 'ERRO: o patch contém código gerado (src/runner)'; exit 1 }
