# Estado do projeto

_Última atualização: 2026-10-15 (desktop, sessão noturna)_

## Marco atual

**2026-10-29 (#72):** build estável ~26 fps no gameplay (256–264 FINISH/600), **GS na GPU por padrão** (o exe roda assim sem variáveis). Eliminação de flags mortas do VU1 confirmada como causa do travamento intermitente (3/7 contra 0/6, `tools/soak.ps1`); fica desligada. Para os próximos testes de estabilidade, use `tools/soak.ps1 -Runs N -Gpu`.

**2026-10-28 (noite, #71):** build atual ~26–27 fps no gameplay, estável. Eliminação de flags mortas do VU1 desligada (travou o jogo 2/2; investigar com `--dead-flags`). Memória ao vivo: `tools/ee_mem.py` + `tools/mem_bisect.py` + `docs/MEMORIA.md`. O número 156 do HUD não reagiu a escrita direta (u32/s32/u16/f32): hipótese de que o HUD só redesenha em eventos; identificar vida/balas/dinheiro com mudanças reais durante o jogo (atirar, tomar dano) e `ee_mem.py next changed/dec`.

**2026-10-28 (fim da tarde, #70):** gameplay com GPU **275 FINISH/600 (~27,5 fps)** com o GS assíncrono (submissão ao paraLLEl-GS numa thread própria); referência em `logs/baseline_gameplay_gpu.json`. `tools/drive.ps1` agora é guiado pela tela (título → Start, Demo Mode → Start, menu reconhecido pela aparência): com o jogo mais rápido os tempos fixos caíam na demo e a medição saía inválida (~520–600, demo).

**2026-10-28 (tarde, #69):** IOP numa thread própria do host: gameplay com GPU **199 FINISH/600 (~20 fps)**, o degrau de 3 VBlanks por quadro. Referência em `logs/baseline_gameplay_gpu.json`. Próximo degrau: 30 fps (2 VBlanks) exige ~33% a menos de trabalho por quadro na thread do jogo; refazer o perfil.

**2026-10-28 (fim da manhã, #68):** gameplay com GPU **152–153 FINISH/600 (~15 fps)**; referência em `logs/baseline_gameplay_gpu.json`. Hipótese: o jogo espera VBlank por quadro, então o FPS anda em degraus (153 ≈ 4 VBlanks por quadro; 3 VBlanks = 200/600 exigiria ~20% a menos de trabalho por quadro). IOP: o laço do LIBSD sumiu com o ritmo do AutoDMA; agora o IOP gasta o teto de 25% no laço de serviço do AGESND (`0x3d180–0x3d340`), que não pode cair para 5% (trava). Próximos: thread própria para o IOP; ou tornar nativo o laço de serviço do AGESND; e seguir enxugando o VU1 (lower/flags).

**VU1 estático (2026-10-28, #67):** gameplay com GPU **139 FINISH/600 ticks (~13,9 fps)**, antes 74; verificado 160/160 contra o interpretador. Referência gravada em `logs/baseline_gameplay_gpu.json`. Perfil depois do VU1 estático (`logs/prof_20261028_075732`): thread do jogo = IOP 30%, VU1 30%, GIF/VIF/DMA 12%,
código do jogo 12%. IOP: o tempo está no laço da thread de som do AGESND (`0x44040–0x44440`, carregado em base
`0x3cb00`; varre 48 vozes e chama modmidi/modsein/modhsyn), acordada por `iWakeupThread` num timer de hardware
(`timrman`) de 4167 µs = 240 Hz; o relógio do IOP emulado anda perto do real (~38,7 MHz), então é trabalho real
interpretado, não laço em falso. Descartado: `PS2X_IOP_BUSY_PERCENT=10` (139 → 141, +1%). Caminhos: rodar o IOP
numa thread própria do host (no PS2 ele é paralelo ao EE) ou recompilar estaticamente o código dos IRX; e
continuar enxugando o VU1 estático (normalização de operandos, flags).
Ferramenta nova: `tools/irx_dis.py <irx> <base> <ini> <fim>` desmonta um trecho do IRX e anota as importações
(`--imports` lista os stubs).

**Gargalo com o GS na GPU (2026-10-27, `tools/profile.ps1 -Gpu`, `logs/prof_20261027_204253`):** a thread
do jogo passa **72% no VU1** (microprogramas compilados, mas ainda com a maquinaria de pipeline ciclo a ciclo do
interpretador: `commitReadyPipelines`, `readyCycle`, flags FMAC, normalização de operandos), 10% esperando,
7% no código do jogo, 5% IOP. Quase tudo vem do DMA do scratchpad → MFIFO → VIF1 → MSCAL. O GS deixou de ser o
limite. Próximo passo: VU1 compilado com tempo resolvido na geração (latências/stalls calculados por
`tools/vu1_recomp.py`, sem filas de pipeline em execução), verificado com `PS2X_VU1_COMPILED_VERIFY`.
Alternativa menor: rodar o VU1 numa thread própria (no PS2 ele roda em paralelo ao EE), ganho máximo ~1,4x.

**Pendente: trapaças (pedido do usuário, 2026-10-29):** transformar **todas as trapaças originais do jogo** (invulnerável, Dead Eye infinito, arma de ouro, morte súbita, sem HUD, dinheiro etc.; nomes `cheat_*` no ELF, #74) e as trapaças de PC (vida/balas/dinheiro do menu F11, #73) em opções acessíveis no port, sem precisar digitar códigos.

**Som (2026-10-30, #81):** SPU2 emulado; sai som no menu (efeitos das vozes + PCM do mixer do AGESND pela AutoDMA). Vídeos com som desde o #82 (libsdr original → SDRDRV emulado → AutoDMA do núcleo 1). Falta: reverb, conferir música/streams, qualidade (interpolação linear).

**Som e vídeos (2026-10-30 13h, #82–#85):** os 22 .PSS do disco foram testados com `tools/fmv_audio_test.ps1` (`logs/fmv_audio/resumo.txt`): todos com som em toda a duração. O ATTRACT (modo demonstração) travava no início; corrigido no #84, que era o travamento do demo mode. Fases: o teste automático (`tools/level_audio_test.ps1`, troca `hng` pela fase via `RDR_LEVEL_REDIRECT`) ainda não é confiável. Com o demo mode funcionando, o `drive.ps1` às vezes não detecta o menu e não aperta as teclas. Trocar `front` por uma fase no boot trava em `FUN_00228740` (`0x228840`). Numa execução, a carga do `hng` acabou no laço do FATAL `FUN_0019A898` (tela de loading parada). O #85 registra a mensagem; ela ainda não foi capturada. Próximo: ajustar a detecção do menu no `drive.ps1` depois do demo, rodar `level_audio_test.ps1` em todas as fases e capturar o `[rdr] FATAL do jogo`.

**Versão de distribuição v0.1.0 (2026-10-31).** `tools\make_dist.ps1` gera `dist\RedDeadRevolver-v0.1.0\` (+ .zip, ~80 MB, sem arquivos do jogo). O `RedDeadRevolver.exe` abre um lançador (PLAY / SELECT ROM / QUIT) que instala a ISO do jogador em pastas por tipo (`movies audio data frontend levels extras system`, como o GTA Vice City) e roda o jogo pelo disco virtual `system\disc.vdisc`, em tela cheia na resolução do monitor com SSAA 16 e 16:9. Saves em `Documentos\Red Dead Revolver`. Também: Options centralizado, widescreen sempre ligado, menu do F10 removido, controle de PS2 dos tutoriais escondido, aviso de memory card da abertura pulado (FUNCOES #89–#91). Verificado: instalação a partir da ISO, boot pelo disco virtual, menu sem o aviso, tutorial sem o controle. Atualizado em 2026-11-01: dois exes (jogo direto e `RDR Launcher.exe`), desinstalador, atalhos no menu Iniciar, ícone, sem SSAA por padrão. Pendente: confirmar que o autosave grava na pasta de Documentos; desempenho da fase `sher70` (11–30 fps mesmo sem SSAA); travamento ao voltar ao menu e carregar outra fase (salto para 0x510000 / 0x8, `logs/run_20261101_100004_drive.log`); menu de configurações no estilo do GTA VC.

**Resolvido (2026-10-31): loading do `hng` (capítulo 2) termina e a fase é jogável.** Três causas encadeadas: (a) pools fixos do runtime (RPC/TLS/bootmode) em 0x1F00000–0x1F31000, dentro do heap do jogo, e pilhas/heap do runtime na RAM do jogo — zeravam objetos (queda em `0x100744`) → área privada 0x40000–0x100000 (#88); (b) o override do free (#80) vazava o bloco de 15 KB de cada subpool esvaziado → Heap overrun no heap da fase durante a descompressão zlib → corrigido; (c) o free inválido de `0x1007EC` (`FUN_00100788`, cabeçalho zerado) ainda acontece e é ignorado pelo #80. Verificação: `logs/run_20261031_123703_drive.log` (Red jogando no hng aos 290 s). Pendente: `guest-branch:missing-target` com pc=0x8 no fim desse teste (depois de voltar ao menu e recarregar o hng) e a origem do free inválido.

**Bloqueio anterior (2026-10-29 20h): loading da história trava (teste do usuário).** Após avançar normalmente na história, `NewLoadShow` (0x69E6C0) fica em +0x90 = 2 (carregando) para sempre; a thread carregadora (id 13, entry 0x2201E8) dorme no sema 7 e o laço principal segue. Antes disso o usuário viu um travamento no início da cutscene antes do tutorial, e duas rodadas automáticas fecharam com salto para 0x510000 (`guest-branch:missing-target`). Hipótese: notificação perdida entre EE e IOP com a thread de IOP (#70). RAM salva em `logs/ee_snap_loadhang_20261029.bin`; log `logs/play_20261029_201054.log`. `-SafeIop` trava no demo mode (thread do vídeo num laço WaitSema/SignalSema esperando o áudio, que sem a thread de IOP não avança): não usar. Achados: (1) demo mode, thread no flip esperando FINISH → contador de FINISH (#79, hipótese); (2) loading do capítulo 2 (HNG.DAT) → laço infinito do free com ponteiro inválido (#80, contorno que ignora a liberação e registra o chamador).

**Pendente: voltar com ○ nos menus (pedido do usuário, 2026-10-29):** o jogo usa △ para voltar (livro "△ Back", perfis "△ Back" / "○ Remove Player"). Plano: só no front-end, trocar ○↔△ na leitura do controle e trocar os glifos 0xBC↔0xBE da fonte, para o ícone acompanhar. Falta achar a flag de "front-end ativo" (máquina de estados do jogo, ver bloqueio de 2026-10-16).

**Pendente: ícones por dispositivo (pedido do usuário, 2026-10-29, #76):** teclado → ícones de teclas; controle de PS → ícones nativos; controle de Xbox → ícones de Xbox. Os ícones são glifos da fonte `berrylium` (0xBA ✕, 0xBC △, 0xBD □, 0xBE ○, 0x2020–0x2023 L1/R1/L2/R2). Caminho: interceptar a leitura de `berrylium_02.tex`/`_04.tex` (ou o upload da textura para o GS) e trocar os pixels desses glifos por versões desenhadas para teclado/Xbox na mesma paleta, conforme o último dispositivo usado. Falta mapear as fontes do HUD do jogo (SWAT.DAT; nomes `xButton`, `triangleButton`... no ELF).

**Pular fases (2026-10-29, #74/#75):** no build final o `unlockall` só libera diário e multi; o livro de capítulos só deixa escolher capítulo no modo Bounty Hunter. O override força esse modo (página virou no teste). Antes: **Pular fases (2026-10-29, #74):** `config/params.txt` passa parâmetros do motor ao jogo; `unlockall` (padrão) libera capítulos, trapaças, diário, multi e dificuldades. Efeito no menu ainda a confirmar.

**Menu do port (2026-10-27, #66):** F10 abre "Resume / Exit to Windows" por cima do jogo.
**Saída pelo botão de voltar (2026-10-30, #86): feito.** No escritório do xerife o rodapé mostra ✕ Enter, ○ Options e △ Quit (este à direita). △ (V / Num 1) abre "Quit game?" Yes/No (padrão No); Yes fecha o jogo. Teste: `tools/test_quit_menu.ps1`. Pendente: confirmar que △ dentro de Options/livro não abre a caixa (o log só mostrou os modos `none` e `book`); trocar o botão de voltar para ○ (`backButtonPressed()` no runtime + glifos). O menu F10 (#66) continua como saída de emergência. O `drive.ps1` agora detecta o menu pelo log `[rdr] prompts book|poster|journal`.

**GS na GPU (2026-10-22, #65):** paraLLEl-GS (Vulkan compute, `external/parallel-gs` @ `cc6184af7e0c`, LGPL-3.0)
integrado como backend opcional: `PS2X_GS_BACKEND=parallel` (build com `PS2X_PARALLEL_GS_DIR`, passado pelo
`3_build.ps1`). Boot → logos → FMV → menu renderizam na RTX 3060 sem erros Vulkan
(`logs/run_20261022_150452_drive.log`); boot/FMV a ~600 FINISH/600 ticks. Gameplay (tutorial, Red criança): **75 FINISH/600 ticks** (≈7,5 fps) contra 62–63 no GS em software (+20%), imagem correta (`logs/run_20261022_151520_drive.log`). Teto sem desenho era 171: o limite agora é outro (EE quase sem ociosidade, `ee.idle_ms`≈70; suspeitos: parse GIF na thread do jogo, readback síncrono segurando o mutex).
Pendências: medir gameplay e comparar com o GS em software; readback por quadro é síncrono (fence); scanout
fixa SMODE1 NTSC quando vazio; SSAA via `PS2X_GS_SSAA=2|4|8|16` não testado; `clearFramebufferContext` (stub
sceGs) não passa pela ponte.

**Desempenho (2026-10-17 17h):** gameplay FINISH/600 ticks 14 → 25 (GS em thread, #55) → 27 (faixas, #56) → 31 → 34 → 38 (VU1, #57) → 40–41 (VU1 rápido, #58) → 44 (matemática rápida do VU, #59) → 52 (**VU1 recompilado**, #61; ~3,7× sobre o início). Depois: HLE da mistura de áudio do AGESND (#62) e IRQ do GS por borda (#63) não mudaram o FPS. Perfil atual (gameplay, 59 amostras): thread do jogo ~73% ocupada (VU1 compilado ainda usa a maquinaria de pipelines do interpretador por par: flags, fila de VI, `advanceOneCycle`) e ~27% esperando o GS esvaziar no FINISH; faixas do GS ~42% ocupadas (WritePixel/SampleTexture, `threadTextureCache` por texel, `bitsPerPixel`). Atualização (#64): sem desenho o jogo faz 171 quadros/600 vblanks (~17 fps) ⇒ o teto atual é o rasterizador do GS em software; com ajustes por pixel e 5 faixas: **62–63** (~6 fps). Próximo salto: **GS na GPU**. Próximo salto real: recompilar os microprogramas do VU1.

**Microprogramas do VU1 (2026-10-17, `PS2X_VU1_DUMP_MPG`, #60):** do boot ao gameplay o jogo envia só **12 programas distintos** (~20 KB, ≈2.500 pares de instruções), sempre iguais e em endereços fixos (0x0000, 0x0800, 0x1000, 0x1F40, 0x2740, 0x2F40, 0x3740). Viabiliza recompilação estática: gerar C++ por programa (pares em linha reta, desvios como goto, VF/ACC imediatos como no modo rápido, flags com atraso de 4 pares via buffer circular, Q/P com cópia antiga quando lidos antes de WAITQ, XGKICK síncrono) e, no `MSCAL`, despachar para a função compilada quando o hash da memória de código bater; senão, interpretador. Gargalo atual: interpretador do VU1 na thread do jogo.

**Plano de longo prazo (definido com o usuário em 2026-10-17; não é o foco agora — primeiro fazer o jogo rodar):**
recompilar o máximo possível para a arquitetura do PC em vez de emular. Situação: o EE (CPU principal) e o
VU0 em modo macro já são recompilados para x64; VU1 é interpretado ciclo a ciclo; GS é rasterizado em
software na CPU; IOP é interpretado (IRX originais + HLE); SPU2 emulado (sem saída de som ainda).
Caminhos, em ordem de impacto esperado: (1) **GS na GPU** (OpenGL/Vulkan) em vez do rasterizador em
CPU — maior ganho e permite resolução alta; (2) **recompilar os microprogramas do VU1** para código
nativo (como o ICO recomp) em vez de interpretar; (3) **fastmem**: reservar 4 GB de endereços e mapear a
RAM do PS2 numa base fixa com os espelhos (kseg0/kseg1/uncached) por memória virtual, e tratar registradores
de hardware por proteção de página — tira máscara/teste de cada acesso e deixa a base fixa (Cheat Engine);
(4) HLE de mais módulos do IOP. Limite: ponteiros nativos de 64 bits exigiriam reescrever o jogo
(decompilação manual); as estruturas do jogo guardam ponteiros de 32 bits.

**Ideias pendentes (pedido do usuário, depois do desempenho):** (1) alocar a RAM do PS2 num endereço fixo do processo para tabelas do Cheat Engine valerem entre execuções (hoje endereço = base do bloco + endereço PS2); (2) suporte a `.pnach` (códigos CodeBreaker/GameShark como no PCSX2) no runtime.

**Desempenho (2026-10-17 14h):** GS em thread própria (#55): +79% de quadros no gameplay (FINISH/600 ticks
14 → 25, `logs/run_20261017_133430_drive.log`). Próximos candidatos: VU1 (interpretador, ~17% do tempo do jogo),
rasterização paralela por faixas dentro do GS, e medir de novo onde a thread do jogo gasta.

**Teste do usuário (2026-10-17 13h):** andou na fase 1 sem cair, direcionais/WASD funcionam, tutorial
avança. O jogo fechou porque Esc fechava a janela (#54, corrigido). Gargalo agora: desempenho no gameplay.

**Plano pendente — mira direta com mouse (estilo GTA VC).** Hoje o mouse vira analógico direito (#53): o jogo
transforma inclinação em velocidade de giro (com aceleração), então a mira "desliza". Objetivo: movimento do
mouse = ângulo (sem aceleração). Passos: (1) achar na RAM os ângulos de câmera/mira do Red (yaw/pitch) com
`PS2X_WRITE_WATCH` e dumps F9, girando a câmera com o analógico; pistas no motor: `AimHorizSensitivity`,
`AimVertSensitivity`, `AimRampX/Y`, `CameraAimOffset`/`CameraAimScale`; (2) em `src/rdr_overrides.cpp`,
somar Δmouse × sensibilidade direto nesses ângulos a cada quadro e manter o analógico direito centrado;
(3) respeitar limites do jogo (pitch máximo, mira travada/L1). Antes disso: confirmar no tutorial quais
botões mirar/atirar/zoom (padrão provisório: esquerdo=R1, direito=L1, rodinha=R3).

**Progresso (2026-10-17 10h): GAMEPLAY — Red anda na fase 1.** A queda pelo terreno era do recompilador
(#52): `LQ/SQ/LQC2/SQC2` não alinhavam o endereço (`& ~0xF`), e o teste de triângulo da malha do cenário
(`FUN_001A2BA8`, vértices de 12 bytes lidos com LQ+MTSAB+QFSRV) recebia vértices deslocados. Com a correção
o Red fica no chão (Y ≈ -0,75 em `0x199CC58`), pula e o tutorial aparece ("Move: left analog stick", HUD com
munição e vida) — `logs/run_20261017_095303_drive.log`. Marco 11 (primeira fase em gameplay) atingido.
**Próximo:** jogar com controle de verdade (teclado/gamepad) para ver movimento, tiro e inimigos; depois
desempenho (3D ~7 qps) e áudio.

**Progresso (2026-10-16 22h): fase 1 carrega e é jogada; Red caía pelo chão.** O "Begin" agora carrega a
fase de verdade (#48: `PollSema` devolvia -419 em vez de -1 e o carregador cancelava; #49: 9 funções de
vtable fora do mapa). A cutscene de introdução roda com cenário, personagens e legendas (cabana, alvo,
cerca, cena com Nate e Falling Star). Ao receber o controle, o Red caía pelo terreno até morrer
("tentar de novo"). Hipótese do dt (relógio real, 0,1 s a ~7 qps) testada com passo fixo 1/30 (#51):
**não resolveu** — passos menores, mas a sonda de chão não corrige nada (`logs/run_20261016_221131_drive.log`).
**Próximo:** descer na sonda `FUN_002C7610` →
mundo `DAT_004F80AC` (vtable `0x5071C8`, métodos +0xEC `0x2BF990` e +0xF0 `0x2BFBF8`) → bounds (+0x60/+0x64).
Ferramentas novas: `PS2X_WRITE_WATCH` (#50), `RDR_TRACE_CALLS` (#48), `Callers.java`/`Callees.java`.
Desempenho continua o gargalo (3D ~7 qps).

**Progresso (2026-10-16 14h): sala do menu desenhada inteira.** A causa era o recompilador: `SQRT.S`
lia o registrador do campo fs (sempre f0) em vez de ft (#47). Os planos laterais da câmera, normalizados
com `SQRT.S` em `FUN_003C4338`, saíam 1000× maiores e o teste de frustum `FUN_00153608` descartava
paredes, chão e fogão. Com #46 (VCLIP) e #47 o escritório do xerife aparece como no PS2 (log
`logs/run_20261016_142457_drive.log`). A fase do capítulo 1 **continua preta** depois do Begin
(`logs/run_20261016_142838_drive.log`): 30 fps, só o microprograma 0x1F40 do VU1 e ~33 primitivos por
quadro; o bloqueio abaixo segue valendo. Também corrigido: `scripts/env.ps1` põe o `vswhere.exe` no
PATH (o vcvars64 falhava fora de um shell do VS).

**Bloqueio atual (2026-10-16 10h): fase do capítulo 1 carrega mas não aparece.** Com #41 (GetDir do
cartão) o boot vai direto ao aviso de Autosave e o "Begin" do capítulo funciona. Máquina de estados do
motor (nó atual em `DAT_0079F158`; transições por nó, condição no método +0x14 de cada transição):
`FrontEnd` → (`FrontEndToLoad`, cond. `0x234500`: `DAT_0073AF14 == 0` e `FUN_0021DF60(obj, "lvl_perfnew")`)
→ `NewLoadShow` (objeto `0x69E6C0`, estado em +0x90: 1 preparado, 2 carregando, 4 carregado/aguarda X,
3 sair) → `LoadToGame` (`0x225B98`: +0x90 == 3 e +0xA0 == 0) → `Game`. A fase é lida (30 leituras de
`LVLS_SNG/PERFNEW.DAT` + SWAT.DAT), mas em `Game` não há viewport ativa (`DAT_0073C18C = 0`), nada é
desenhado e não há mais leituras. Som/IOP estáveis (bancos e streams sem pendência). Próximo: descobrir
o que ativa a câmera/viewport no início da fase (update de Game `FUN_00236568` → `FUN_00182718`; talvez
a cena de introdução `LoadToIntro` exigisse +0xA0 != 0).
Ferramentas: `PS2X_WATCH_U32`, `PS2X_CD_TRACE`, F9, `tools/ghidra/XRefs.java`.

**Bloqueio atual (2026-10-16 08h): início do capítulo 1.** Menu → livro "Player in Progress" → X →
perfil "RED" (Gunslinger Profiles, X confirma) → livro "Chapter 1 – Bull's Eye" → X (Begin) → tela
preta para sempre. O jogo lê o cabeçalho de `LVLS_SNG/PERFNEW.DAT` (setores 0 e 2796) e para de ler o
disco; a thread principal continua no laço do front-end (`FUN_00101BA8`, retorno em 0x102A24), sem
desenhar. Corrigido no caminho: música (`STREAMS.DAT;1;1`, #38), busca de CD do IOP (#37), alocador
do IOP (#39), prioridade do mcserv HLE (#40). Pendente: descobrir o que o front-end espera
(variáveis do script de UI `playerChoice`/`loadIt`/`saving1`; gerenciador do cartão `FUN_003F0348`).
Teclas do roteiro: `drive.ps1 -MenuKeys "6:20:400,20:20:400,85:20:400,..."` (X a cada ~25 s).
A sala do menu (abaixo) continua pendente.

**Bloqueio atual (23h): sala do menu não é desenhada.** O menu real é o escritório do xerife
(paredes de tijolo, fogão, escrivaninha; imagem de referência do usuário). Aqui só livros, pôsteres e o
escorpião chegam ao GS; por baixo aparece a grade de depuração do motor (textura "grid" 8x8 de cores
aleatórias, plano de 100x100). Descartados: GS (endereçamento PSMCT32/16/T8/T4 conferido com o PCSX2,
truque de 16/32 bits do sépia idêntico ao esperado), recorte do VU1 (`PS2X_VU1_CLIP_ZERO` não muda a
lista), microprogramas (0x10 e 0x1F40 são só configuração; todo desenho passa pelo de 0x0 e todo
XGKICK termina), leitura de disco (FRONT.DAT lido inteiro, sem erro). Corrigida a semântica da FPU do
EE/VU0 (FUNCOES.md #35) — necessária, mas não resolveu. Hipóteses abertas: a sala não chega ao VU1
(descarte no EE: visibilidade/setores; ou não instanciada), ou o EE monta a lista dela num caminho que
falha em silêncio. Ferramentas novas: F9 (FUNCOES.md #36), `PS2X_CD_TRACE`.

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

**Investigação (2026-11-01 noite): travamento com salto para 0x510000 ao carregar fase.** É intermitente: em 7 rodadas de `tools/walk_test.ps1`, aconteceu 3 vezes, no front ou no carregamento do perfnew. Também já aparecia em logs de 29/10.

O que se sabe:
- A thread 1 retorna de `FUN_00236568` (atualização por quadro; quadro de 176 bytes, ra em sp+0x98 = `0x1F7FDA8`, perto do topo da pilha principal em `0x1F80000`) com o ra e os registradores salvos (s0, s1 = 0) corrompidos.
- `0x510000` aparece de forma legítima nesse slot quando o quadro não está ativo.
- **Descartado:** as pilhas de interrupção, que ficam na área privada `0x80000–0x100000`.
- **Hipótese:** escrita fora do lugar perto do anel do MFIFO, que começa em `0x1F80000`, logo acima da pilha.

Diagnóstico deixado no runtime (`EeScheduler.cpp`):
- no PC inválido, o log mostra os últimos 24 despachos (pc/ra/sp) e a pilha em volta do sp;
- `PS2X_WATCH=<end>:<valor>` registra as transições de uma palavra.

Falta reproduzir de novo com o dump da pilha.

**Travamento 0x510000: mecanismo (2026-11-01, `logs/run_20261101_215104_drive.log`).**

O dump da pilha mostra:
- o quadro do `FUN_00236568` com ra = `0x510000` em sp+0x98 (`0x1F7FDA8`);
- o ra verdadeiro (`0x1018C8`, retorno da chamada em `0x1018C0` dentro de `FUN_001018A0`) 16 bytes acima, em `0x1F7FDB8`.

Leitura: a thread 1 ficou bloqueada dentro de `FUN_00151960` (contexto salvo: pc `0x1516A0`, sp `0x1F7FD00`) e **continuou por outro caminho**. O loop chamou o `FUN_00236568` de novo, com sp 16 bytes acima, e o quadro novo cobriu o antigo. Depois o escalonador retomou o contexto velho, já obsoleto, e o retorno caiu em `0x510000`.

O problema está no escalonador do runtime (`EeScheduler.cpp`): continuação duplicada da thread principal. Não é escrita errada na memória.

Próximo passo: achar onde um contexto da thread 1 fica guardado e é retomado depois. Candidatos:
- `GuestInvocation` (interrupções e callbacks, `invokeCurrent`/`m_pendingInvocations`);
- bloqueio em syscall (`blockCurrent`);
- `transferIfRequested`.

Para isso, registrar a cada retomada da thread 1 o pc/sp retomado e o pc/sp da última vez que ela rodou.

**2026-11-02: SSAA 16 e o travamento.**

FPS com SSAA 16, em 4K numa RTX 3060:
- tutorial a ~11 fps, com a GPU em 30–45% de uso e as threads da CPU esperando (`tools/profile.ps1`). Ou seja, serialização CPU↔GPU, não falta de GPU;
- menu a ~28 fps (limitado pelo jogo a 30) com a GPU em ~70%.

Correção feita: o `present` da ponte do paraLLEl-GS (`gs_parallel_bridge.cpp`) copiava a imagem e esperava a GPU (`fence->wait()`) a cada vsync. Agora a leitura é em buffer duplo: copia o quadro N, lê o N−1, com buffers fixos. `PS2X_GS_READBACK_SYNC=1` volta ao modo antigo. **Ganho ainda não medido no gameplay.**

Travamento (PC inválido `0x510000` / `0x1A761B0` / `0x1A22294` na thread 1): em 6 de 8 rodadas, sempre logo depois da invocação da interrupção `0x3B94A8` sobre a thread 1 (`t-101` no anel). Ao fim dela, a thread 1 retoma com pc/ra errados.

Descartado:
- pilhas sobrepostas: `[CreateThread]` mostra cada thread com pilha própria, por exemplo a thread 2 em `0x795390`/0x2000;
- reentrada do laço principal: a vigia `RDR_GUARD_236568` não registrou nenhuma.

Próximo passo: na entrada e na saída de cada invocação de interrupção sobre uma thread, registrar o contexto da thread (pc, ra, sp e `in_delay_slot`). Também ver onde o código gerado lança a transferência (`invokeCurrent`/`transferIfRequested` dentro de stubs e syscalls) sem `ctx->pc`/ra coerentes.

Ferramentas de diagnóstico:
- anel de despachos com a thread de cada um;
- dump da pilha no PC inválido;
- `PS2X_WATCH`;
- `RDR_GUARD_236568=1`;
- log `[CreateThread]`.

Medir o gameplay com save vazio: `-Env "RDR_SAVE_DIR=<pasta vazia>"`. Com o save de teste, o jogo para no menu antes da sher70, que não gera FINISH.

**2026-11-02 (tarde): travamento, novas pistas.**

`FUN_00236568`:
- o laço principal **sempre** o chama com sp `0x1F7FDD0`, então o quadro real fica em `0x1F7FD20`, com o ra em `0x1F7FDB8`;
- no travamento de 0x510000, a continuação rodou com **sp 16 bytes abaixo** (`0x1F7FD10`) e leu o ra no slot errado;
- conclusão: alguma função chamada voltou ao escalonador com sp−16. Não é escrita errada na memória.

Conferido no código gerado: o `jr ra` com `addiu sp` no delay slot e os checkpoints de laço (`eeCheckpointDue`) executam o delay slot antes de sair.

Outros travamentos:
- PC inválido `0x1F7FA50` e `0x1A22294`, com ra lido de um slot com dado de pilha ou de heap;
- vêm logo depois da interrupção do FINISH do GS (`FUN_003B94A8`: `sceGsPutIMR`, `iSignalSema(DAT_007620C8)`, `EI`), que roda sobre a thread 1.

Diagnóstico deixado no código:
- `RDR_GUARD_236568=1`, que também arma `ps2SetSpExpectation`: o escalonador registra `[spcheck]` quando retoma dentro do `FUN_00236568` com sp diferente do esperado e mostra o despacho que devolveu o sp errado;
- o log de entrada com sp diferente.

Próximo passo:
1. Rodar até um travamento em `FUN_00236568` com `[spcheck]`.
2. Generalizar: guardar o sp de cada despacho de retorno e comparar com o sp do quadro que chamou.

O override de leitura do Dave agora só age se a original terminou (`ctx->pc == ra`) e limita a cópia ao tamanho pedido (`a2`). Isso não era a causa do travamento.

**2026-11-02 (noite): travamento sp−16 corrigido.**

Causa: com o `[spcall]` (novo log no `dispatchGuestBranch`), a chamada `0x236700` → `FUN_00182718` voltou com sp−16 e ra `0x182758`. O `FUN_00182718` percorre uma árvore, e os métodos virtuais (`jalr` em `0x182750`) chamam o `FUN_00182718` de novo. Quando o checkpoint caía nessa chamada recursiva, `ctx->pc` ficava igual à entrada, e o `dispatchGuestBranch` de fora entendia "pc == entrada" como stub em C++ terminado. O `FUN_00236568` seguia então com o sp do quadro interno. O escalonador estava certo; a "continuação duplicada" das notas anteriores era isso.

Correção (runtime, FUNCOES.md #96): o contador `m_guestBailouts` conta as saídas antecipadas do código gerado. "pc == entrada" só vale como término se não houve nenhuma saída durante a chamada; senão a chamada fica incompleta e o escalonador a retoma.

Resultado: 3 rodadas de 10 min de gameplay sem travar (antes eram 6 de 8), sem `[spcheck]` e sem `PC invalido`. Logs: `run_20261102_200058/201100/202102_drive.log`, FPS em regime ~260 por janela.

Em aberto (**hipótese**, não trava): o `[spcall]` ainda acusa retorno normal (`entrada=0 saidas=0`) com sp menor em:
- `0x100f74` → `0x100b60` (−128/−256), que tem `jr $a0` em tabela e uma segunda cópia `entry_100b40`. Pode ser uma troca de contexto/corrotina do motor;
- `0x1419fc` → `0x1418b8` (−224, uma vez −1568 = 7 quadros);
- `0x3f7940` → `0x3f7800` (−16, raro).

Próximo passo: olhar `FUN_00100b60` no Ghidra (troca de pilha intencional?). Os diagnósticos `RDR_GUARD_236568`/`ps2SetSpExpectation` podem sair quando isso fechar.

**2026-11-04: FPS com SSAA 16 (4K, RTX 3060).**

- Gameplay (tutorial) a 250 FINISH/600 (~25 fps). Antes da leitura em buffer duplo da GPU era ~11 fps.
- Perfil (`logs/prof_20261104_063439`): a thread do jogo gasta 52% no VU1, 63% inclusivo no `drainMfifo` → VIF1 → microprogramas. A GPU não é o limite.
- Flags mortas do VU1 (#71) religadas, agora padrão em `tools/vu1_recomp.py` (`--no-dead-flags` desliga): 995 de 3.464 pares deixam de gravar MAC/status. O travamento que fez desligá-las era o bug do sp−16 (#96). Resultado: **~278 FINISH/600 (~27,8 fps)**, 3 de 3 rodadas sem travar.
- Novo `tools/level_fps_test.ps1`: fps por fase via `RDR_LEVEL_REDIRECT=hng=<fase>`. A varredura das 35 fases está em `logs/level_fps_20261104.txt`.

Próximo passo: atacar as fases mais lentas. Candidatos para o VU1: tempo resolvido na geração (sem o anel de flags em execução) ou VU1/VIF1 numa thread própria.

**2026-11-05: fases lentas e correções de medição.**

- A varredura de 2026-11-04 com `hng=<fase>` não valeu: com save vazio o Novo jogo carrega `perfnew`. `tools/level_fps_test.ps1` agora usa `-From perfnew`.
- Rodadas de 300 s pegam pouco gameplay: use 600 s nas fases. O log deve mostrar `[rdr] fase perfnew -> <fase>`; sem isso, a rodada ficou no menu.
- `measure.ps1` vazava as variáveis extras entre medições da mesma sessão (corrigido). A "queda do tutorial" para 122/145 era a fase `ranch` por esse vazamento.
- `tools/cost_report.py <logs>`: ms do EE por quadro e por mil MSCAL, para comparar versões em cenas diferentes.
- VU1: `out/vu1mpg` foi de 12 para 15 microprogramas (capturados em gameplay de `msn`/`ranch`; antes caíam no interpretador). `merged_chains` do `vu1_recomp.py` gera uma cadeia por combinação de variantes (até 32): 41 programas estáticos. Custo por mil MSCAL: `msn` 84 → 52 ms, `ranch` 57 → 44 ms. Tutorial 278 → 292 FINISH/600.
- Agrupar o `accountCycles` a cada 1024 ciclos foi testado e removido: 2 rodadas com, 2 sem, sem ganho de fps (276 contra 292).

FPS com SSAA 16 em 4K, 600 s (`logs/level_fps_ssaa16_20261105.txt`): `royale` 27,1; `core` 25,6; `msn` 19,2; `woods` 16,2; `ranch` 15,1. EE ocupado 50–65 ms por quadro nas piores; 45–60% é `drainMfifo` → VIF1 → VU1 na thread do jogo.

Logo da tela de título: as três texturas 256×128 (PSMT8) são desenhadas pelo Flash `logo01` acima do topo da tela (y do GS 1455–1750, centro ≈ 2048 − 448), paradas. Falta nos dois backends do GS, com ou sem widescreen e com ou sem o override #91. Causa não encontrada; próximo passo é ler o `logo01` extraído do FRONT.DAT.

**2026-11-05 (tarde): VIF1 em thread própria, menu e logo.**

- VIF1 em thread própria (FUNCOES.md #99, ligado por padrão desde esta sessão; `PS2X_VIF1_ASYNC=0` desliga): em SSAA 16 / 4K, `msn` 19,2 → 24,3 fps (EE 49 → 29 ms/quadro) e `ranch` 15,1 → 27,8 fps (esta rodada voltou ao menu no meio; repetir). As esperas por `sync` zeraram.
- Com a opção ligada, `msn` e `ranch` voltaram ao menu no meio de algumas rodadas (`msn > front > msn`). **Hipótese:** o personagem parado morre antes porque o jogo anda mais rápido. Não confirmado.
- Menu PC Settings: opções de Picture/Sound incluídas e "Game Options" removido (#98).
- Logo do título aparece (#97), com correção de posição; causa real desconhecida.
- Com o build padrão, SSAA 16 / 4K: tutorial 30,1 fps em 2 rodadas (limite do jogo, EE 15,5 ms/quadro); `woods` 23,3. Falta medir `royale`, `core` e `ranch` de novo. Com o EE ocioso ~4,8 s por janela na `woods`, o gargalo agora é a thread do VU1.
- Janela (#100): tela cheia sem "sempre no topo", Window Mode e Resolution valem na hora, Widescreen do jogo acompanha o Aspect Ratio.

Próximos passos: confirmar se a volta ao menu no meio de `msn`/`ranch` é o personagem morrendo; achar por que o logo do título fica acima da tela nos dados do Flash; reduzir o custo do VU1 (ver FUNCOES.md #71, #99).

Medição final (build padrão, SSAA 16 / 4K, `logs/run_20261105_12*`): tutorial 30,1 fps. A `msn` voltou ao menu de novo no meio da rodada (`msn > front`) e a `ranch` não chegou à fase, então estas duas seguem sem número final com o build padrão. `[rdr] widescreen do jogo -> 1` e `[rdr] logo do titulo` confirmados no log. Distribuição `dist/RedDeadRevolver-v0.1.0` gerada com este build.

**Pendente: ícones de controle na loja (pedido do usuário, 2026-11-05):** a tela SHOP mostra ✕ Done e □ Buy This Item como texturas de 32×32 PSMT8 (GS: 9677 com CLUT 9673 e 9669 com CLUT 9665; ✕ em x 1855–1880, y 2203–2233). Os pixels não foram achados por forma em nenhum `.pck` extraído; 9669 aparece também no título, então a origem é comum. Plano: registrar um hash dos uploads de 32×32 PSMT8 e das CLUTs no frontend do GS para identificar os dois ícones, e trocar os bytes no upload por uma tecla desenhada, reaproveitando `drawKeyCap`. Falta também conferir o menu antes da fase (`popups_01_ingame.pck`). `tools/pck_bitmaps.py` e `tools/avm1_dump.py` ajudam a analisar os `.pck`. Para chegar à loja: a cópia do save do usuário abre nela sozinha depois de ~2 min.

**Vídeos (2026-11-05, #103, #104):** `movies/*.mpg` são os PSS renomeados, e um vídeo comum com o mesmo nome toca no lugar do original. Para editar: `tools/fmv_audio_test.ps1 -Only <NOME>` grava o som e `tools/export_movies.py game/disc/PSS out/movies_mp4 --only <NOME>` gera o `.mp4` (RSLOGO e TITLES já gerados). Falta decodificar o ADPCM do PSS para exportar sem gravar em tempo real.

**Conquistas (2026-11-06, #105):** tela ACHIEVEMENTS no lançador, com registro em `Documentos\Red Dead Revolverchievements.ini`. Conferido com o save de teste (`lvl_hng`): 2 de 20. Falta confirmar em jogo o registro pela RAM e se cada conquista bate com a fase certa nos capítulos seguintes. A leitura "`+0x29C` = última fase concluída" vem de comparar dois saves.

Cheat Engine: RAM do EE em `0x200000000` e scratchpad em `0x210000000` (`docs/MODS.md`). Leitura e escrita externas testadas por `ReadProcessMemory`/`WriteProcessMemory`.

## 2026-11-06: texto em alta resolucao

- Fontes do jogo ampliadas 4x em execucao e trocadas na GPU (FUNCOES.md #106); teclas no estilo do RDR2 (#107).
- Medicao gameplay em GPU sem SSAA: 56,7 gs.finish, 0 erros. Ainda nao medido em SSAA 16.
- Nao melhora: texto em Flash (loja, Press START, a confirmar) e logos em video (usar `movies/`).
- Teclas com fundo preto e simbolo branco (pedido do usuario).
- Traducao: extrator e motor prontos (FUNCOES.md #108, TRADUCAO.md). So ingles e portugues por enquanto. Teste em game/text/pt-br.txt (fora do git): Confirm, Change Name e Back trocados na tela de perfil, com acentos. Falta: texto longo estoura a caixa; nao testado em fase; rotulos de .swui e Flash nao passam por aqui.
- Proximo: Flash pela loja. Distribuicao ainda sem estas mudancas.
