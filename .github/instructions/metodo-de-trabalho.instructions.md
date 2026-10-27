---
applyTo: "**"
---

# Método de trabalho

Este arquivo descreve como trabalhar neste repositório: como investigar, como verificar e como agir
diante de erros. É o método que vinha sendo seguido nas sessões anteriores (Claude Opus 5.5). Vale
para qualquer tarefa, junto com `.github/copilot-instructions.md`, que descreve o projeto.

## 1. Antes de agir

- Leia o pedido inteiro e identifique o resultado esperado. Se o pedido puder ser lido de duas
  formas e a diferença mudar o trabalho, pergunte. Se não mudar, siga a leitura mais provável e diga
  qual escolheu.
- Leia o estado atual antes de propor qualquer coisa: `docs/ESTADO.md` (o que funciona, o que está
  bloqueado) e a entrada relevante de `docs/FUNCOES.md`.
- Abra o código que você vai mudar e os pontos que o chamam. Não edite um arquivo que você não leu
  nesta sessão.
- Não invente nomes de função, endereços, variáveis de ambiente, flags ou APIs. Se não encontrou no
  código, no log ou na documentação, diga que não encontrou.

## 2. Investigar um problema

1. **Reproduza.** Rode o jogo ou o teste e guarde o log em `logs/` com data e hora. Sem reprodução,
   não há como saber se a correção funcionou.
2. **Leia o log de verdade.** Procure a primeira mensagem anormal, não a última. Erros em cascata
   escondem a causa (ex.: um `VK_ERROR_DEVICE_LOST` no fim era consequência de um fluxo GIF
   desalinhado muito antes).
3. **Formule uma hipótese explícita** e marque-a como hipótese: "hipótese: o pacote termina no meio
   de uma tag GIF".
4. **Meça antes de mudar.** Adicione um contador ou log que confirme ou derrube a hipótese
   (`ps2_gs_stats::countEvent`, aparece em `[run:draws]` com `PS2X_STATS=1`). Uma linha de log
   barata vale mais que uma hora de raciocínio.
5. **Compare com uma referência.** Rode com e sem a mudança (A/B), ou compare com o PCSX2. Uma
   melhora só conta se o número mudar na mesma cena e nas mesmas condições.
6. **Descarte hipóteses com dados.** Várias suspeitas de desempenho deste projeto se mostraram
   falsas quando medidas (limite de dt, orçamento do IOP, IRQ do GS). Registre as descartadas para
   ninguém repetir.

## 3. Ao encontrar um erro

- **Corrija a causa, não o sintoma.** Um `ret0` ou um `skip` que faz o boot avançar é ferramenta de
  triagem, não correção. Se usar, marque como temporário em `docs/FUNCOES.md`.
- **Explique o mecanismo** antes de corrigir: o que entra, o que deveria sair, onde diverge.
  Exemplo: "o VIF1 reembala cada pedaço de IMAGE com uma tag nova; o paraLLEl-GS guarda o estado do
  caminho e lê essa tag como pixel".
- **Mude o mínimo.** Uma correção, um motivo. Não aproveite para reformatar ou "melhorar" código ao
  redor.
- **Siga o estilo do código vizinho:** nomes, comentários (em português neste projeto), densidade de
  comentários, idioma de C++.
- Se a mesma abordagem falhar duas vezes, pare e reavalie a hipótese em vez de tentar uma terceira
  variação.
- Se um comando falhar, leia a mensagem de erro e trate a causa. Não repita o mesmo comando
  esperando outro resultado.

## 4. Verificar antes de dizer que terminou

- Compile. Uma build que passou não prova nada sobre o comportamento, mas uma que falhou prova que
  nada foi testado.
- Rode e confira o resultado concreto: log, contador, captura de tela. "Deve funcionar" não é
  verificação.
- Confira se a mudança não quebrou o caminho antigo (ex.: o GS em software continua funcionando
  quando o backend da GPU está desligado).
- Reporte exatamente o que foi verificado e o que não foi. Se algo ficou sem testar, diga.
  Exemplo: "menu renderiza; gameplay ainda não medido".
- Números com contexto: "62 → 75 quadros por 600 vblanks no tutorial", não "ficou mais rápido".

## 5. Comunicação

- Relatórios curtos: resultado, número, próximo passo. Detalhes vão para `docs/ESTADO.md` e
  `docs/FUNCOES.md`, não para a conversa.
- Seja direto sobre falhas: se não funcionou, diga que não funcionou e por quê.
- Ao propor uma decisão, dê uma recomendação, não uma lista de opções sem posição.
- Cite arquivos com caminho e linha (`src/lib/gs/gs_frontend.cpp:776`).

## 6. Segurança das ações

- **Não faça commits.** O usuário faz. Ao final, sugira a mensagem no formato `area: descrição`.
- Antes de apagar ou sobrescrever algo, olhe o que está lá.
- Nunca adicione arquivos do jogo ao git (ISO, ELF, IRX, dados, C++ gerado, dumps de microcódigo).
- Se precisar instalar uma ferramenta, pode instalar. Se precisar escolher entre opções e o usuário
  não responder, siga a recomendada e registre a escolha.

## 7. Registrar o que aprendeu

- Toda intervenção (stub, skip, patch, override, mudança no runtime) ganha uma linha numerada em
  `docs/FUNCOES.md`: arquivos, o que muda, por quê, se é temporária e como foi verificada.
- Ao fim de cada sessão, atualize `docs/ESTADO.md`: último ponto alcançado, o que mudou, bloqueios,
  próximos passos.
- Lições gerais de recompilação de PS2 (que valem para outros jogos) vão para a skill
  `~/.claude/skills/ps2-recomp/`, em inglês e sem dados do jogo.
