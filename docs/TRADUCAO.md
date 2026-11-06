# Plano de tradução (pt-BR)

Levantamento de 2026-11-05; extrator e motor prontos em 2026-11-06. Por enquanto só inglês e português (decisão do usuário): inglês é o texto original, português vem de `text/pt-br.txt`. Os textos traduzidos derivam do jogo e não entram no repositório sem decisão do usuário: a tradução é nova, mas o inglês de referência não.

## Onde estão os textos

| Origem | Volume | Como trocar |
|---|---|---|
| `.strtbl` (`swat.strtbl`, `swat_ps2.strtbl`, 37 `lvl_*.strtbl`) | 1.634 textos, 8.770 palavras (extrator, sem Xbox e sem cópias de reserva) | pela chave, ver abaixo |
| `.swui` (rótulos de menu: "Options", "Back") | dezenas por tela | `swuiLineRewrite` já reescreve linhas ao carregar |
| `.pck` (Flash: telas de abertura, loja, popups) | a medir | texto como cache de imagem: é o caso mais difícil |
| Imagens com texto (`.tex`) | a medir | trocar a textura |

A contagem ainda inclui textos sem uso. Fontes usadas: berrylium 1.358, damaged 123, bosshealth 102, regulatorcondensed 49, instructions 2.

## Fontes

Teste de 30 caracteres do português (ã, á, à, â, é, ê, í, ó, õ, ô, ú, ç, maiúsculas...):

| Fonte | Tem | Uso |
|---|---|---|
| `berrylium` | 29 de 30 | texto principal |
| `narrow_lables` | 29 de 30 | rótulos |
| `steelfish` | 28 de 30 | |
| `damaged`, `courier18` | 1 de 30 | títulos, diversos |
| `regulatorcondensed`, `arial14`, `woodcut_small` | 0 de 30 | títulos dos menus |

Para as fontes sem acento há duas saídas: desenhar os glifos nas páginas `.tex` e na tabela `.fonttex`, reaproveitando o que `rdr_keyicons.cpp` já faz, ou deixar títulos em maiúsculas sem acento. O desenho é melhor; começar sem ele não bloqueia o resto.

## Como aplicar

Troca feita na busca por chave, sem reescrever os `.strtbl` (`src/rdr_text.cpp`). Confirmado: `FUN_0017F398(chave)` é a única entrada para `FUN_0017BF38` e devolve o objeto de texto, com a cadeia UTF-16 em `+8`, alocada por `FUN_00219E70` e liberada por `FUN_00219F70` no destrutor `FUN_0017B980`. O override procura a chave em `text/pt-br.txt` e troca a cadeia no objeto, então o tamanho do texto pode mudar e o arquivo do jogo fica intacto. O hash da chave diferencia maiúsculas de minúsculas. Cada entrada do `.strtbl` também traz fonte e escala, ainda não usadas. O arquivo tem lugar para 10 idiomas; só o primeiro está preenchido.

Um arquivo por idioma na pasta `text/`, como o GTA VC. `RDR_LANG` escolhe o idioma (padrão `pt-br`; sem arquivo, fica o original). O menu PC Settings ganhará a opção com inglês e português.

## Etapas

1. **Extrator (pronto):** `python tools/text_extract.py game/disc out/text/en.txt`.
2. **Motor (pronto):** frases de teste vistas na tela de perfil, com ç e ã. Texto longo passou da borda do papel em "Change Name".
3. **Tradução dos menus:** `.strtbl` e `.swui`.
4. **Tradução da história e das dicas:** o maior volume. Glifos de botão (`0xBA`...) precisam ficar no texto.
5. **Fontes:** acentos nas fontes de título.
6. **Flash e imagens:** telas de `.pck` e texturas com texto.
7. **Revisão em jogo:** texto que estoura a caixa, legendas e tempo de leitura.

## Riscos

- Português costuma ser mais longo que o inglês: caixas fixas podem estourar.
- As legendas dependem da voz em inglês, então o tempo de leitura pode ficar curto.
- Textos com `%s`, `%d` e glifos de botão precisam manter esses marcadores.
