# Prepara os arquivos do jogo a partir da ISO do usuario (nada disto vai para o git):
#   game/SLUS_205.00               ELF original do disco
#   game/SLUS_205.00.unpacked.elf  ELF descomprimido (alvo do Ghidra/PS2Recomp)
#   game/disc/                     disco inteiro extraido (raiz do cdrom0: no runner)
param([string]$Iso = '')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $Iso) { $Iso = Get-ChildItem (Join-Path $root 'ROM') -Filter *.iso | Select-Object -First 1 -ExpandProperty FullName }
if (-not $Iso -or -not (Test-Path $Iso)) { throw 'ISO nao encontrada: coloque-a em ROM/ ou passe -Iso <caminho>' }

$expected = '7bbe502a1b170f80e378a566ad8ec313dba78361'
"conferindo SHA-1 da ISO..."
$sha1 = (Get-FileHash $Iso -Algorithm SHA1).Hash.ToLower()
if ($sha1 -ne $expected) { throw "SHA-1 $sha1 difere do Redump ($expected): o projeto mira SLUS-20500 v1.03" }

$game = Join-Path $root 'game'
$disc = Join-Path $game 'disc'
$tool = Join-Path $root 'tools\iso_extract.py'
python -I $tool extract $Iso $game /SLUS_205.00 | Out-Null
if ($LASTEXITCODE) { throw 'falha ao extrair o ELF' }
if (-not (Test-Path $disc)) {
    "extraindo o disco para game/disc (2,2 GB)..."
    python -I $tool extract $Iso $disc | Out-Null
    if ($LASTEXITCODE) { throw 'falha ao extrair o disco' }
}
python -I (Join-Path $root 'tools\rdr_unpack.py') (Join-Path $game 'SLUS_205.00') (Join-Path $game 'SLUS_205.00.unpacked.elf')
if ($LASTEXITCODE) { throw 'falha ao descomprimir o ELF' }
