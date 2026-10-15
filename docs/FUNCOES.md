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
handler no runtime), servidor RPC do lado EE usado por `FUN_004bd550` (`0x003CDDF8`, `0x003CDE90`,
`0x003CDF68`, `0x003CDFC0`, `0x003CE188` — SetRpcQueue/RegisterRpc/GetNextRequest/ExecRequest/RpcLoop,
ordem a confirmar), demais funções da libcdvd (comandos N via 0x3C7338: `0x003C7AA8`, `0x003C7DC0`,
`0x003C7540` = sceCdSync?, `0x003C75E0` = sceCdSyncS?), libmtap (0x3C8130–0x3C84C0, strings) e
libpad (0x3C8528 em diante, strings).
