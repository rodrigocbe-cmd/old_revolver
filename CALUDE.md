# Red Dead Revolver (PS2) — Teste de Recompilação Estática

> Contexto para agentes de IA no VS Code (Claude, Copilot, Cursor etc.).
> Leia este arquivo inteiro antes de qualquer ação e mantenha `docs/ESTADO.md` atualizado ao fim de cada sessão.

## 1. Objetivo

Experimento de **recompilação estática** do *Red Dead Revolver* (PlayStation 2, Rockstar San Diego, 2004) para um executável nativo de Windows x64, usando o **PS2Recomp** (https://github.com/ran-j/PS2Recomp).

O código MIPS R5900 do executável original (o ELF do disco) é traduzido **antes da execução** para C++, compilado com MSVC e ligado ao `ps2xRuntime`, que reimplementa o hardware e o sistema do PS2 (kernel do EE, IOP, GS, VU, DMA, SPU2, CD/DVD).

**Isto é um teste exploratório**, não um port jogável. A meta realista da primeira fase é: o executável compila, inicializa e chega o mais longe possível no boot, com cada bloqueio documentado.

## 2. Regras inegociáveis

- **Nenhum arquivo do jogo vai para o repositório**: ISO, ELF, módulos IRX, arquivos de dados, nem o C++ gerado a partir do ELF (ele é derivado do código da Rockstar). Tudo isso fica em pastas ignoradas pelo git (`game/`, `out/`, `generated/`).
- Nunca adicionar links ou instruções para baixar o jogo. O usuário usa a **própria cópia**.
- Endereços de função valem **só para o build exato** do ELF (região, versão). Sempre registrar o CRC32/SHA-256 do ELF usado em `docs/ESTADO.md`.
- Não editar o código gerado à mão. Correções vão em: configuração TOML, patches de instrução, *game overrides* ou no runtime.
- Mudanças no PS2Recomp ficam num patch versionado (`patches/ps2recomp-runtime.patch`) sobre um commit fixo, nunca soltas no submódulo.

## 3. Alvo

| Item | Valor |
|---|---|
| Jogo | Red Dead Revolver |
| Plataforma | PS2 |
| Região alvo | NTSC-U (USA) — **confirmar o serial pelo nome do ELF na raiz do disco** (formato `SLUS_XXX.XX`) |
| CPU principal | Emotion Engine — MIPS R5900 (64/128 bits, instruções MMI, VU0 em modo macro) |
| Coprocessadores | VU0/VU1 (microcódigo próprio), GS (vídeo), IOP R3000A (som, disco, controle), SPU2 |
| Hipótese a investigar | Motor da Angel Studios/Rockstar San Diego; o jogo usa muitas cutscenes em vídeo (FMV). Verificar o codec (provavelmente IPU/MPEG-2) |

Preencha o serial, os hashes e o tamanho do ELF em `docs/ESTADO.md` assim que o usuário extrair o executável.

## 4. Estrutura do repositório

```
.
├── AGENTS.md                     # este arquivo
├── .recomp.json                  # descrição do projeto (opcional, sem arquivos do jogo)
├── config/
│   ├── recomp.template.toml      # config do ps2_recomp com placeholders (@ELF@, @MAP@, @OUT@)
│   └── funcmap.csv               # mapa de funções exportado do Ghidra
├── docs/
│   ├── ESTADO.md                 # estado atual, último PC alcançado, bloqueios, próximos passos
│   ├── ARQUITETURA.md            # pipeline e decisões técnicas
│   └── FUNCOES.md                # funções identificadas (nome, endereço, papel, status)
├── game/                         # ELF do usuário (IGNORADO pelo git)
├── patches/
│   └── ps2recomp-runtime.patch   # alterações no PS2Recomp sobre o commit fixado
├── scripts/
│   ├── 1_setup.ps1               # instala/valida ferramentas, clona PS2Recomp no commit fixo
│   ├── 2_recompile.ps1           # gera o C++ a partir do ELF + TOML
│   ├── 3_build.ps1               # compila o runner nativo (CMake + Ninja + MSVC)
│   └── 4_run.ps1                 # roda por N segundos e salva log em logs/
├── src/
│   └── rdr_overrides.cpp         # game overrides específicos do Red Dead Revolver
├── tools/                        # utilitários (extração da ISO, scripts do Ghidra)
├── logs/                         # IGNORADO pelo git
└── out/                          # build e C++ gerado (IGNORADO pelo git)
```

`.gitignore` mínimo: `game/`, `out/`, `generated/`, `logs/`, `*.iso`, `*.elf`, `SLUS_*`, `SLES_*`, `SLPM_*`, `*.irx`.

## 5. Ferramentas

- Windows 10/11 x64, **Visual Studio 2022 Build Tools** (carga C++), CMake ≥ 3.20, Ninja, Git
- Compilador C++20; CPU com SSE4/AVX (exigido por partes vetoriais do runtime)
- **Ghidra** com suporte a PS2 (plugin/loader de ELF R5900) e o script `ps2xRecomp/tools/ghidra/ExportPS2Functions.java`
- **PCSX2** (com debugger) para comparação A/B: rodar o jogo original, pôr breakpoints e comparar registradores e memória com o executável recompilado
- 16 GB de RAM recomendados; o C++ gerado tem milhares de arquivos

## 6. Como o PS2Recomp funciona (resumo para o agente)

Módulos:

- `ps2xAnalyzer`: analisa o ELF e gera um TOML (stubs, skip, patches). Use como plano B; para jogos comerciais sem símbolos, prefira o fluxo com Ghidra.
- `ps2xRecomp` (`ps2_recomp`): lê TOML + ELF, decodifica R5900 e gera C++ literal (ex.: `addiu $r4,$r4,0x20` → `ctx->r4 = ADD32(ctx->r4, 0X20);`).
- `ps2xRuntime`: memória do guest, tabela de funções, despacho de syscalls, stubs de GS/VU/arquivos.
- `ps2xIOP`: executa os módulos IRX originais (R3000A) com fallbacks HLE.

Comportamentos importantes:

- `stubs` aceita nome ou `handler@0xENDERECO` para ELF sem símbolos (ex.: `sceCdRead@0x00123456`).
- Retornos genéricos para triagem: `ret0@0x...`, `ret1@0x...`, `reta0@0x...`.
- `skip` não recompila a função e gera `ps2_stubs::TODO_NAMED(...)`.
- `J/JAL` não resolvidos caem em `runtime->lookupFunction(0x...)`; um "function not found" no log quase sempre indica limite de função faltando no mapa do Ghidra.
- `SYSCALL` vai para `runtime->handleSyscall(...)`.
- Game overrides: `ps2xRuntime/include/game_overrides.h`, macro `PS2_REGISTER_GAME_OVERRIDE(name, elfName, entry, crc32, applyFn)` e `ps2_game_overrides::bindAddressHandler(runtime, addr, "handler")`.
- Limitações conhecidas: VU e GS são muito lentos, e boa parte do hardware ainda é stub.

Sempre confira o README e a wiki do PS2Recomp no commit fixado. A API muda rápido; se algo neste arquivo divergir do código, **o código vence**, e este arquivo deve ser atualizado.

## 7. Configuração base (`config/recomp.template.toml`)

```toml
[general]
input = "@ELF@"
ghidra_output = "@MAP@"
output = "@OUT@"
single_file_output = false
low_memory_mode = false
output_worker_threads = 0
patch_syscalls = false
patch_cop0 = false
patch_cache = false

# Começar mínimo. Só adicionar stubs/skip com justificativa registrada em docs/FUNCOES.md.
stubs = []
skip = []

[patches]
instructions = []
```

## 8. Fluxo de trabalho

### Fase 0 — Preparação
1. O usuário extrai o ELF da raiz do disco para `game/`. Registrar o serial, o CRC32 e o SHA-256.
2. Listar os IRX e os principais arquivos de dados do disco (só nomes e tamanhos, nunca o conteúdo) em `docs/ESTADO.md`.
3. Fixar o commit do PS2Recomp e registrá-lo.

### Fase 1 — Análise
1. Abrir o ELF no Ghidra e rodar a autoanálise.
2. Identificar as bibliotecas do SDK da Sony linkadas estaticamente (sceCd*, sceSif*, sceGs*, sceDma*, scePad*, sceMc*, libsd/sdr), usando as strings de versão das libs e padrões conhecidos.
3. Exportar o TOML e o CSV com `ExportPS2Functions.java` para `config/`.
4. Documentar em `docs/FUNCOES.md`: entry point, `main`, loop principal, carregadores de arquivo e chamadas de vídeo.

### Fase 2 — Primeira recompilação
1. `scripts/2_recompile.ps1` → C++ em `out/generated/`.
2. `scripts/3_build.ps1` → `out/build/<runner>.exe`.
3. Corrigir erros de geração e compilação no TOML ou no runtime, nunca no C++ gerado.

### Fase 3 — Iteração de boot (loop recomendado pelo PS2Recomp)
1. Rodar com a configuração mínima, sem `skip` agressivo.
2. Corrigir primeiro os bloqueios duros: `function not found`, syscall TODO, stubs críticos de IO/CD.
3. Usar `ret0/ret1/reta0` **só** para classificar a importância de uma chamada, e anotar cada um como temporário.
4. Promover correções temporárias a implementações reais.
5. Mover hacks específicos do jogo para `src/rdr_overrides.cpp`, chaveados pelo CRC do ELF.
6. Testar de novo do boot frio após cada lote de mudanças.
7. Comparar com o PCSX2 sempre que o comportamento divergir.

## 9. Marcos

| # | Marco | Status |
|---|---|---|
| 1 | ELF extraído e identificado (serial, hashes) | ⏳ |
| 2 | Mapa de funções exportado do Ghidra | ⏳ |
| 3 | C++ gerado sem erros | ⏳ |
| 4 | Executável nativo compila (MSVC x64) | ⏳ |
| 5 | Boot: heap, threads e kernel do EE inicializados | ⏳ |
| 6 | Módulos IOP carregados (IRX originais ou HLE) | ⏳ |
| 7 | Leitura de arquivos do disco funcionando | ⏳ |
| 8 | Primeira imagem na tela (logos, tela legal) | ⏳ |
| 9 | Menu principal + entrada do controle | ⏳ |
| 10 | FMV reproduzindo | ⏳ |
| 11 | Primeira fase carregada (gameplay) | ⏳ |
| 12 | Áudio | ⏳ |
| 13 | Velocidade de jogo real (≥ 30 FPS) | ⏳ |

## 10. Pontos de atenção específicos do PS2

- **VU1/VIF**: a geometria quase sempre passa por microprogramas VU1 enviados via VIF1. Identificar os microprogramas cedo; projetos como o `ico-recomp` também recompilam o microcódigo de VU1 estaticamente.
- **GS**: a VRAM é entrelaçada e há muitos truques de framebuffer. Avaliar como backend o fork com OpenGL usado no Shadow of the Colossus/God of War ou o paraLLEl-GS.
- **Self-modifying code / overlays**: verificar se o jogo carrega código em tempo de execução (overlays/módulos). Se sim, cada overlay precisa ser recompilado separadamente.
- **Sincronização**: vsync, interrupções do GS e semáforos do EE costumam causar travamentos silenciosos no boot. Logar a espera por eventos.
- **FPU**: o R5900 não é IEEE 754 completo (sem denormais/infinitos, clamp). Diferenças de física e colisão podem vir daqui.
- **DVD-9**: se o disco tiver duas camadas, garantir que a extração cobre ambas.

## 11. Convenções para o agente

- Antes de cada sessão: ler `docs/ESTADO.md`. Ao final: atualizar o último PC/função alcançada, o que mudou, os bloqueios e os próximos passos.
- Toda mudança de stub, skip ou override deve ser registrada em `docs/FUNCOES.md` com: endereço, nome (ou hipótese), motivo, se é temporária e como foi verificada.
- Commits pequenos, mensagens no formato `area: descrição` (ex.: `config: bind sceCdRead`, `runtime: implementa syscall 0x3C`).
- Ao rodar o jogo, sempre salvar o log em `logs/` com data e hora e citar o trecho relevante na análise.
- Ao propor uma hipótese sobre o jogo (motor, formato de arquivo, função), marcar como **hipótese** até confirmar no Ghidra ou no PCSX2.
- Não inventar endereços. Todo endereço citado deve vir do Ghidra, do log ou do debugger.

## 12. Referências

- PS2Recomp: https://github.com/ran-j/PS2Recomp (README + wiki)
- God of War (PS2) recomp, pipeline completo e bem documentado: https://github.com/KIexster/god-of-war-recomp
- ICO recomp, com VU1 recompilado e renderizadores GS: https://github.com/nathanialf/ico-recomp
- Tobiichi-Port (GoW, com launcher Qt): https://github.com/YYOzcan/Tobiichi-Port
- PCSX2 (referência de comportamento do hardware): https://github.com/PCSX2/pcsx2
- Especificação do `.recomp.json`: https://recomp.fyi/spec
