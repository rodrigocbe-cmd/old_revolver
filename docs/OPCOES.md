# Menu de opções (referência: Red Dead Redemption 2 para PC)

Pedido do usuário (2026-11-02): um menu de opções mais completo, tendo o RDR2 PC como referência. Os textos dos menus devem ser menores, porque em alta resolução e em TVs grandes eles ocupam espaço demais.

## Estrutura do RDR2 PC (referência)

A busca de 2026-11-02 confirmou:
- um menu de configurações acessível pelo menu principal e pela pausa;
- seções Gráficos, Sistema/Display, Interface, Controles e Áudio;
- predefinição de qualidade com 20 níveis, "favorecer desempenho/qualidade" e API Vulkan/DX12;
- escala de resolução e ferramenta de benchmark;
- mira automática e sensibilidade de câmera e de mira separadas.

O resto da lista abaixo vem do conhecimento do jogo (**hipótese**; conferir no menu do RDR2).

| Seção | Opções |
|---|---|
| **Display** | monitor, resolução, taxa de atualização, modo de tela (tela cheia / sem borda / janela), V-Sync, buffer triplo, limite de FPS, proporção, brilho/calibragem, HDR, área segura da interface |
| **Gráficos** | predefinição de qualidade, escala de resolução, antisserrilhamento (FXAA, TAA, MSAA), nitidez do TAA, qualidade de texturas, filtragem anisotrópica, iluminação, sombras, reflexos, água, volumétricos, partículas, desfoque de movimento, API |
| **Áudio** | volume geral, música, efeitos, diálogos; saída (estéreo, fones, surround); faixa dinâmica |
| **Controles (teclado e mouse)** | teclas por ação, sensibilidade do mouse (câmera e mira), inverter eixo Y, aceleração do mouse, alternar ou segurar (mira, correr, agachar) |
| **Controle (gamepad)** | layout, sensibilidade, zona morta, inverter Y, vibração, mira automática |
| **Câmera** | sensibilidade, tremor de câmera, campo de visão, modo da câmera |
| **Interface (HUD)** | exibição do HUD (sempre, contextual, nunca), elementos (minimapa, retícula), **tamanho das legendas** e legendas ligadas/desligadas, unidades |
| **Gameplay** | modo de mira (mira automática), salvamento automático, dicas |

## Mapa para o Red Dead Revolver

Onde cada opção pode viver:
- **jogo**: o menu Options original do PS2 já tem a opção;
- **PC**: o runtime ou os overrides implementam (é nosso).

| Seção | Opção no RD Revolver | Onde | Situação |
|---|---|---|---|
| Display | Resolução da janela | PC (`PS2X_WINDOW_SIZE`) | existe por variável de ambiente; falta no menu |
| Display | Modo de tela: tela cheia sem borda / janela | PC (`PS2X_FULLSCREEN`) | idem |
| Display | Proporção 16:9 / 4:3 (widescreen) | PC + jogo (`-widescreen`, `PS2X_ASPECT`) | idem |
| Display | V-Sync / limite de FPS (30, 60, livre) | PC (raylib `SetTargetFPS`/VSync) | a fazer |
| Display | Brilho (gama) | PC (shader na apresentação) | a fazer |
| Display | Área segura / escala do HUD | jogo (desenha em 640x448) | **pesquisa**: ver abaixo |
| Gráficos | Predefinição: Baixa / Média / Alta / Ultra | PC | a fazer (agrupa as opções abaixo) |
| Gráficos | Supersampling (1x, 4x, 16x) | PC (`PS2X_GS_SSAA`) | existe; 16x pesado (docs/ESTADO.md) |
| Gráficos | Texturas supersampled | PC (`PS2X_GS_SS_TEXTURES`) | existe |
| Gráficos | Saída em alta resolução | PC (`PS2X_GS_HIRES`) | existe |
| Gráficos | Filtro da imagem (bilinear / nítido) | PC (filtro da textura da janela) | a fazer |
| Gráficos | Mipmaps desligados (mais detalhe ao ampliar) | PC (paraLLEl-GS `Hacks::disable_mipmaps`) | a fazer, experimental |
| Gráficos | Contador de FPS | PC (overlay) | a fazer |
| Áudio | Volume geral | PC (saída do SPU2) | a fazer |
| Áudio | Música / efeitos / diálogos | jogo (Options → Sound, `gamesound.swui`) | existe no jogo |
| Controles | Teclas por ação (até 3) | PC (`controls.ini`, `src/rdr_controls.cpp`) | existe no arquivo; falta a tela |
| Controles | Sensibilidade do mouse, inverter Y | PC (`PS2X_MOUSE_SENS`, `PS2X_MOUSE_INVERT_Y`) | existe por variável de ambiente |
| Controles | Inclinação de andar | PC (`walk_tilt`) | existe no `controls.ini` |
| Controles | Alternar ou segurar correr/mirar | PC (mapa de teclas) | a fazer |
| Controle | Vibração | jogo (Player Options) | existe no jogo |
| Câmera | Sensibilidade, inverter | jogo (Player Options → câmera, `multioption_sensitivity.swui`) | existe no jogo |
| Interface | Legendas | jogo (Options) | **hipótese**: conferir se o jogo tem |
| Interface | Ícones de tecla / de controle | PC (`rdr_keyicons.cpp`) | existe (teclado); falta escolher |
| Gameplay | Dificuldade | jogo (`difficultyselect.swui`) | existe no jogo |

As opções de PC ficam em `Documentos\Red Dead Revolver\settings.ini`, ao lado do `controls.ini`, e o menu só edita esse arquivo. Assim valem também para quem abre pelo lançador ou pela Steam.

## Tamanho dos textos

- **Menus nossos** (o novo menu de opções, a caixa de saída, o lançador): o texto deixa de crescer com a tela inteira. Medido pela altura da janela, o corpo fica em ~1,8% (≈20 px em 1080p, ≈39 px em 4K) e os títulos em ~3%. Hoje a caixa de saída e o lançador usam uma escala proporcional a 640x448, e o texto fica do tamanho do original do PS2 ampliado. Com o texto menor cabem mais opções por tela, como no RDR2.
- **Menus do próprio jogo** (Options, pausa, loja, HUD): são desenhados pelo jogo dentro da imagem de 640x448 e ampliados junto com ela. Para reduzi-los é preciso mudar a escala com que o jogo desenha as fontes e as caixas. **Pesquisa**: o tamanho e a posição dos textos vêm dos `.swui` (`<TEXT>` com escala) e da função de desenho de texto do motor. Um override poderia reduzir a escala e reposicionar em direção ao centro, como uma "área segura". Fica para depois do menu nosso.

## Onde o menu aparece

1. **Primeira versão:**
   - um item **PC SETTINGS** no lançador;
   - o mesmo menu dentro do jogo, aberto pelo menu de pausa ou por uma tecla (F1 está livre na distribuição), com o jogo pausado como na caixa de saída.
2. **Depois:** interceptar o item Controls do menu Options do jogo (`multioption_controls.swui`) para abrir a aba de controles do nosso menu (docs/CONTROLES.md).

Visual: o cartão preto com o ornamento e as fontes do FRONT.DAT, como na caixa de saída, mas com texto menor. Abas no topo: Display, Gráficos, Áudio, Controles, Interface. Lista à esquerda, valor à direita e uma linha de descrição embaixo, como no RDR2.
