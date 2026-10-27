# Red Dead Revolver (PS2) — recompilação estática para Windows x64

O método de trabalho (investigar, verificar, agir em erros) está em
`.github/instructions/metodo-de-trabalho.instructions.md`. As regras completas do projeto estão em
`CLAUDE.md`; o estado atual está em `docs/ESTADO.md`. Leia os dois antes de começar.

## O que é

O executável MIPS R5900 do *Red Dead Revolver* (PS2, NTSC-U, SLUS-20500 v1.03) é traduzido para C++
pelo **PS2Recomp** (`external/PS2Recomp`), compilado com MSVC e ligado ao `ps2xRuntime`, que
reimplementa o hardware do PS2 (kernel do EE, IOP, GS, VU1, DMA/VIF/GIF, SPU2, CD).

Estado: boot, menu, FMV e a primeira fase (tutorial) funcionam. Falta áudio e velocidade: o gameplay
roda a ~7,5 fps. O gargalo atual é a CPU na thread do jogo.

## Regras que não podem ser quebradas

- **Nada do jogo vai para o git**: ISO, ELF, IRX, dados do disco, C++ gerado, dumps de VU. Ficam em
  `game/`, `ROM/`, `out/`, `logs/` (ignorados).
- **Não edite o C++ gerado** (`out/generated/`, `ps2xRuntime/src/runner/`). Correções vão para:
  `config/` (TOML), `src/rdr_overrides.cpp` (overrides do jogo), o runtime ou o recompilador.
- **Mudanças no PS2Recomp** ficam versionadas em `patches/ps2recomp-runtime.patch`. Depois de editar
  o submódulo, regenere: `git -C external/PS2Recomp diff > patches/ps2recomp-runtime.patch`
  (arquivo novo precisa antes de `git -C external/PS2Recomp add -N <arquivo>`).
- **Endereços** valem só para o ELF descomprimido (CRC32 `EF570C27`). Todo endereço citado tem de
  vir do Ghidra, do log ou do debugger.
- **Sem commits**: o usuário commita. Sugira a mensagem como `area: descrição`.

## Estrutura

| Caminho | Conteúdo |
|---|---|
| `config/` | TOML do recompilador, mapa de funções do Ghidra (`funcmap.csv`) |
| `src/rdr_overrides.cpp` | game overrides e ferramentas de rastreio (`RDR_TRACE_CALLS`, `RDR_WATCH_ADDR`) |
| `scripts/1_setup.ps1` … `4_run.ps1` | setup, recompilação, build, execução (log em `logs/`) |
| `scripts/env.ps1` | ambiente do MSVC (vswhere, cmake, ninja) |
| `tools/` | descompactador do ELF, extração da ISO, scripts do Ghidra, `vu1_recomp.py`, `vu_dis.py`, `find_code_gaps.py` |
| `docs/ESTADO.md` | estado, medições, bloqueios, próximos passos |
| `docs/FUNCOES.md` | todas as intervenções numeradas (#1…#65) e as libs do SDK identificadas |
| `external/PS2Recomp` | recompilador + runtime (com o patch aplicado) |
| `external/parallel-gs` | GS em Vulkan (LGPL-3.0), backend opcional |
| `out/build/ps2xRuntime/ps2EntryRunner.exe` | executável gerado |

Dentro do runtime (`external/PS2Recomp/ps2xRuntime/src/lib/`):
- `gs/` — GS: `gs_frontend.cpp` (registradores, pacotes GIF), `gs_cpu_backend.cpp` (rasterizador
  em software), `gs_threaded_backend.cpp` (rasterização em faixas paralelas),
  `gs_parallel_bridge.cpp` (ponte para o paraLLEl-GS), `ps2_gif_arbiter.cpp`.
- `vu/` — VU1: interpretador ciclo a ciclo (`ps2_vu1_core.cpp`) e microprogramas recompilados
  (`ps2_vu1_compiled.{h,cpp}` + `out/vu1gen/`, gerados por `tools/vu1_recomp.py`).
- `Kernel/` — escalonador do EE (`EeScheduler.cpp`), syscalls, stubs de bibliotecas da Sony.
- `ps2_memory.cpp` (DMA, GIF), `ps2_vif1_interpreter.cpp`, `ps2_runtime.cpp` (laço principal,
  `UploadFrame` = apresentação), `ps2_pad.cpp` (teclado/mouse → controle).
- IOP: `external/PS2Recomp/ps2xIOP/src/emulator/iop_emulator.cpp` (inclui HLE por assinatura).

## Build e execução

```powershell
.\scripts\3_build.ps1            # sincroniza o C++ gerado e compila (incremental)
.\scripts\3_build.ps1 -NoSync    # só recompila o runtime/runner
.\scripts\4_run.ps1 -Seconds 60  # roda e salva logs\run_<data>.log
```

- Mudar um header incluído por `ps2_runtime.h` dispara rebuild completo (~45 min). Prefira mudar
  `.cpp`.
- Se o exe estiver aberto, o link falha: feche o jogo ou renomeie o exe antigo.
- Gerar C++ de novo só com `scripts\2_recompile.ps1` (após mudar TOML ou o recompilador).

## Scripts que poupam contexto (use antes de ler arquivos grandes)

| Comando | O que devolve |
|---|---|
| `.\tools\measure.ps1 [-Gpu] [-Scenario gameplay\|menu] [-Env 'K=V,...'] [-SetBaseline]` | Roda o jogo num cenário fixo e imprime ~20 linhas: FPS em regime, série de `gs.finish`, erros agrupados, comparação com a referência (`logs\baseline_*.json`), caminho da última tela. Gameplay ~10 min, menu ~3 min. Não use o PC durante a medição (o roteiro manda teclas). |
| `python tools/run_report.py <log> [--baseline <json>]` | Mesmo relatório para um log já existente. |
| `python tools/run_report.py <build.log> --build` | Só os erros distintos do build (o `3_build.ps1` já chama em caso de falha). |
| `python tools/ask_local.py <arquivo> -q "pergunta" [--filter regex]` | Pergunta ao modelo local (Ollama, `qwen3-coder:30b`) sobre um arquivo grande e devolve poucas linhas. Confira no original antes de agir: o modelo pode errar. |
| `.\tools\update_patch.ps1` | Regenera `patches/ps2recomp-runtime.patch` (inclui arquivos novos, exclui o C++ gerado). |

Fluxo: mudar código → `3_build.ps1 -NoSync` → `measure.ps1` → comparar o número com a referência →
registrar em `docs/FUNCOES.md`/`docs/ESTADO.md` → `update_patch.ps1`. Nunca leia um log inteiro: use
`run_report.py` ou `ask_local.py`.

## Variáveis de ambiente úteis

| Variável | Efeito |
|---|---|
| `PS2X_STATS=1` | contadores por 600 ticks em `[run:draws]` (`gs.finish` = quadros, `ee.idle_ms`, `vu1.*`) |
| `PS2X_GS_BACKEND=parallel` | GS na GPU (paraLLEl-GS/Vulkan); sem ela, GS em software |
| `PS2X_GS_PARALLEL_LOG=1` | loga registradores de display a cada mudança |
| `PS2X_GS_NODRAW=1` | descarta primitivas (mede o teto sem rasterização) |
| `PS2X_VU1_FAST`, `PS2X_VU1_FAST_MATH` | modo rápido do VU1 (necessário para o VU1 compilado) |
| `PS2X_VU1_COMPILED=0` / `PS2X_VU1_COMPILED_VERIFY=<n>` | desliga / compara com o interpretador |
| `PS2X_IOP_HLE=0` | desliga o HLE por assinatura do IOP |
| `PS2X_WRITE_WATCH`, `RDR_WATCH_ADDR`, `RDR_TRACE_CALLS` | rastreio de escritas e chamadas |

Métrica de desempenho: `gs.finish` por 600 ticks (≈ quadros a cada 10 s). Referências no tutorial:
GS em software 62–63, GPU 75, teto sem desenho 171.

## Lições já aprendidas (não repetir os erros)

- `function not found` no log = limite de função faltando no mapa do Ghidra (ver
  `tools/find_code_gaps.py`).
- LQ/SQ/LQC2/SQC2 alinham o endereço em 16 bytes (`& ~0xF`); sem isso o personagem cai pelo chão.
- `SQRT.S` usa o operando `ft`, não `fs`.
- `PollSema` do EE retorna -1 em qualquer falha.
- O runtime não escreve `SMODE1` (no PS2 real é o kernel); o paraLLEl-GS precisa dele para o scanout.
- Os pacotes GIF do runtime são autocontidos; o paraLLEl-GS guarda estado por caminho. A ponte
  reinicia o caminho antes de cada pacote.
- Esc fechava o jogo (padrão do raylib); desligado com `SetExitKey(KEY_NULL)`.
- Hipóteses de desempenho já descartadas por medição: limite de dt, orçamento do IOP, HLE do IOP,
  IRQ do GS por borda.

## Próximos passos (ver `docs/ESTADO.md`)

1. Achar o gargalo de CPU com o backend da GPU (parse GIF na thread do jogo? readback síncrono do
   quadro?). Medir com profiler antes de mudar.
2. Terminar o tutorial (até a lição de zoom com o Red adulto).
3. Áudio (SPU2).
4. Mira com mouse estilo PC, suporte a `.pnach`, base fixa de memória para Cheat Engine.

## Controles (teclado/mouse → DualShock)

Mapeamento em `ps2xRuntime/src/lib/ps2_pad.cpp`:
WASD = analógico esquerdo, setas = direcional, X/Espaço = ✕, C/Esc = ○, Z = □, V = △,
Q/E = L1/R1, Shift esq./dir. = L2/R2, Enter = Start, Tab = Select, R = R3, F = L3.
Mouse (capturado ao clicar, F2 solta): movimento = analógico direito; botões e roda configuráveis
pelas variáveis `PS2X_MOUSE*`. F1 esconde o painel de debug.
