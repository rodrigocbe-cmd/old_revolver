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
