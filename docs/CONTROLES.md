# Controles de teclado e ícones de teclas (plano)

Pedido do usuário (2026-11-01):
- teclas padrão no estilo do RDR2 para PC;
- uma opção dentro do menu Options do jogo para trocar as teclas;
- ícones de tecla no lugar dos ícones do controle durante o jogo inteiro.

## Onde o jogo mostra botões (levantamento de 2026-11-01, FRONT.DAT e SWAT.DAT)

| Meio | O que é | Onde |
|---|---|---|
| Glifos de fonte | `0xBA` ✕, `0xBC` △, `0xBD` □, `0xBE` ○ (25x25; 18x17 na narrow_lables) | fontes `berrylium`, `damaged`, `courier18`, `narrow_lables` (FRONT.DAT); cópias e variantes (`berryliumx`, `damagedx`, `instruct*`…) em SWAT.DAT e nos `.DAT` das fases |
| Texto (UTF-16, `.strtbl`) | nomes escritos: "R1 button", "L1", "L2", "R2", "L3", "R3", "START", "right analog stick", "directional buttons" | `swat_ps2.strtbl` (SWAT.DAT) e `lvl_*.strtbl`. Existe também `swat_xbx.strtbl` (versão Xbox) |
| Modelos 3D | `xbutton.mod`, `circlebutton.mod`, `squarebutton.mod`, `trianglebutton.mod`, `leftonebutton.mod`, `lefttwobutton.mod`, `rightonebutton.mod`, `righttwobutton.mod`, `selectbutton.mod`, `startbutton.mod`, `joystick.mod`, `controller.mod` | SWAT.DAT e fases; usados pelo widget do controle dos tutoriais (`padmodel_psx2.hud`, já escondido: FUNCOES #90) |
| Imagem da tela de controles | `ps2controller.tex`, `controller_flat.tex` | `multioption_controls.swui` (Options → Controls) |

## Ações do jogo por botão (das dicas em `swat_ps2.strtbl`)

| Botão PS2 | Ação no jogo | Tecla padrão proposta (RDR2 PC) |
|---|---|---|
| Analógico esq. | mover: pouco inclinado = andar, todo = correr | W A S D = andar (analógico pouco inclinado); segurando **Shift esq.** = correr (todo inclinado). Decisão do usuário (2026-11-01), como o sprint do RDR/RDR2 no PC |
| Analógico dir. | mirar, câmera; para trás = agarrar; para frente = sacar (duelo) | mouse |
| L1 | sacar a arma, segurar para mirar, marcar alvos | botão direito do mouse |
| R1 | atirar, marcar alvo no Dead Eye | botão esquerdo do mouse |
| R2 | movimento especial (Dead Eye), tiro antecipado | Caps Lock; alternativa: botão do meio (**hipótese**: as fontes divergem sobre o padrão do RDR2) |
| L2 | trocar a arma principal | Tab |
| ←/→ do direcional | trocar de arma | roda do mouse |
| ✕ (`0xBA`) | pular, rolar esquivando | Espaço |
| □ (`0xBD`) | usar, falar, abrir porta, cobertura, aceitar | E; alternativa: Q (cobertura no RDR2) |
| ○ (`0xBE`) | soco, ataque corpo a corpo | F |
| △ (`0xBC`) | **recarregar** ("Press △ to reload", só pelo texto; não testado em jogo), recusar, pular, cancelar | Backspace |
| L3 | agachar | Ctrl esq. |
| R3 | zoom | Z |
| START | pausa, menu | Esc (Enter também) |
| SELECT | — | M |

A inclinação de "andar" fica no `controls.ini` (`walk_tilt`, padrão 0,74). Medida com `tools/walk_test.ps1` em 2026-11-01, no tutorial (perfnew), segurando W por 3 s:

| `walk_tilt` | Deslocamento em 3 s | Resultado |
|---|---|---|
| 0,20–0,30 | 0 | zona morta |
| 0,40–0,76 | ~2,95 | anda (~1 u/s), sempre na mesma velocidade |
| 0,78–0,84 | ~13 | corre (~4,4 u/s) |
| Shift (1,0) | ~13,9 | corre |

O limiar de corrida fica entre 0,76 e 0,78. O jogo tem só uma velocidade de andar, então não existe um "andar mais rápido". O padrão 0,74 é o maior andar com margem. Correr usa inclinação 1,0.

O mapa atual do runtime (`Kernel/Stubs/Pad.cpp`) é provisório. Os botões de face são Z/X/C/V, L1/R1 são Q/E, L2/R2 são 1/3 e Start é Enter.

## Plano

1. **Teclas configuráveis no runtime.**
   - Uma tabela ação → até duas teclas ou botões do mouse.
   - Lida de `Documentos\Red Dead Revolver\controls.ini`, com os padrões acima.
   - A API `ps2SetKeyBindings` (runtime) é preenchida por `src/`.
2. **Ícones: o mesmo ponto de carga.**
   - Todos os arquivos dos `.DAT` passam pelo leitor de pacotes "Dave" do jogo (FUN_002CAB88).
   - Um override nesse leitor corrige, logo depois da descompressão:
     - as páginas `.tex` das fontes, trocando os glifos `0xBA/0xBC/0xBD/0xBE` por teclas desenhadas com o nome da tecla atual;
     - os `.strtbl`, trocando "R1 button" etc. por glifos de tecla.
   - Para os botões sem glifo (L1, R1, L2, R2, L3, R3, Start), a ideia é reaproveitar códigos pouco usados da fonte (`0xA9` ©, `0xAE` ®, `0xAB` «, `0xBB` »…). Assim "R1" (2 caracteres) vira glifo + espaço e o tamanho do texto não muda.
   - Precisa confirmar no Ghidra a função que entrega o arquivo descomprimido e o formato do `.strtbl` (cabeçalho `0x0A`, tabela de offsets).
3. **Opção no menu Options.**
   - O item Controls (`multioption_controls.swui`) hoje mostra a imagem do controle.
   - A ideia é interceptar a abertura dessa tela e mostrar no lugar uma tela de ajuste de teclas, no visual do jogo (cartão preto e fontes do FRONT.DAT, como a caixa de saída).
   - Depois de trocar uma tecla, os ícones são refeitos: as páginas de fonte já carregadas são corrigidas de novo.
4. **Modelos 3D dos botões.** Ficam por último, só se aparecerem fora do widget dos tutoriais.

## Ícones fora das fontes (2026-11-01)

Algumas telas são pacotes Flash próprios (`.pck`), com os ícones de botão como imagens embutidas, e não usam os glifos das fontes:
- **Loja:** `shop.swui` + `shop.pck` (SWAT.DAT, 438 KB, sem compressão). O pacote tem os símbolos `theSquare`, `Done` e `Menu_SHOP_BuyItem`.
- **Menu antes da fase** (Start / Weapon Selection / Shop, "✕ OK"): provavelmente `popups_01_ingame.pck` (FRONT.DAT).

Para trocar esses ícones é preciso entender o formato `.pck` e achar os bitmaps dos botões. A troca usaria o mesmo override de `FUN_002C9F40` das fontes.

Na caixa de saída do runtime, o "voltar", o confirmar e o cancelar também aceitam as teclas do mapa (△, ✕ e ○, via `ps2PadGetKeyMap`). Antes só valiam V e o 1 do teclado numérico.

## Mapa do △ (2026-11-05)

Levantado por análise estática (`swat.strtbl`, `swat_ps2.strtbl`, `.swui`, código gerado); nada foi testado apertando o botão.

**Gameplay e diálogos (textos):**
- recarregar: "Press △ to reload";
- pular: "Press △ to Skip", "skip to gameplay", "skip tutorial", "skip training section";
- recusar: "Press □ to Accept / Press △ to Decline";
- voltar/encerrar no treino: "return to current exercise", "end training section".

O código do gameplay não testa o bit `0x1000` direto (nenhuma ocorrência fora das telas de menu), então a leitura deve passar por outra camada. **Hipótese**, falta achar essa função.

**Menus:**
- `BUTTON_BACK` = △, `BUTTON_CONFIRM` = ✕, `BUTTON_ALT1_OPTIONS` = ○ (`rdr_overrides.cpp`, FUNCOES.md #86). `BUTTON_ALT2_MISC` ainda sem confirmação.
- 11 telas `.swui` têm rodapé com `BUTTON_BACK`: `gamepicture`, `gamesound`, `gameplayeropt`, `mainoptions`, `mpmode`, `multioption_controls`, `multioption_menu`, `multioption_name`, `multioption_profile`, `multioption_sensitivity`, `storyselect`.
- Cada tela lê a máscara em `obj+0x304`: △ (`0x1000`) e ○ (`0x2000`) são ações diferentes, e o △ costuma chamar `FUN_00104A48`.
- 17 funções de tela testam `0x1000`: `0x116B98`, `0x11DBB8`, `0x11F0D8`, `0x1208B0`, `0x123FF0`, `0x127218`, `0x1288A0`, `0x129E10`, `0x12AD08`, `0x12B058`, `0x12C290`, `0x12CDB0`, `0x12F0E8`, `0x130B00`, `0x136280`, `0x1365D8`, `0x137D38`. Várias delas também testam □ junto com ✕.
- Ícones fora das fontes: os `.pck` da loja e do menu antes da fase têm botões embutidos como imagem (acima).

