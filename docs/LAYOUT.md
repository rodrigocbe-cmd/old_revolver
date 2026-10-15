# Layout da versão final (proposta, inspirada no GTA Vice City PC)

Objetivo: a versão nativa do Red Dead Revolver deve ter arquivos soltos e organizados por tipo,
como o GTA Vice City para PC, para facilitar mods (trocar vídeos, áudio, textos, texturas).
Nenhum arquivo do jogo vai para o git: a pasta final (`dist/`) é gerada a partir da ISO do usuário
por scripts deste repositório e fica ignorada pelo git, como `game/`.

## Referência: GTA Vice City (PC)

```
gta-vc.exe, Mss32.dll
anim/     animações (ped.ifp, cuts.img)       models/  modelos e texturas (gta3.img, *.txd)
Audio/    efeitos e falas (*.wav, *.mp3)       movies/  vídeos (Logo.mpg, GTAtitles.mpg)
data/     configs e mapas em texto (*.dat,     skins/   skins do jogador (.bmp)
          *.ide, *.ipl, maps/, paths/)         TEXT/    textos por idioma (american.gxt, ...)
mp3/      músicas do usuário (rádio MP3)       txd/     telas de carregamento/intro (*.txd)
mss/      plugins do driver de áudio
```

Princípios que valem para o RDR: um tipo de conteúdo por pasta; nomes legíveis; formatos que
ferramentas comuns abrem (mpg, wav/mp3, txt); arquivos soltos têm prioridade sobre os empacotados.

## Disco do RDR (SLUS-20500)

| No disco | Tamanho | Conteúdo (hipótese até confirmar no Ghidra/PCSX2) |
|---|---|---|
| `PSS/*.PSS` (22) | 179 MB | vídeos MPEG-2 + áudio (logos, cutscenes): `rslogo`, `ATTRACT`, `BRIDGE01`… |
| `STREAMS.DAT` | 1,2 GB | áudio em streaming (música, falas) — **hipótese** |
| `BANKS.DAT` | 22 MB | bancos de efeitos sonoros (SPU2 ADPCM) — **hipótese** |
| `SWAT.DAT` | 290 MB | dados globais do motor SWAT (modelos, texturas, scripts) — **hipótese** |
| `FRONT.DAT` | 4 MB | front-end: menus, fontes, textos — **hipótese** |
| `PUPSHOW.DAT` | 264 MB | desconhecido ("pup show": galeria/extras?) — **hipótese** |
| `LVLS_SNG/*.DAT` (35) | 198 MB | fases single-player |
| `LVLS_M2/`, `LVLS_M4/` (13+13) | 59 MB | arenas multiplayer 2 e 4 jogadores |
| `SYSTEM/*.IRX`, `IOPRP280.IMG` | 0,6 MB | módulos do IOP (som, controle, memory card) |

## Layout proposto (`dist/`)

```
RedDeadRevolver.exe            executável recompilado + DLLs (FFmpeg, SDL2 ...)
movies/      *.pss  (fase C: aceitar também .mp4/.mpg com o mesmo nome)
audio/
  streams/   faixas extraídas do STREAMS.DAT (fase C: .wav/.ogg)
  banks/     bancos do BANKS.DAT
data/        conteúdo do SWAT.DAT (fase C: extraído em pastas)
frontend/    FRONT.DAT (menus, fontes) — fase C: text/ separado
text/        textos por idioma (extraídos do FRONT/SWAT, fase C)
levels/
  single/    LVLS_SNG/*.DAT
  mp2/       LVLS_M2/*.DAT
  mp4/       LVLS_M4/*.DAT
extras/      PUPSHOW.DAT
system/      IRX, IOPRP280.IMG, SYSTEM.CNF (não editar)
saves/       memory card (hoje game/disc/mc0)
mods/        sobreposição: qualquer arquivo aqui, no mesmo caminho relativo, vence o original
```

## Como chegar lá (fases)

**Fase A — disco virtual por arquivos soltos (viável agora).** Hoje o runtime lê o disco da ISO
(override `src/rdr_overrides.cpp`) porque o jogo lê por LSN (`sceCdRead`, sem `sceCdSearchFile`).
O runtime já tem uma tabela LSN → arquivo do host (`g_cdFilesByKey` em `Kernel/Stubs/Helpers/Support.h`).
Um script lê o diretório ISO9660 da ISO, copia cada arquivo para o lugar do layout acima e grava um
manifesto `system/disc_map.txt` (caminho no disco ↔ LSN ↔ arquivo em `dist/`). O override carrega o
manifesto e registra os arquivos; a ISO deixa de ser necessária. Leituras de `cdrom0:\...` por
fileio (`sceOpen`) passam pelo mesmo mapeamento.

**Fase B — pasta `mods/` (troca 1:1).** O mapeamento procura primeiro `mods/<caminho>`. Funciona
direto para arquivos com o mesmo tamanho. Para tamanhos diferentes é preciso controlar o índice
de arquivos do próprio jogo: identificar no Ghidra a busca de arquivo do motor (candidata:
`FUN_004079d0`, usada pelo player de FMV para obter LSN e tamanho) e ligar a um override que
consulte o manifesto em vez do índice do disco.

**Fase C — desempacotar os `.DAT`.** Exige descobrir os formatos (cabeçalho, tabela de entradas,
compressão) no Ghidra a partir das funções que os abrem. Para cada formato: um extrator
(`tools/`) que gera arquivos soltos e um caminho no override que serve esses arquivos ao jogo.
Vídeos: o HLE de MPEG usa FFmpeg, então aceitar `.mp4`/`.mpg` no lugar de `.pss` é um passo
pequeno quando a fase B existir.

## Pendências para decidir

- Nome do executável final e se `dist/` fica dentro do repositório (ignorada) ou em outra pasta.
- Prioridade da fase C: vídeos e textos primeiro (mods mais comuns), depois áudio, texturas, modelos.
