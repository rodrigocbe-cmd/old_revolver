"""Classifica um print da janela do jogo pela distribuição de brilho (heurística para o roteiro de teste)."""
import sys

import numpy as np
from PIL import Image

im = Image.open(sys.argv[1]).convert('RGB')
w, h = im.size
a = np.asarray(im.crop((20, 75, w - 20, h - 20))).astype(int)
mean = float(a.mean())
mx = int(a.max())
# texto claro concentrado na metade de baixo (diálogos) vs. imagem cheia (vídeo/demo)
lower = a[a.shape[0] // 2:]
bright_lower = float((lower.max(axis=2) > 150).mean())
orange = float(((a[:, :, 0] > 120) & (a[:, :, 1] < 90) & (a[:, :, 2] < 60)).mean())

if mean > 12:
    state = 'scene'          # vídeo, demo 3D ou menu com fundo
elif mx > 200 and bright_lower > 0.01 and orange > 0.0008:
    state = 'autosave'       # aviso de Autosave (texto + ícone laranja do tambor)
elif mx > 200 and bright_lower > 0.01:
    state = 'dialog'         # diálogo de texto (memory card)
elif mx > 200 and bright_lower > 0.003:
    state = 'demotext'       # tela escura só com "Demo Mode" (início da demonstração)
elif mx > 60:
    state = 'loading'
else:
    state = 'black'
print('%s mean=%.1f max=%d lowtxt=%.4f orange=%.4f' % (state, mean, mx, bright_lower, orange))
