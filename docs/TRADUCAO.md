# Plano de tradução (pt-BR)

Levantamento de 2026-11-05. Nada foi implementado ainda. Os textos traduzidos derivam do jogo e não entram no repositório sem decisão do usuário: a tradução é nova, mas o inglês de referência não.

## Onde estão os textos

| Origem | Volume | Como trocar |
|---|---|---|
| `.strtbl` (`swat.strtbl`, `swat_ps2.strtbl`, 37 `lvl_*.strtbl`) | ~3.000 textos, ~10.600 palavras (estimativa) | pela chave, ver abaixo |
| `.swui` (rótulos de menu: "Options", "Back") | dezenas por tela | `swuiLineRewrite` já reescreve linhas ao carregar |
| `.pck` (Flash: telas de abertura, loja, popups) | a medir | texto como cache de imagem: é o caso mais difícil |
| Imagens com texto (`.tex`) | a medir | trocar a textura |

A estimativa veio de uma leitura simples dos arquivos: inclui textos sem uso e textos do Xbox, e pode ter deixado outros de fora. A contagem real sai da ferramenta da etapa 1.

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

Trocar na busca por chave, sem reescrever os `.strtbl`. `FUN_0017F398(chave)` devolve o objeto de texto (hipótese: visto em `FUN_00101BA8`, com o texto em `+8`). Um override procuraria a chave em `text/pt-br.txt` e devolveria o texto traduzido num buffer próprio. Assim o tamanho do texto pode mudar, e o arquivo do jogo fica intacto. Falta confirmar a função e o formato no Ghidra.

Um arquivo por idioma na pasta `text/`, como o GTA VC, permite outras traduções depois. O menu PC Settings ganharia a opção de idioma.

## Etapas

1. **Extrator:** gerar `text/en.txt` (chave, texto, arquivo de origem) com todos os `.strtbl`, separando textos do PS2 e do Xbox.
2. **Motor:** override da busca, com `text/pt-br.txt` e uma frase de teste na tela.
3. **Tradução dos menus:** `.strtbl` e `.swui`.
4. **Tradução da história e das dicas:** o maior volume. Glifos de botão (`0xBA`...) precisam ficar no texto.
5. **Fontes:** acentos nas fontes de título.
6. **Flash e imagens:** telas de `.pck` e texturas com texto.
7. **Revisão em jogo:** texto que estoura a caixa, legendas e tempo de leitura.

## Riscos

- Português costuma ser mais longo que o inglês: caixas fixas podem estourar.
- As legendas dependem da voz em inglês, então o tempo de leitura pode ficar curto.
- Textos com `%s`, `%d` e glifos de botão precisam manter esses marcadores.
