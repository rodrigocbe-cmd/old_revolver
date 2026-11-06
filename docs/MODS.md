# Mods e ferramentas externas

## Cheat Engine

A memória do jogo fica aberta no processo `RedDeadRevolver.exe`, sem proteção. Ela está em endereços fixos, iguais em toda abertura:

| Área | Endereço no processo | Tamanho |
|---|---|---|
| RAM do PS2 (EE) | `0x200000000` | 32 MB |
| Scratchpad | `0x210000000` | 16 KB |

Endereço no processo = `0x200000000` + endereço do jogo. Exemplo: a estrutura de opções é apontada por `0x742120`, então o ponteiro fica em `0x200742120`.

- Marque **Writable** e **MEM_PRIVATE** nas opções de busca. Os valores são little-endian.
- Ponteiros dentro da memória do jogo têm 32 bits e são endereços do PS2, sem a base: para segui-los, some `0x200000000`.
- Trocar um valor pode não ter efeito imediato: o jogo guarda alguns em registradores ou recalcula a cada quadro.
- Se um dos endereços estiver ocupado, o bloco vai para outro lugar; o log mostra o endereço na linha `[memoria]`.
