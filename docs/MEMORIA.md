# Mapa de memória do jogo (para testes e trapaças)

Endereços de RAM do PS2 identificados no *Red Dead Revolver* recompilado: vida, munição, dinheiro, fase,
itens etc. Servem para alterar o jogo durante os testes (pular fase, vida infinita) e, no futuro, para um
sistema de trapaças (`.pnach`) e para a base fixa do Cheat Engine.

**Build:** ELF descomprimido `game/SLUS_205.00.unpacked.elf`, CRC32 `EF570C27` (SLUS-20500 v1.03, NTSC-U).
Endereços de **dados** só valem para este build. Endereços **dinâmicos** (objetos no heap, como o jogador)
mudam entre execuções: registre o ponteiro estático que leva a eles, não o endereço final.

Regra do projeto: só entra aqui o que foi **confirmado** no runtime (lido e alterado com efeito visível) ou no
Ghidra. Hipóteses ficam marcadas como tal.

## Como achar e alterar (com o jogo aberto)

**No próprio jogo (recomendado):** F11 abre o menu de debug (#73). Ex.: balas no tambor — digite `6`, Enter;
atire uma vez; digite `5`, N; repita até sobrar 1–3 candidatos; selecione com as setas, `W` para escrever um valor
de teste (digitado) e ver se o HUD/jogo muda; `S` salva em `config/cheats.txt`. Valor desconhecido (barra de vida):
Enter com o campo vazio (todos os endereços), tome dano, `D` (diminuiu); espere sem tomar dano, `U` (não mudou);
repita. Tipo com `T` (vida costuma ser `f32`).

**Por script (fora do jogo):**

O runtime grava no log `[mem] pid=... rdram=0x...`; `tools/ee_mem.py` usa isso para ler e escrever a RAM do
PS2 ao vivo:

```powershell
python tools/ee_mem.py scan 6 --type u32      # ex.: 6 balas no tambor
# atire uma vez no jogo
python tools/ee_mem.py next 5                 # quem agora vale 5
python tools/ee_mem.py list                   # poucos candidatos? teste escrevendo:
python tools/ee_mem.py write 0x00XXXXXX 6
python tools/ee_mem.py freeze 0x00XXXXXX 6    # mantém o valor (Ctrl+C para parar)
```

Valores desconhecidos (barra de vida): `scan` de um valor qualquer não funciona; use `snap` antes/depois e
filtros `next dec` / `next unchanged` partindo de uma busca ampla (`--type f32` para barras em float).

## Endereços confirmados

| Endereço | Tipo | O que é | Estático/dinâmico | Como foi achado | Data |
|---|---|---|---|---|---|
| _(nenhum ainda)_ | | | | | |

## Hipóteses (não confirmadas)

| Endereço | O que pode ser | Evidência |
|---|---|---|
| `0x0073BE80` | buffer de leitura do controle (`scePadRead`, porta 0): botões em `[2..3]`, analógicos em `[4..7]` | FUNCOES.md (Lote 5, libpad), análise no Ghidra |

## Fases e capítulos

| Capítulo | Nome | Arquivo de nível | ID interno |
|---|---|---|---|
| 1 | Bull's Eye (tutorial) | `LVLS_SNG/...` (ver ESTADO.md) | _(a identificar)_ |

## Fontes externas

Não há `.pnach` público para este CRC (`E169BAF8` no PCSX2). Existem códigos CodeBreaker (formato CB7,
criptografado) para o SLUS-20500 em PSX Data Center e almarsguides; podem ser decodificados para obter
endereços crus, que então precisam ser confirmados aqui.
