# Funções identificadas e intervenções

Endereços referem-se a `game/SLUS_205.00.unpacked.elf` (CRC32 `EF570C27`, ver `docs/ESTADO.md`).

## Pontos conhecidos

| Endereço | Nome / hipótese | Papel | Como foi verificado |
|---|---|---|---|
| 0x00800008 | stub de boot (ELF do disco) | chama o descompactador e salta para 0x00100008 | desmontagem do ELF original |
| 0x00800028 | descompactador aPLib+XOR | 0x00800600 → 0x00100000 | reimplementado em `tools/rdr_unpack.py`; saída consome todo o blob |
| 0x00100008 | crt0 (`_start`) | zera registradores/FPU, limpa BSS 0x0054A000–0x007A26CC | desmontagem |
| 0x004FEA68 | fim do código | último `jr $ra`; depois padding e dados | desmontagem |

## Intervenções (stubs / skip / patches)

| # | Tipo | Selector | Motivo | Temporária? | Verificação |
|---|---|---|---|---|---|
| 1 | skip | `entry_004fb340@0x004FB340` | 0x004FB340–0x004FD800 é dado no meio do `.text`: tabelas da libipu (zigue-zague, matrizes de quantização MPEG, string `PsIIlibipu  2800`). Ghidra exportou o rótulo como executável; o recompilador gerava ~100 instruções inválidas. O código recomeça em 0x004FD800 (`addiu $sp,$sp,-0x20`). | Não (é dado, nunca executado) | dump de bytes em 2026-10-14; se o log de execução mostrar chamada a 0x004FB340, rever |
| 2 | stub (lote 1) | 26 seletores em `config/project.toml` (tabela abaixo) | Boot travava em `sceSifInitRpc` (laço `while(!sceSifGetSreg(0))` em 0x3CD508): as libs do SDK linkadas no jogo esperavam um IOP real. Ligadas aos handlers HLE do `ps2xRuntime`. | Não (é o modelo do PS2Recomp), mas cada uma pode precisar de ajuste | pilha via cdb (2026-10-14), `DumpRegion.java`/`DecompileFuncs.java` no Ghidra; rodar e checar o log |
| 3 | stub (lote 2) | 6 seletores da libcdvd (tabela "Lote 2") | Bind dos comandos N (SID 0x80000595, cdvdfsv) ficava em laço em 0x3C7338: o IOP emulado não tem o cdvdfsv (vem da ROM/IOPRP). | Não | decompilação de `FUN_0025cac0`/`FUN_00407318` (camada de CD do jogo); rodar e checar o log |
| 4 | override | `src/rdr_overrides.cpp` (`applyRdrOverrides`, ELF `SLUS_205.00` CRC32 `EF570C27`) | O jogo lê o disco por LSN (sem `sceCdSearchFile`); o HLE de CD precisa de `IoPaths::cdImage`, que o runtime não expõe na linha de comando. O override aponta a ISO (`RDR_CD_IMAGE` ou o 1º `.iso` em `ROM/`). | Não | log `[rdr] CD image: ...` |
| 5 | patch runtime | `patches/ps2recomp-runtime.patch` — `Kernel/Stubs/CD.cpp` `sceCdReadChain` | O HLE andava a lista de 16 em 16 bytes; `sceCdRChain` = {lbn, sectors, buf} = 12 bytes (confirmado pelo código do jogo, que monta triplas em 0x7483C0). | Não (bug do runtime; candidato a PR upstream) | leitura do código do jogo e do SDK |
| 6 | patch runtime | `patches/ps2recomp-runtime.patch` — `ps2_memory.cpp/.h`: MFIFO do DMAC | O jogo usa MFIFO (`FUN_003b4e98`: `D_RBOR=0x1F80000`, `D_RBSR=0x7FFF0`, `D_CTRL=9` → MFD=VIF1, `VIF1_CHCR=0x145`) e espera `SPR_FROM.MADR == VIF1.TADR` (FIFO vazio) em 0x46FB68 e 0x3B5008. O runtime processava a cadeia do VIF1 de uma vez e não colocava o SPR_FROM no anel. Implementado como no PCSX2: destino do SPR_FROM forçado ao anel; canal que drena em modo cadeia só consome tags até alcançar o MADR do SPR_FROM (espera o payload inteiro de cnt/next/end), parando em tag de fim. Endereços: D_RBSR = 0x1000E040, D_RBOR = 0x1000E050. Interrupção MFIFO vazio (causa 14) não emulada: o handler do jogo (0x3B4E58) só mede tempo. | Não (falta no runtime; candidato a PR upstream) | log de execução; laço em 0x46FB68 deve sair |
| 7 | stub (lote 3) | 9 seletores da libmpeg (tabela "Lote 3") | O player de FMV chamava a libmpeg do jogo, que decodifica via IPU (start codes em 0x497F48, `FUN_00498030`); o runtime não emula a IPU (0x10002000–0x10002030 só guardam valores) e o boot ficava procurando `0x000001`. Ligada ao HLE de MPEG (FFmpeg). | Não | log de execução (`[MPEG:...]`), vídeo na janela |
| 8 | stub (lote 4) | `sceSdRemoteInit@0x0049AFE8`, `sceSdRemote@0x0049B128` | O FMV parava após ~10 quadros: o player (`FUN_00470540`) sincroniza pelo `sceSdBlockTransStatus` (`sceSdRemote(1,0x8100,1,0)`); pela RPC 0x80000701 o comando vai para `ps2_audio` (`onSoundCommand`), que não avança a posição, então o buffer de áudio enchia e a thread leitora parava. O HLE `ps2_stubs::sceSdRemote` simula BlockTrans/Status (avança 1 KB por consulta). Só o player de FMV chama sceSdRemote (`FUN_00470178`, `FUN_00470540`, `FUN_004707f8`). | Sim, parcialmente: o áudio do FMV fica mudo (não chega ao backend de som) | log `[MPEG:GetPicture:FRAME]` deve passar de 10 quadros |
| 9 | stub ret1 | `ret1@0x00471158` (callback de stream M2V do player de FMV) | O callback copia o vídeo demultiplexado para o anel do jogo (`+0x90`) em laço até haver espaço; esse anel só é esvaziado pela libmpeg original (callback Nodata `0x470F70`), que o HLE substitui. Com o HLE decodificando do PSS, o anel enchia após ~80 KB e a thread leitora (`FUN_004713f0`) ficava presa dentro do `sceMpegDemuxPss`. | Não (adaptação permanente ao HLE de MPEG; reverter se a libmpeg voltar a ser recompilada) | log: `DemuxPss` passa de 5 chamadas |
| 10 | patch runtime | `Kernel/Stubs/Audio.cpp` `sceSdRemote` BlockTransStatus | O HLE avançava a posição do SPU2 1 KB por consulta; o player consulta 1x/quadro e usa a posição como relógio A/V (vídeo a ~1/3 da velocidade). Agora a posição segue o tempo real a 192.000 bytes/s (48 kHz, 16 bits, estéreo). | Não (candidato a PR upstream) | — |
| 11 | override + patch runtime | `ps2_stubs::setMpegGetPictureNonBlocking(true)` (`src/rdr_overrides.cpp`; opção nova em `runtime/ps2_mpeg_options.h` / `MPEG.cpp`) | O laço de quadro do player (`FUN_00236058`) chama `sceMpegGetPicture` antes de alimentar o SPU2 (`FUN_00470540`); o GetPicture do HLE suspendia a thread sem quadro pronto, e a thread leitora ficava presa no callback de áudio esperando espaço (deadlock). Com a opção, GetPicture devolve -1 ("sem imagem neste quadro"), como a libmpeg original via callback Nodata. Padrão do runtime inalterado. | Não | log: o player volta a chamar o consumidor de áudio |
| 12 | override + patch runtime | `ps2_stubs::setMpegDemuxBlockOnBackpressure(true)` (mesma opção/arquivos) | Com 10 quadros na fila, o HLE devolvia 0 bytes consumidos no `sceMpegDemuxPss`; a thread leitora do jogo ignora o retorno, então o resto do vídeo era descartado e o player terminava após 10 quadros. Com a opção, o DemuxPss suspende a thread (`waitExternal`, tipo 2) até o GetPicture retirar um quadro. Padrão do runtime inalterado (Code Veronica depende do comportamento antigo). | Não | vídeo da Rockstar toca inteiro (`logs/run_20261015_051515_shots.log`) |
| 13 | patch runtime | `Kernel/EeScheduler.cpp` `dispatchGsIrqIfPending()` | O runtime nunca gerava a interrupção INTC 0 (GS): o FINISH só ligava o bit em `GS_CSR`. O jogo espera o fim de cada quadro pelo handler do GS (`FUN_003b94a8`: `if (CSR & FINISH) iSignalSema(DAT_007620c8)`), registrado em `FUN_003b9ea0`, em `FUN_003ba798` (flip). Agora, a cada início/fim de VBlank, se CSR tem SIGNAL/FINISH e o IMR não mascara (0x100/0x200), dispara a causa 0. | Não (candidato a PR upstream) | laço principal volta a rodar após o vídeo |
| 14 | override + patch runtime | `ps2_stubs::setMpegProgramEndIsEof(true)` (`runtime/ps2_mpeg_options.h`, `MPEG.cpp`) | O `sceMpegIsEnd` do HLE só reconhece o fim pelo EOF do `sceCdSt`; o RDR lê o .PSS com `sceCdRead` e o player (`FUN_00101ac8` → `FUN_00236058`) ficava para sempre no último quadro preto. Com a opção, o código de fim de programa demultiplexado (0x000001B9, que já liga `streamEnded`) conta como EOF. | Não | o jogo sai do player após o vídeo da Rockstar |
| 15 | entry points | `config/code_gaps.toml` (649 inícios: lacunas + 32 alvos de ponteiro do `.data`, gerado por `tools/find_code_gaps.py`, mesclado por `make_recomp_config.py`) | Funções alcançadas só por ponteiro ficaram fora do mapa do Ghidra; a 1ª a aparecer: `[guest-branch:missing-target] JALR source=0x19a58c target=0x2cae18` matou a thread de boot logo após o vídeo. A ferramenta acha lacunas do `.text` com `jr $ra` e marca o início de cada função (1º word não nulo e após cada `jr $ra`+delay slot). 0x4FE6D8–0x4FE798 é dado: fica só como limite (o ps2_recomp reporta 3 instruções inválidas ali, nunca executadas). 2ª iteração: `missing-target 0x19A6B0` (função mínima `jr $ra` colada num fragmento órfão) → regra "`jr $ra` após nop"; e varredura do `.data` por ponteiros para `.text` sem início conhecido (exclui tabelas de salto; só alvos após `jr $ra`+delay ou prólogos `addiu sp,-N` após nop). | Não | log sem `missing-target` |
| 16 | patch runtime | `ps2_vif1_interpreter.cpp` (DIRECT + IMAGE pendente), `ps2_memory.h` (`m_vif1PendingDirectQwc`) | Listras verticais no FMV: o jogo envia cada faixa de 16 px como `DIRECT(4)` (TRXPOS/TRXDIR + tag GIF IMAGE sem dados) seguido de REF com `NOP`+`DIRECT(h*4)` e os pixels (`FUN_0046fcf8`). O runtime, ao ver IMAGE pendente, consumia os próximos bytes do fluxo como pixels **antes** de decodificar os VIFcodes: os 8 bytes de `NOP`+`DIRECT` viravam 2 pixels e cada faixa ficava deslocada 2 px. Agora o resto de IMAGE só é preenchido por payload de DIRECT; o fluxo bruto só continua um DIRECT realmente truncado entre transferências. Verificação: quadro 61 dumpado do FFmpeg e do buffer do jogo são idênticos e limpos (`PS2X_MPEG_DUMP_FRAME`), costuras medidas a cada 16 px na tela. | Não (candidato a PR upstream) | prints sem listras |
| 17 | build | `ps2xRuntime/CMakeLists.txt`: nova opção `PS2X_ENABLE_FUNCTION_LOG_TRACKER` (OFF) | `PS2X_ENABLE_AGRESSIVE_LOGS` (ON) ligava também o rastreio de entrada/saída de **toda** função em `game/disc/ps2_log.txt` (173 MB após poucas execuções) e deixava o jogo bem mais lento. Os logs detalhados continuam; o rastreio fica opcional. | Não | `ps2_log.txt` não cresce |
| 18 | stub (lote 5) | 15 seletores da libpad (tabela "Lote 5") | Enter/Espaço não faziam efeito no vídeo de demonstração: a libpad do jogo, recompilada, conversa com o PADMAN.IRX no IOP emulado, e o teclado/gamepad do host só alimenta o HLE `scePad*`. libmtap (0x3C8130–0x3C84C0) mantida: não há HLE e ela usa o MTAPMAN real. | Não | Enter (Start) pula o vídeo de demonstração |
| 19 | patch runtime | `Kernel/Stubs/MPEG.cpp`: espera do DemuxPss limitada a 30 VSyncs | Com Start durante o vídeo, o player liga a flag de parada (`+0x2D8`) e espera a thread leitora terminar (`FUN_0046f9f8`: `WaitSema(+0x2D4)`); a leitora estava estacionada no DemuxPss (#12) e nunca via a flag. Agora o DemuxPss espera 1 VSync por vez e, se a fila não andar por 30 quadros, devolve 0 ao jogo (contador zera quando volta a consumir). | Não | Start encerra o vídeo de demonstração |
| 20 | patch runtime | `Kernel/Syscalls/Deci2.cpp` | Em builds sem `_DEBUG`/`RUNTIME_DECI2CALL` o `Deci2Call` não fazia nada (nem escrevia `$v0`); o TTY da libkernl (`FUN_003cc038`, usado pelo printf do jogo `FUN_0025d298`) tratava o lixo como envio aceito e girava para sempre em `sceDeci2Poll` esperando o DECI2_WRITEDONE. Agora é sempre emulado como console de varejo: `ReqSend` falha (-1) e o texto é registrado como `[Deci2Call:tty]`. | Não (candidato a PR upstream) | o jogo passa da mensagem de debug após o Start |
| 21 | entry points | `tools/find_code_gaps.py` (822 inícios) | `missing-target 0x405458` (chamado por ponteiro de `0x4065cc`) dentro de `entry_00405450`: regiões só com rótulos `entry_` (código sem função no Ghidra) agora também são divididas após cada `jr $ra`+delay slot (+173 inícios). | Não | log sem `missing-target` |
| 22 | patch runtime | `ps2xIOP/src/modules/mcserv.cpp` | A libmc 2810 do jogo exige `mcserv ≥ 0x20A` / `mcman ≥ 0x20E` (`FUN_0049a030`) e o HLE informava 0x205/0x206 ("libmc: too old release of mcserv.irx"); agora informa as versões dos IRX do disco (0x210 / 0x222). Também implementado o RPC 0x15 = `sceMcGetSlotMax(port)` (`FUN_0049a220`, usado pela checagem de cartão `FUN_00403c10`), que devolve 1 slot. | Não | log sem "too old release" e sem "MCSERV unknown RPC" |
| 23 | patch runtime | `ps2_vif1_interpreter.cpp` + `ps2_memory.h` (`m_vif1Carry`) | Tela preta após o vídeo: o jogo desenha ~9 primitivas/quadro, com ~170 mil `MSCAL`/10 s e **zero `XGKICK`**. Com o MFIFO o fluxo do VIF1 chega em pedaços e o interpretador descartava `MPG`/`UNPACK`/`STMASK`/`STROW`/`STCOL` cortados no fim do pedaço (e lia o resto como VIFcodes): microprogramas e vértices da VU1 corrompidos. Agora os bytes do comando incompleto são guardados e juntados ao próximo pedaço (limpos no `FBRST`). | Não (candidato a PR upstream) | `vu1.xgkick` > 0 e geometria na tela |
| 24 | debug runtime | `runtime/gs/gs_draw_stats.h`, `gs_frontend.cpp`, `ps2_vif1_interpreter.cpp`, `ps2_vu1_core.cpp`, `ps2_runtime.cpp` | Diagnóstico: linha `[run:draws]` a cada 600 ticks com primitivas por destino (FBP/PSM/FBMSK) e contadores `vif1.mpg/unpack/mscal/direct`, `vu1.xgkick(.start)`, `vu1.end.*`; `[run:tick]` com `pmode/dispfb2/display2`. | Sim (instrumentação) | — |
| 25 | patch runtime | `vu/ps2_vu1_lower.cpp`: `JR`/`JALR` leem o valor atual do VI | Ainda tela preta após #23: 14 `XGKICK` no microcódigo (0x20A8–0x3CE0) e nenhum executado. Rastreio da VU1 (`PS2X_VU1_TRACE`): o programa de `0x0` faz `IADDIU vi15, vi0, 0x3EA; JR vi15` para ir à rotina de envio (0x1F50), mas o runtime aplicava ao `JR` o atraso de VI dos desvios condicionais e saltava para o vi15 antigo (0x248), terminando sem enviar nada. Com `JR`/`JALR` lendo o valor atual: ~73 mil XGKICK/10 s, ~148 mil primitivas/10 s e a **tela legal da Rockstar** desenhada pelo motor. `PS2X_VU1_JR_VI_DELAY=1` restaura o comportamento antigo. **A confirmar** contra o PCSX2 (microVU aplica "VI delay" a JR em alguns casos). | Não (experimento que funcionou) | print 150 s em `run_20261015_125600` |
| 26 | patch runtime | `Kernel/EeScheduler.cpp` (`accountCycles`): COP0 `Count` | O runtime nunca atualizava `cop0_count`: `mfc0 Count` lia sempre o mesmo valor e todo tempo medido pelo jogo era zero. A tela legal levava minutos no fade-in (brilho 111→152 em 150 s) com o jogo a ~10 fps e sem leituras de disco. Agora `Count` segue o relógio real a 294,912 MHz no contexto em execução. Resultado: tela legal com brilho total em segundos e o jogo avança para a tela de carregamento (tambor de revólver). | Não (candidato a PR upstream) | prints 105/115 s em `run_20261015_131222` |
| 27 | entry points | `tools/find_code_gaps.py` (1170 inícios) | `missing-target 0x3FAC68` na tela de carregamento: lacuna sem `jr $ra` (a função termina dentro do rótulo seguinte). Nova regra: em lacuna sem `jr $ra`, o 1º word não nulo após o padding inicia função se o código anterior terminou em `jr $ra`+delay. 0x4F02E0–0x4FB340 é dado (bloco antes das tabelas da libipu) e foi excluído. | Não | log sem `missing-target` |
| 28 | patch runtime | `Kernel/Stubs/MemoryCard.cpp` (`sceMcGetInfo`): -1 no 1º acesso | `GetInfo` devolvia sempre 0 ("mesmo cartão"); na libmc o 1º acesso a um cartão formatado devolve -1 ("cartão novo"). Agora: -1 na 1ª consulta de cada porta, depois 0. Com o cartão vazio o jogo procura `/BASLUS-20500RDR/icon.sys` (não existe) e mostra "save danificado, Retry?"; respondendo **No** segue para o aviso de Autosave, LOADING e o **Demo Mode** 3D. | Não | prints em `run_20261015_144146` e `run_20261015_144609` |
| 29 | debug runtime | `ps2xIOP/src/modules/mcserv.cpp` | Rastreio `[mcserv] fn=.. name=.. result=..` (exceto GetInfo, até 200 linhas). | Sim (instrumentação) | — |
| 30 | patch runtime | `ps2xIOP/src/emulator/services/iop_rpc.cpp`: `sceSifBindRpc`/`sceSifCallRpc` do lado IOP (imports 15/16 do sifcmd) | O AGESND.IRX (driver de som da Angel Studios, roda no IOP emulado) faz `BindRpc` ao servidor **do EE** 0xADDA e repetia o bind para sempre (o IOP emulado não roteia RPC IOP→EE). O EE registra 0xABBA (handler `0x4BD5E0`) e 0xADDA (`0x4BD620`) na thread `FUN_004BD550`. Agora o bind é dado como feito (`cd+0x24 = 0xFFFF0000|sid`) e o `CallRpc` é concluído sem chegar ao EE (resposta zerada), com log `[iop-rpc-client]`. Até o Demo Mode nenhuma `CallRpc` IOP→EE aconteceu. | Sim: rotear de verdade se aparecer `CallRpc ... (dropped)` no log | log `[iop-rpc-client] BindRpc cd=0x50470 sid=0xadda (faked)` |
| 31 | patch runtime | `ps2xIOP/src/emulator/imports/iop_ioman.cpp/.h`, `iop_emulator.cpp` (`IopIoman` recebe o `IopHost`) | O ioman do IOP emulado só tinha `AddDrv`/`DelDrv`: o AGESND abria `cdrom0:\BANKS.DAT;1` (bancos de som) com `open` (import 4) e recebia lixo. Implementados `open` (só leitura, via `translateGuestPath` → `cdrom0:` = `game/disc`), `close` (5), `read` (6) e `lseek` (8); descritores a partir de 3. Log `[iop-ioman] open ... host=... -> fd` (até 64). Primeira versão recusava `O_RDONLY` (máscara de escrita incluía 0x3) — corrigido: escrita = `WRONLY(2)`/`APPEND`/`CREAT`/`TRUNC`. | Não | log `open 'cdrom0:\BANKS.DAT;1' ... -> 3` e sem `AgeSnd ERROR` |
| 32 | patch runtime | `ps2xIOP/src/emulator/core/iop_memory.cpp`: leitura de `ENDX` do SPU2 (0x1F900340 / 0x1F900740) = 0x00FFFFFF | O IOP emulado não tem SPU2. O AGESND acompanha o fim das vozes pelo ENDX; lido como 0, as 18 vozes disparadas ficavam no estado 0x15 ("tocando") para sempre na resposta do RPC 0xACE7 (`FUN_004BD478` → bytes `+0x50..`). Após o Start no Demo Mode o jogo espera as vozes terminarem (`FUN_0027EE78`, até 300 quadros em `FUN_00101BA8`) e depois cai num `b .` proposital (0x102B18). Agora toda voz consta como terminada (estado 0x17). | Sim: rever quando houver SPU2/áudio do IOP | dump `[rpc-dump] sid=0xace7` com `17` em `+0x50` e sem laço em 0x102B18 |
| 33 | entry points | `tools/find_code_gaps.py` (1414 inícios) | Ciclo `LOADING → preto → LOADING` após o Start no Demo Mode: a thread principal chamava `0x4BF098` por ponteiro de vtable (`JALR` em 0x1829F4) — função de 2 instruções `lui v0; jr ra; addiu v0` (devolve uma string, nome de tipo) após padding de nops, colada num fragmento órfão em `entry_004bf044`. O runtime pulava a chamada e o carregador recebia lixo. Nova regra nas regiões só `entry_`: `lui` + `jr $ra` + `addiu` após nop inicia função (+3: 0x4BF058, 0x4BF098, 0x4C44B0). A versão sem exigir `lui`/`addiu` pegava epílogos (`ld ra; jr ra; addiu sp`) e foi descartada. 2ª iteração: `missing-target 0x2BB100` (thread principal morre no carregamento do Demo Mode): lacuna sem `jr $ra` colada logo após o `jr $ra`+delay de `FUN_002baf18`, sem padding — a regra de #27 exigia padding; agora aceita sem padding se a lacuna tiver 4+ words (lacunas de 2 words são sobras de epílogo). 3ª iteração: `missing-target 0x2B7FE0` — a lacuna começava com sobra de epílogo (`addiu sp,sp,+N` + nop, 0x2B7FD8) que virava o início e engolia a função real; agora essas sobras são puladas, e um início que já é `jr $ra` ("jr ra; lw v0,..", 0x4C2628) fecha a função na hora. 4ª iteração: `missing-target 0x234500` (após o Start no vídeo do título): a sobra era `sltiu v0,v0,1; nop`. Regra final de sobra: instrução isolada + nop no ponto de início, sem ser desvio, que seja `addiu sp,sp,+N` ou leia at/v0/v1/t* (sem valor na entrada de função). Total 1414 inícios. | Não | log sem `missing-target 0x4bf098` / `0x2bb100` / `0x2b7fe0` / `0x234500` |
| 34 | debug runtime | `Kernel/Syscalls/RPC.cpp` (`PS2X_RPC_TRACE=1`) | Rastreio das `sceSifCallRpc` do EE: `[rpc-trace]` (até 200 por SID) e `[rpc-dump]` (hexdump de até 0x140 bytes da resposta a cada 50 chamadas por SID). | Sim (instrumentação) | — |

### Lote 1 — identificação (2026-10-14)

Nomes por padrão do SDK 2.8 (libkernl/libcdvd sem símbolos): SIDs, números de função RPC, ordem
da sequência de boot em `FUN_0014fdb8` e strings. Os de confiança média estão marcados.

| Endereço | Nome | Evidência |
|---|---|---|
| 0x003CCC58 | sceSifGetSreg | `return *(u32*)(0x767300 + i*4)`; chamado só por InitRpc no laço de espera |
| 0x003CCC80 | sceSifInitCmd | AddDmacHandler, sceSifSetDChain, Get/SetReg 0x80000000/1; buffers 0x767xxx |
| 0x003CCF00 | sceSifExitCmd | RemoveDmacHandler + DisableDmac; chamado por ExitRpc |
| 0x003CCF38 | sceSifAddCmdHandler | grava tabela 0x767164/0x76716c; chamado por InitRpc e init da libcdvd |
| 0x003CCFB0 | sceSifRemoveCmdHandler | mesma tabela; chamado pelo exit da libcdvd |
| 0x003CD3B0 | sceSifInitRpc | laço `while(!GetSreg(0))` + `SetReg(0x80000002,1)`; 11 chamadores incl. boot |
| 0x003CD550 | sceSifExitRpc | chama ExitCmd; chamado por RebootIop |
| 0x003CD9F0 | sceSifBindRpc | (client, sid, mode); CreateSema/WaitSema; binds de 0x80000592/3/5, 0x80000003 |
| 0x003CDBC0 | sceSifCallRpc | (client, fno, mode, send, ssize, recv, rsize, endfunc); 46 chamadores |
| 0x003CDDB8 | sceSifCheckStatRpc | 60 bytes, sem chamadas; usado nas esperas da libcdvd ("N cmd wait") |
| 0x003D0398 | sceSifRebootIop | string `rom0:UDNL `; ExitRpc → reset → InitRpc; `while(!f(img))` no boot |
| 0x003D0348 | sceSifSyncIop | lê SIF reg; `while(!f())` logo após o reboot |
| 0x003CFEA0 | sceSifLoadFileReset | zera flag de bind do loadfile (0x76a7e8); chamado no boot após InitRpc |
| 0x003D01B0 | sceSifLoadModule (**confiança média**) | wrapper de 0x3CFF88 (CallRpc no client do loadfile 0x76a7c0) com 4 args (path, arglen, args, &result): é `sceSifLoadStartModule`; o HLE ignora `result` (fica 0 = residente) |
| 0x003CFB70 | sceSifInitIopHeap | `BindRpc(0x76a400, 0x80000003)` |
| 0x003CFBF8 | sceSifAllocIopHeap | CallRpc fno 1 no client 0x76a400; boot chama com 0x600000 |
| 0x003CFC68 | sceSifFreeIopHeap | CallRpc fno 2 no client 0x76a400 |
| 0x003CEA50 | sceFsReset | zera estado do fileio (0x76a328); boot |
| 0x003CEA88 | sceOpen | (path, flags, mode); CallRpc fno 0 no client do fileio 0x76a300 |
| 0x003CED10 | sceClose | CallRpc fno 1 |
| 0x003CEE90 | sceLseek | (fd, off, whence); fno 4 |
| 0x003CF0C8 | sceRead | (fd, buf, size); fno 2 |
| 0x003CF328 | sceWrite | (fd, buf, size); fno 3 |
| 0x003CF5E8 | sceIoctl | (fd, req, arg); fno 5 |
| 0x003C77C0 | sceCdInit | `BindRpc(.., 0x80000592)` + CallRpc fno 0; boot |
| 0x003C8038 | sceCdMmode | comando S (client 0x4fd600, bind 0x80000593 em 0x3C7650); boot com `DAT_004f6f70` |

Identificados e **ainda não ligados**: `0x003CFED8` = sceSifSearchModuleByName (loadfile fno 9; sem
handler no runtime), servidor RPC do lado EE (`0x003CDDF8` sceSifSetRpcQueue, `0x003CDE90`
sceSifRegisterRpc, `0x003CDF68` sceSifGetNextRequest, `0x003CDFC0` sceSifExecRequest, `0x003CE188`
sceSifRpcLoop — confirmados por decompilação). Usado por `FUN_004bd550` (thread criada pela init de som
`FUN_004bd238`, que carrega os 7 IRX de som e faz bind no SID 0xACE7 do AGESND, com 99 tentativas e
laço infinito se falhar) para registrar os servidores EE 0xABBA/0xADDA. Mantidos recompilados: o IOP
emulado não implementa CallRpc IOP→EE (ordinais 14–16 do sifcmd são no-op em `iop_rpc.cpp`), então a
thread só dorme em `sceSifRpcLoop`, demais funções da libcdvd (comandos N via 0x3C7338: `0x003C7AA8`, `0x003C7DC0`,
`0x003C7540` = sceCdSync?, `0x003C75E0` = sceCdSyncS?), libmtap (0x3C8130–0x3C84C0, strings) e
libpad (0x3C8528 em diante, strings).

### Lote 2 — libcdvd (2026-10-14)

A libcdvd do jogo é pequena (0x3C7100–0x3C8108): sem `sceCdSearchFile` nem streaming. Números de
comando do cdvdfsv: N (SID 0x80000595, prólogo 0x3C7338) e S (SID 0x80000593, prólogo 0x3C7650).

| Endereço | Nome | Evidência |
|---|---|---|
| 0x003C74A8 | sceCdNcmdDiskReady | comando N 0xE, sem args; chamadores testam `== 6` (SCECdNotReady) |
| 0x003C7540 | sceCdSync | (mode); `CheckStatRpc` no client N 0x4FCC50; string "N cmd wait" |
| 0x003C75E0 | sceCdSyncS | (mode); mesmo padrão no client S 0x4FD600; "S cmd wait" |
| 0x003C7AA8 | sceCdReadChain | (tags, mode); comando N 0xF; triplas {lsn, n, buf} até -1; jogo chama com 0x7483C0 |
| 0x003C7DC0 | sceCdRead | (lsn, sectors, buf, mode); comando N 1; tamanho do setor por modo 0x800/0x918/0x924 |
| 0x003C7FA0 | sceCdGetError | comando S 4, -1 se a RPC falha; jogo: `while(sceCdRead(..)!=1); while(sceCdSync(0)); if(!sceCdGetError())` em 0x407318 |

Internas não ligadas (inalcançáveis com o HLE): 0x3C7100 (cria semáforos), 0x3C71B8 (exit), 0x3C7258/
0x3C72B0 (handler de callback via AddCmdHandler 0x80000012), 0x3C7338/0x3C7650 (prólogos N/S).

### Lote 3 — libmpeg / player de FMV (2026-10-15)

Player do jogo: setup `FUN_0046f548`, encerramento `FUN_0046f7c0`, início `FUN_0046f858`, thread
leitora `FUN_004713f0` (lê blocos do .PSS e chama DemuxPss), quadro `FUN_0046fa60` (salva o
scratchpad, espera o MFIFO esvaziar, GetPicture, restaura). Libmpeg em 0x4925A8–0x4993xx.

| Endereço | Nome | Evidência |
|---|---|---|
| 0x00493140 | sceMpegInit | sem args; primeiro passo do setup |
| 0x004931F0 | sceMpegCreate | (mp, work, size); `size = w*h*9/2 + 0x2800` |
| 0x00493530 | sceMpegAddCallback | (mp, type, fn, data); tipos 0,1,2,3,5 |
| 0x004928D8 | sceMpegAddStrCallback | (mp, type, ch, fn, data); tipo 4 com canais 1–4 (áudio), tipos 0 e 2 |
| 0x004928B8 | sceMpegDemuxPss | (mp, buf, size); 32 bytes; chamado pela thread leitora |
| 0x00493410 | sceMpegGetPicture | (mp, rgb32, mbcount); buffer `mbcount*1024`, `mbcount = w*h/256`; ok se ≥ 0 |
| 0x00493468 | sceMpegIsEnd | `return mp->sys->isEnd` (`**(mp+0x40)`) |
| 0x00493478 | sceMpegReset | (mp); ao iniciar a reprodução (`FUN_0046f858`) |
| 0x00493400 | sceMpegDelete | (mp); 8 bytes; no encerramento do player |

### Lote 4 — libsdr (2026-10-15)

| Endereço | Nome | Evidência |
|---|---|---|
| 0x0049AFE8 | sceSdRemoteInit | string "sceSdRemoteInit() RPC bind error!"; `BindRpc(0x794240, 0x80000701)` |
| 0x0049B128 | sceSdRemote | variádica (arg, cmd, ...); comandos 0x8010 SetParam, 0x80E0 BlockTrans, 0x8100 BlockTransStatus, 0x8130–0x81D0; CallRpc no client 0x794240 |
| 0x0049B098 | sceSdTransToIOP (não ligada) | `sceSifSetDma` de um bloco para a RAM do IOP + espera `sceSifDmaStat`; funciona com o HLE de SetDma |

### Lote 5 — libpad (2026-10-15)

Camada de controle do jogo: init `FUN_00143478` (libmtap + `scePadInit(0)` + 4 portas), abertura
`FUN_00142c28`, máquina de estados de modo/vibração `FUN_00142e68` (padrão do exemplo do SDK),
leitura por quadro `FUN_001431b0` (`scePadRead` → `0x73BE80`, botões em `[2..3]`, analógicos em `[4..7]`).

| Endereço | Nome | Evidência |
|---|---|---|
| 0x003C8640 | scePadInit | `(0)`; checagem "libpad.a = %d.%d, padman.irx = %d.%d" |
| 0x003C88A0 | scePadPortOpen | `(port, slot, buf)`; "buffer addr is not 64 byte align", "pad port is already open" |
| 0x003C8A88 | scePadPortClose | só no encerramento (`FUN_00142dc8`/`FUN_00142e00`) |
| 0x003C8820 | scePadEnd | só no encerramento (`FUN_001435c8`) |
| 0x003C8BA0 | scePadRead | `(port, slot, data)`; 0 = sem dados |
| 0x003C8D08 | scePadGetState | retorno 0–7 testado no switch (2 FINDCTP1, 6 STABLE, 7 ERROR) |
| 0x003C8DF0 | scePadGetReqState | 0 completo, 1 falhou |
| 0x003C8F70 | scePadInfoMode | `(port, slot, 1|2, 0)`: CurID / CurExID (4 = DualShock, 7 = analógico) |
| 0x003C90A8 | scePadSetMainMode | `(port, slot, 1, lock)` |
| 0x003C8E48 | scePadInfoAct | `(port, slot, -1, 0)`: número de atuadores |
| 0x003C9220 | scePadSetActAlign | `(port, slot, align)` após InfoAct > 0 |
| 0x003C9160 | scePadSetActDirect | `(port, slot, data[6])` vibração |
| 0x003C9460 | scePadInfoPressMode | antes de Enter/ExitPressMode |
| 0x003C94C0 | scePadEnterPressMode | conforme preferência do jogo |
| 0x003C9518 | scePadExitPressMode | idem |
