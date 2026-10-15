# Estado do projeto

_Última atualização: 2026-10-15 (desktop, sessão noturna)_

## Marco atual

**Resumo 2026-10-15 (19h45): MENU PRINCIPAL ALCANÇADO.** Fluxo: ... Demo Mode → Start → vídeo do
Red no deserto (título) → Start → **menu de livros** ("Player in Progress", "X Enter / O Options").
Correções: FUNCOES.md #30 (BindRpc IOP→EE 0xADDA fingido), #31 (ioman do IOP: open/read/lseek/close →
AGESND abre `BANKS.DAT`), #32 (ENDX do SPU2 = todas as vozes terminadas; sem isso o jogo esperava 18
vozes e caía num `b .` em 0x102B18), #33 (4 rodadas no `find_code_gaps.py`: 0x4BF098, 0x2BB100,
0x2B7FE0, 0x234500), #34 (rastreio `PS2X_RPC_TRACE`). Runtime: `PS2X_WINDOW_SIZE=WxH` abre a janela
maior (só escala a imagem). Preview: `out/preview/Jogar.bat`.
**Problemas visíveis:** fundo do menu com ruído colorido (textura da mesa errada?); ~10–12 fps; sem áudio.
**Próximo:** textura do fundo do menu; entrar num jogo (New Game); desempenho; áudio.
Roteiro de teste guiado pela tela: `scratchpad/drive.ps1` + `classify.py` (fora do repositório).

**Resumo 2026-10-15 (15h):** **marcos 8, 9 e 11 (parcial) alcançados.** Fluxo completo até agora:
logo Rockstar (FMV) → abertura / vídeo de demonstração → **Start** → checagem de memory card → tela
legal (motor) → tela de carregamento (tambor) → diálogo "save danificado, Retry?" (cartão emulado
vazio; responder **No** com S/seta para baixo segurado ~0,5 s e X) → aviso de Autosave (X) → LOADING →
**Demo Mode em 3D em tempo real** (personagens, armas, cenários). ~10–12 fps, sem áudio. Correções
desta etapa: FUNCOES.md #26 (COP0 Count), #27 (lacunas sem jr), #28 (GetInfo do memory card).
Teclado: Enter = Start, Espaço/X = X, C/Esc = Círculo, Z = Quadrado, V = Triângulo, WASD/setas =
direcional (enviar setas sintéticas exige a flag de tecla estendida). Próximo: Start no Demo Mode →
menu principal; desempenho (VU1/GS interpretados); áudio; conferir #25 no PCSX2.

**Resumo 2026-10-15 (13h):** marcos 8 e 9 (parcial) alcançados. Fluxo atual: logo da Rockstar
(FMV) → abertura cinematográfica / vídeo de demonstração em loop → **Start** (teclado Enter, libpad
via HLE) encerra o vídeo → checagem de memory card → **tela legal desenhada pelo motor do jogo**
(VU1 → XGKICK → GS). Correções desta etapa: FUNCOES.md #16 (listras do FMV, VIF1 DIRECT/IMAGE),
#17 (rastreador de funções desligado: jogo mais rápido), #18 (libpad), #19 (parada do vídeo),
#20 (DECI2 como console de varejo), #21 (+173 funções em regiões órfãs), #22 (mcserv), #23 (VIF1
contínuo entre pedaços do MFIFO), #24 (instrumentação), #25 (JR da VU1). Próximo: ver o que vem após
a tela legal (título/menu), conferir #25 no PCSX2, áudio.

**Resumo 2026-10-15 (manhã):** marcos 5–8 alcançados. O boot inicializa EE/IOP, carrega os 10 IRX,
lê o disco (ISO via override), monta a lista de desenho no MFIFO e **toca o vídeo de abertura da
Rockstar Games do começo ao fim** (FFmpeg via HLE de MPEG; sem som). Depois do vídeo o jogo
encontrou a 1ª função fora do mapa do Ghidra (`0x2CAE18`, chamada por ponteiro); 602 inícios de
função em lacunas do mapa foram adicionados (FUNCOES.md #15) e o rebuild está em andamento.
Intervenções #2–#15 em FUNCOES.md; mudanças do runtime em `patches/ps2recomp-runtime.patch`
(MFIFO, interrupção do GS, ReadChain, relógio do BlockTrans, opções de MPEG). Ferramentas novas:
`tools/ghidra/DumpRegion.java`, `tools/ghidra/DecompileFuncs.java`, `tools/sync_generated.py`,
`tools/find_code_gaps.py`. Diagnóstico: `[run:thread]`/`[run:sema]` a cada 600 ticks no log;
pilhas via `cdb` (WinDbg); capturas de tela via script (F1 esconde o painel de debug).

Marcos 1–3 alcançados com o ELF de seções separadas (sessão de 2026-10-13):
- Ghidra (reanalisado): 12.580 funções + 62.062 rótulos exportados → `config/funcmap.csv`.
- `ps2_runtime.lib` compilada sem erros (`out/build`, RelWithDebInfo; `logs/build_runtime_lib.log`).
- 2026-10-14: `entry_004fb340` (dados da libipu no `.text`) marcado como `skip` (FUNCOES.md #1).
  `ps2_recomp` (log `logs/recompile_20261014_184432.log`): 74.642 funções processadas,
  74.594 recompiladas, 47 stubs, 1 skip, **0 instruções não tratadas**, 0 falhas de decodificação.
  `out/generated` limpo (74.645 arquivos = funções + 3).

**Marco 4 alcançado (2026-10-14, desktop 32 GB / 16 threads):** `out/build/ps2xRuntime/ps2EntryRunner.exe`
(354 MB, RelWithDebInfo) com `3_build.ps1 -Jobs 12`: 2452 passos, 0 erros, ~47 min, RAM livre mínima
~11 GB (log `logs/build_20261014_205734.log`). Pipeline 0→3 refeito do zero no desktop: SHA-1 da ISO e
SHA-256 do ELF descomprimido conferem; `ps2_recomp` gerou os mesmos 74.645 arquivos.

**Primeiro boot (`logs/run_20261014_214439.log`):** raylib/OpenGL e áudio sobem, ELF carregado, execução
em 0x100008, `SetupHeap` ok. Depois da inicialização do SIF o processo **não termina nem avança** por
30 s; últimas linhas (stderr):
```
[sceSifGetReg] reg=0x80000002 value=0x0 pc=0x3cd4d8 ra=0x3cd4d8
[sceSifSetDma:CALL] pc=0x3cd114 ra=0x3cd114 list=0x1f7fbe0 count=1
[sceSifSetDma:DESC] i=0 src=0x7673c0 dst=0x0 size=0x10 attr=0x44 pc=0x3cd114 ra=0x3cd114
```
**Lote 1 de stubs (FUNCOES.md #2, 2026-10-14, `logs/run_20261014_232108.log`):** o boot passou do
`sceSifInitRpc`. `SifInitRpc` HLE inicializado; reboot/sync do IOP ok; o IOP emulado (`ps2xIOP`)
carregou e iniciou os 10 IRX do jogo (`SIO2MAN`, `MTAPMAN`, `PADMAN`, `LIBSD`, `SDHD`, `SDRDRV` "SDR driver
version 4.0.1", `MODSEIN`, `MODMIDI`, `MODHSYN`, `AGESND`); `GsSetCrt(interlaced=1, NTSC)`; frames
enviados à janela (ainda pretos); 2 threads ativas.

**Lote 2 + override de CD (`logs/run_20261015_003107.log`):** passou do bind do cdvdfsv; o override
aponta a ISO (`[rdr] CD image: ...ROM\Red Dead Revolver (USA) (SLUS-20500).iso`); 3 threads. Parou em
`pc=0x46fb68` (`FUN_0046fa60`, chamado pelo laço de quadro `FUN_00236058`): espera
`SPR_FROM.MADR == VIF1.TADR` — o jogo usa o **MFIFO** do DMAC (FUNCOES.md #6), que o runtime não
emulava. Implementado no patch do runtime (`logs/run_20261015_014016.log`: `[MFIFO] drain ... tadr=fill`,
laço de 0x46FB68 resolvido).

**Depois do MFIFO:** o jogo chegou ao **primeiro vídeo** (FMV dos logos): parou em `0x497F90/98`
(`FUN_00497f48`, busca de start code MPEG via IPU, chamado pelo parser de cabeçalho `FUN_00498030`).
O runtime não emula a IPU; a libmpeg do jogo foi ligada ao HLE de MPEG/FFmpeg (lote 3, FUNCOES.md #7).

**Lote 3 (`logs/run_20261015_024819.log`):** o FFmpeg decodifica o primeiro vídeo (sequence header
`000001b3`, quadros 512x448) e `sceMpegGetPicture` entrega quadros ao jogo; o display muda
(`dispfb1=0x11000`); os quadros passam pelo MFIFO até o GS. Parou após 10 quadros: o player
sincroniza pela posição do áudio (`sceSdBlockTransStatus` via `sceSdRemote`), que não avançava pela
RPC do IOP → lote 4 (FUNCOES.md #8). Rebuild em andamento.

**Bloqueio anterior (resolvido no lote 2):** a `GameThread` ficava em `pc=0x3c7400 ra=0x3c7434`, dentro de `FUN_003c7338` (bind
dos comandos N da libcdvd): `while(1){ while(sceSifBindRpc(0x4fcc50, 0x80000595) < 0); if (cd->server
@0x4fcc74) break; espera; }`. O SID 0x80000595 (cdvdfsv) não é servido pelo IOP emulado (cdvdfsv vem
da ROM do IOP/IOPRP), então `server` fica 0 para sempre. Solução: ligar as funções de alto nível da
libcdvd (`sceCdRead`, `sceCdSync`, `sceCdSearchFile`, `sceCdDiskReady`, `sceCdGetError`, `sceCdSt*`…)
ao HLE do runtime (lote 2).

**Causa do 1º travamento (confirmada com cdb, pilha da `GameThread` com 20 s de execução, CPU ~1,25 núcleo):**
laço ativo em `entry_003cd508` (parte de `FUN_003cd3b0`):
```c
while (FUN_003ccc58(0) == 0) ;   // FUN_003ccc58(i) = *(u32*)(0x767300 + i*4)
sceSifSetReg(0x80000002, 1);
```
Padrão do `sceSifInitRpc` do SDK: espera o IOP gravar o SREG 0 e marca RPCINIT. `FUN_003ccc58` é o
`sceSifGetSreg` da libkernl (o nome é **hipótese** pelo padrão; lê o vetor de SREGs em 0x767300 na
RAM do guest). O `ps2xRuntime` emula os SREGs num mapa próprio (`g_sifSregs` em `SIF.cpp`) e tem
HLE para `sceSifInitCmd/InitRpc/BindRpc/CallRpc/GetSreg/...`, mas só os wrappers de syscall (47 stubs
do Ghidra, `SetGsCrt`…`Deci2Call`, 0x3CAE20–0x3CB650) estão ligados. As funções de biblioteca do
SIF/RPC (e, por extensão, libcdvd, libpad, libmc) são recompiladas do jogo e esperam um IOP real do
outro lado, que nunca responde.

## Disco
| Item | Valor |
|---|---|
| Arquivo | `ROM/Red Dead Revolver (USA) (SLUS-20500).iso` |
| Serial / versão | SLUS-20500, v1.03 (`SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_205.00;1`, `VER = 1.03`, `VMODE = NTSC`) |
| Mídia | DVD-5 (camada única), volume `REDDEADREVOLVER`, 1.142.816 setores |
| SHA-1 da ISO | `7bbe502a1b170f80e378a566ad8ec313dba78361` (idêntico ao Redump, disc 6380) |

## ELF original (`game/SLUS_205.00`)
| Item | Valor |
|---|---|
| Tamanho | 1.828.068 bytes |
| CRC32 | `F82FAB71` |
| SHA-256 | `2101613bc97f3f34eb965ff1d7af5adf465d85058292f4ad7a3cb350cebc8bae` |
| CRC PCSX2 (XOR de words) | `E169BAF8` (bate com a wiki do PCSX2) |
| Entry | `0x00800008` |

**O ELF é comprimido.** O `.text` (0x5D8 bytes em 0x00800000) é só um descompactador:
aPLib com XOR por byte de entrada (chave de 8 bits começando em 1, +1 a cada byte lido; o
primeiro literal é copiado sem XOR). Descompacta 0x00800600 → 0x00100000 e salta para 0x00100008.
Reimplementado em `tools/rdr_unpack.py`.

## ELF descomprimido (`game/SLUS_205.00.unpacked.elf`) — **alvo do Ghidra e do PS2Recomp**
| Item | Valor |
|---|---|
| Gerado por | `python tools/rdr_unpack.py game/SLUS_205.00 game/SLUS_205.00.unpacked.elf` |
| `.text` (R-X) | 0x00100000–0x004FEA80 (último código: `jr $ra` em 0x004FEA68, depois padding e tabelas) |
| `.data` (RW-) | 0x004FEA80–0x00549FE8 (rodata + data) |
| `.bss` | 0x00549FE8–0x007A26CC (do laço de limpeza do crt0) |
| Entry | `0x00100008` (crt0) |
| CRC32 | `EF570C27` |
| SHA-256 | `ba2252ccff1fe4e5b16f73fd2fef8847e1eb7db3610360e65518dc875fd1745a` |

Os endereços de função do projeto referem-se a este ELF descomprimido.

Histórico: a 1ª versão (CRC32 `FBD901B5`) tinha uma seção RWX única; o Ghidra marcou tabelas de
dados após 0x004FEA80 como código e o `ps2_recomp` gerou ~13 mil instruções inválidas em
`entry_00501120`/`entry_00523df8`. Corrigido separando `.text`/`.data`.

## O que já se sabe do executável
- SDK da Sony 2.8.0 linkado estaticamente: `PsIIlibcdvd/dma/graph/ipu/kernl/pad/sdr 2800`, `libmc 2810`, `libusbkb 2800`.
- Motor interno chamado **SWAT** (caminhos `c:\soft\swat\src\...`; arquivo `SWAT.DAT` no disco). Código C++.
- FMV: `.PSS` (MPEG-2) decodificados via libipu/libmpeg (`sceMpegGetPicture`). Confirma a hipótese do CALUDE.md.
- Carrega `cdrom0:\SYSTEM\IOPRP280.IMG` (reboot do IOP) e IRX de `cdrom0:\SYSTEM\`.
- Strings `host0:` presentes (resíduo de build de desenvolvimento).
- "wrong ucode overlay": overlays de microcódigo VU (não overlays de código do EE). **Hipótese**: não há código do EE carregado em runtime; a confirmar no Ghidra.

## Conteúdo do disco (nomes e tamanhos)
| Caminho | Tamanho (bytes) |
|---|---|
| `SLUS_205.00` | 1.828.068 |
| `SYSTEM.CNF` | 57 |
| `STREAMS.DAT` | 1.272.516.608 |
| `SWAT.DAT` | 304.035.840 |
| `PUPSHOW.DAT` | 276.621.312 |
| `BANKS.DAT` | 22.675.456 |
| `FRONT.DAT` | 4.462.592 |
| `LVLS_SNG/*.DAT` (35 fases single-player) | ~215 MB |
| `LVLS_M2/*.DAT`, `LVLS_M4/*.DAT` (13 + 13, multiplayer 2/4 jogadores) | ~57 MB |
| `PSS/*.PSS` (22 vídeos) | 187.629.656 |

Módulos IOP em `SYSTEM/`: `IOPRP280.IMG` (269.889), `AGESND.IRX` (58.200), `LIBSD.IRX` (28.605),
`MCMAN.IRX` (95.909), `MCSERV.IRX` (7.417), `MODHSYN.IRX` (61.069), `MODMIDI.IRX` (21.965),
`MODSEIN.IRX` (9.393), `MTAPMAN.IRX` (11.029), `PADMAN.IRX` (43.813), `SDHD.IRX` (14.685),
`SDRDRV.IRX` (9.161), `SIO2MAN.IRX` (6.641).

## Ferramentas instaladas
| Ferramenta | Versão / local |
|---|---|
| Visual Studio Community 2026 | carga C++ (MSVC + CMake) |
| Ninja | 1.13.2 (winget) |
| Desktop (2026-10-14) | VS Community 2026 + carga C++ (MSVC 14.51, CMake 4.3.1), Ninja 1.13.2, Python 3.14, Git 2.56; RTX 3060; JDK Temurin 21.0.12.1, Ghidra 12.1.4 (SHA-256 conferido) + extensão EE v2.1.38 (SHA-256 `e389ff3e…f240b`); WinDbg 1.2610 (`cdb.exe`, para pilhas do runner) |
| JDK | Eclipse Temurin 21.0.12.1 — `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot` |
| Ghidra | 12.1.4 (`ghidra_12.1.4_PUBLIC_20260921.zip`, SHA-256 `ddac49f9…d2d4db` conferido) — `%LOCALAPPDATA%\Programs\ghidra_12.1.4_PUBLIC` |
| Extensão EE | ghidra-emotionengine-reloaded v2.1.38 (build 12.1.4) — `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC\Extensions` |
| PCSX2 | instalado pelo usuário |

Projeto Ghidra: `out/ghidra/RDR.gpr`. No desktop o dono do projeto foi trocado de `RodrigoBechara` para
`rodri` em `RDR.rep/project.prp` (original em `project.prp.notebook`); ao levar de volta, restaurar.
Scripts headless do projeto em `tools/ghidra/` (`DumpRegion.java`, `DecompileFuncs.java`); o
`analyzeHeadless` precisa só de `JAVA_HOME` (não carregar o `env.ps1` com `2>`/`2>$null`, que quebra no aviso do vswhere).
Histórico: (ELF descomprimido importado como `r5900:LE:32:default`, ainda sem autoanálise).

## PS2Recomp
| Item | Valor |
|---|---|
| Repositório | https://github.com/ran-j/PS2Recomp |
| Commit fixado | `2c5fbb9389e11dd95693385969490c9e8e6f57b4` (2026-10-05, "Dispatch syscalls 0x79/0x7A…") |
| Local | `external/PS2Recomp` (clonado por `scripts/1_setup.ps1`) |
| Ferramentas | `out/tools/ps2xRecomp/ps2_recomp.exe`, `out/tools/ps2xAnalyzer/ps2_analyzer.exe` (MSVC 14.51, Release) |
| Patch local | nenhum ainda (`patches/ps2recomp-runtime.patch`) |

Observações do código no commit fixado:
- O runtime compila tudo em `ps2xRuntime/src/runner/*.cpp` e inclui `ps2_recompiled_*.h` de `ps2xRuntime/include` (ignorados pelo git do PS2Recomp). `3_build.ps1` copia `out/generated` e `src/*.cpp` para lá.
- `cdrom0:` = pasta do ELF passado ao runner. Por isso o disco está extraído em `game/disc/`, onde `4_run.ps1` renomeia o original para `SLUS_205.00.packed` e põe o ELF descomprimido como `SLUS_205.00`.
- No Windows o CMake baixa sozinho o FFmpeg pré-compilado (n7.1, LGPL) para os FMV.

## Pipeline
| Script | Faz |
|---|---|
| `scripts/env.ps1` | carrega vcvars64 (MSVC), CMake, Ninja, JAVA_HOME |
| `scripts/0_game.ps1 [-Iso caminho]` | confere o SHA-1 da ISO em `ROM/`, extrai o ELF e o disco para `game/`, gera o ELF descomprimido |
| `scripts/1_setup.ps1` | inicializa o submódulo `external/PS2Recomp` no commit fixado, aplica patch, compila ferramentas em `out/tools` |
| `scripts/2_recompile.ps1 [-Export]` | exporta mapa do Ghidra → `config/funcmap.csv` + `config/ghidra_export.toml`; mescla com `config/project.toml` → `out/recomp.toml`; gera C++ em `out/generated` |
| `scripts/3_build.ps1 [-NoSync] [-Jobs N] [-Config X]` | sincroniza (só o que mudou, `tools/sync_generated.py`) o C++ para o runner e compila `ps2EntryRunner` em `out/build`; impede suspensão durante o build. `-NoSync` retoma sem recopiar; `-Jobs` limita o paralelismo |
| `scripts/4_run.ps1 [-Seconds N]` | roda por N s com `game/disc` como cdrom0, log em `logs/` (stdout + stderr anexado) |

## Bloqueios
- **Boot travado em `sceSifInitRpc`** (ver "Marco atual"): funções de biblioteca do SIF/RPC do jogo
  não estão ligadas ao HLE do runtime.
- (Resolvido no desktop) **RAM insuficiente no notebook (8 GB) para o build do runner.** Em 2026-10-14 o build chegou a
  2075/2344 (88%, sem erros de compilação), mas os últimos blocos unity são pesados: com 16 `cl.exe`
  em paralelo o sistema entra em swap pesado e o Windows caiu duas vezes com bugcheck
  **0x9F DRIVER_POWER_STATE_FAILURE** (19:29 e 20:02), mesmo com suspensão bloqueada.
  Decisão: continuar no **desktop** (mais RAM). No notebook, só com `3_build.ps1 -NoSync -Jobs 2`
  (lento; o link final de ~75 mil funções com símbolos também pode exigir vários GB).

## Migração para outra máquina
Vai pelo git: scripts, tools, config (inclui `funcmap.csv` e `ghidra_export.toml`, então o Ghidra
**não** é necessário para compilar), docs, e o submódulo `external/PS2Recomp` (`.gitmodules`).
Levar à mão: a ISO (→ `ROM/`). Opcional: `out/ghidra/` (projeto Ghidra analisado, 58 MB).

Pré-requisitos: Git, Python ≥ 3.11 (usa `tomllib`), Visual Studio com a carga "Desenvolvimento
para desktop com C++" (traz MSVC e CMake), Ninja (`winget install Ninja-build.Ninja`).

```powershell
git clone --recurse-submodules https://github.com/rodrigocbe-cmd/old_revolver.git
cd old_revolver
# copiar a ISO para ROM\
.\scripts\0_game.ps1        # confere SHA-1, extrai ELF + disco, descomprime o ELF
.\scripts\1_setup.ps1       # PS2Recomp no commit fixado + ferramentas
.\scripts\2_recompile.ps1   # C++ em out\generated (sem -Export: usa o CSV do git)
.\scripts\3_build.ps1       # ps2EntryRunner.exe
.\scripts\4_run.ps1 -Seconds 30
```
Se o PowerShell bloquear os scripts: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
Conferir que o ELF descomprimido saiu com SHA-256 `ba2252cc…1745a` (ver acima).

## Próximos passos
1. Lote 2: identificar o resto da libcdvd (região ~0x3C6000–0x3C8040; comandos N via `FUN_003c7338`,
   comandos S via `FUN_003c7650`; chamadores do jogo `FUN_0025cac0`, `FUN_00407318`) e ligar ao HLE
   (`sceCdRead`, `sceCdSync`, `sceCdSearchFile`, `sceCdSt*`, …). Depois libpad/libmtap/libmc e o
   servidor RPC do lado EE (`FUN_004bd550`). Ver lista em FUNCOES.md.
2. Rebuild: com `tools/sync_generated.py` só o que mudou é copiado; mudança de stubs altera
   `ps2_recompiled_stubs.h` (incluído por todos) e ainda força build completo (~50 min).
3. Conferir no PCSX2 (breakpoint em 0x00100008) se a RAM em 0x00100000 bate com a imagem descomprimida.

Espaço: o disco extraído (2,2 GB) + C++ gerado + build com símbolos ocupam vários GB; deixar ≥ 20 GB livres.

## Notas de ambiente
- Scripts `.ps1` só em ASCII (PowerShell 5.1 lê sem BOM como ANSI).
- Logs dos scripts saem em UTF-8 (`Out-File -Encoding utf8`); o `*>` do PS 5.1 grava UTF-16.
- O aviso "'vswhere.exe' não é reconhecido" ao carregar o `vcvars64.bat` é inofensivo.
- Ghidra headless precisa de `JAVA_HOME` (o `env.ps1` define). Projeto: `out/ghidra/RDR.gpr`.
- Ferramentas auxiliares temporárias (venv com capstone para desmontar) ficaram fora do projeto,
  no scratchpad da sessão; recriar se necessário (`pip install capstone`).
