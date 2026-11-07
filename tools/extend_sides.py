"""Estende as laterais de um video 4:3 para 16:9 sem IA (docs/ESTADO.md, sessao de 2026-11-06).

As laterais sao o espelho da borda do proprio quadro, entao fades e mudancas de luz vem junto. A luz e refeita: mede-se a
queda por coluna em cada quadro e troca-se por uma curva unica, suave, do miolo ate a ponta da tela. Assim a vinheta do
original deixa de marcar onde o video terminava. O miolo (quadro menos FEATHER colunas de cada lado) nao e tocado.
Serve para fundos que continuam na horizontal, como as tabuas do RSLOGO. A saida deriva do jogo: fica em pasta ignorada.

Uso: python tools/extend_sides.py <ffmpeg.exe> <video.mpg> <com_audio.mp4> <saida.mp4> [luz_na_ponta]
luz_na_ponta: fracao da luz do inicio da transicao que chega a ponta da tela (padrao 0.18).
"""
import os
import subprocess
import sys

import numpy as np

W, H = 597, 448          # 4:3 em pixel quadrado, sem mudar a altura
OUT_W = 796              # 16:9
FEATHER = 110            # colunas do original, de cada lado, em que a luz e refeita
FPS = '30000/1001'


def smooth(values, size, axis):
    """Media movel com a borda repetida."""
    def run(line):
        return np.convolve(np.pad(line, size, mode='edge'), np.ones(size) / size, mode='same')[size:-size]
    return np.apply_along_axis(run, axis, values)


def light_curve(profile, pad, tip):
    """Luz desejada (log) da ponta da tela ate o fim da transicao; termina com a inclinacao medida no original."""
    length = pad + FEATHER
    start = profile[FEATHER]
    slope = float(np.clip((profile[FEATHER + 10] - profile[FEATHER - 10]) / 20.0, 0.0, 0.03))
    end = start + np.log(tip)
    t = np.arange(length) / length
    return ((2 * t ** 3 - 3 * t ** 2 + 1) * end + (-2 * t ** 3 + 3 * t ** 2) * start
            + (t ** 3 - t ** 2) * length * slope)


def left_side(frame, profile, pad, tip):
    """Devolve `pad` colunas novas e as FEATHER primeiras do quadro com a luz refeita."""
    curve = light_curve(profile, pad, tip)
    outer = frame[:, pad - 1::-1] * np.exp(curve[:pad] - profile[pad - 1::-1])[None, :, None]
    inner = frame[:, :FEATHER] * np.exp(curve[pad:] - profile[:FEATHER])[None, :, None]
    return np.concatenate([outer, inner], axis=1).clip(0, 255)


def main():
    ffmpeg, video, audio, out = os.path.abspath(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
    tip = float(sys.argv[5]) if len(sys.argv) > 5 else 0.18
    raw = subprocess.run([ffmpeg, '-v', 'error', '-i', video, '-vf', 'yadif,scale=%d:%d:flags=lanczos' % (W, H),
                          '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-'], capture_output=True, check=True).stdout
    frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, H, W, 3)
    profiles = frames.mean(axis=(1, 3)).astype(np.float32)
    profiles = np.log(np.maximum(smooth(smooth(profiles, 7, 0), 15, 1), 1.0))
    print('quadros', len(frames))
    pad_left = (OUT_W - W) // 2
    pad_right = OUT_W - W - pad_left
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    enc = subprocess.Popen([ffmpeg, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (OUT_W, H),
                            '-r', FPS, '-i', '-', '-i', audio, '-map', '0:v', '-map', '1:a', '-c:v', 'mpeg4', '-q:v', '2',
                            '-pix_fmt', 'yuv420p', '-c:a', 'copy', '-shortest', out], stdin=subprocess.PIPE)
    for frame, profile in zip(frames, profiles):
        f = frame.astype(np.float32)
        a = left_side(f, profile, pad_left, tip)
        b = left_side(f[:, ::-1], profile[::-1], pad_right, tip)[:, ::-1]
        row = np.concatenate([a.astype(np.uint8), frame[:, FEATHER:W - FEATHER], b.astype(np.uint8)], axis=1)
        enc.stdin.write(row.tobytes())
    enc.stdin.close()
    enc.wait()


main()
