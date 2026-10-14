# Estado do projeto

_Última atualização: 2026-10-13_

## Marco atual
Marcos 1–3 alcançados na 1ª passada (ELF identificado; mapa do Ghidra com 12.580 funções + 62.414
rótulos; C++ gerado: 74.947 funções recompiladas, 47 stubs, 0 falhas de decodificação).
Refazendo 2–3 com o ELF de seções separadas. Próximo: marco 4 (compilar o runner).

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
| JDK | Eclipse Temurin 21.0.12.1 — `C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot` |
| Ghidra | 12.1.4 (`ghidra_12.1.4_PUBLIC_20260921.zip`, SHA-256 `ddac49f9…d2d4db` conferido) — `%LOCALAPPDATA%\Programs\ghidra_12.1.4_PUBLIC` |
| Extensão EE | ghidra-emotionengine-reloaded v2.1.38 (build 12.1.4) — `%APPDATA%\ghidra\ghidra_12.1.4_PUBLIC\Extensions` |
| PCSX2 | instalado pelo usuário |

Projeto Ghidra: `out/ghidra/RDR.gpr` (ELF descomprimido importado como `r5900:LE:32:default`, ainda sem autoanálise).

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
| `scripts/1_setup.ps1` | clona/fixa PS2Recomp, aplica patch, compila ferramentas em `out/tools` |
| `scripts/2_recompile.ps1 [-Export]` | exporta mapa do Ghidra → `config/funcmap.csv` + `config/ghidra_export.toml`; mescla com `config/project.toml` → `out/recomp.toml`; gera C++ em `out/generated` |
| `scripts/3_build.ps1` | copia o C++ para o runner e compila `ps2EntryRunner` em `out/build` |
| `scripts/4_run.ps1 [-Seconds N]` | roda por N s com `game/disc` como cdrom0, log em `logs/` |

## Bloqueios
- Nenhum no momento.

## Próximos passos
1. Concluir a autoanálise do Ghidra e exportar (`2_recompile.ps1 -Export`).
4. Conferir no PCSX2 (breakpoint em 0x00100008) se a RAM em 0x00100000 bate com a imagem descomprimida.
