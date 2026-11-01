"""Resumo de uma gravacao do SPU2 (PS2X_SPU2_PCM, s16le estereo 48 kHz): volume RMS e pico por segundo.

Uso: python tools/audio_report.py <arquivo.pcm> [--wav saida.wav] [--summary [--label NOME] [--first N | --last N]]
--summary: uma linha so (segundos com som / total, RMS medio dos segundos com som, primeiro segundo com som).
"""
import sys
import wave

import numpy as np


def main():
    path = sys.argv[1]
    try:
        data = np.fromfile(path, dtype='<i2')
    except OSError:
        print(f'{path}: sem gravacao'); return
    data = data.reshape(-1, 2).astype(np.float64)
    secs = len(data) // 48000
    if '--summary' in sys.argv:
        label = sys.argv[sys.argv.index('--label') + 1] if '--label' in sys.argv else path
        if '--first' in sys.argv:
            secs = min(secs, int(sys.argv[sys.argv.index('--first') + 1]))
        if '--last' in sys.argv:
            keep = min(secs, int(sys.argv[sys.argv.index('--last') + 1]))
            data = data[(secs - keep) * 48000:secs * 48000]
            secs = keep
        rms = [np.sqrt((data[s * 48000:(s + 1) * 48000] ** 2).mean()) for s in range(secs)]
        loud = [i for i, r in enumerate(rms) if r > 50]
        avg = np.mean([rms[i] for i in loud]) if loud else 0
        first = loud[0] if loud else '-'
        print(f'{label}: som em {len(loud)}/{secs} s, rms medio {avg:.0f}, primeiro {first}s')
        return
    print(f'{path}: {len(data)} quadros ({len(data) / 48000:.1f} s de audio gerado)')
    line = []
    for s in range(secs):
        chunk = data[s * 48000:(s + 1) * 48000]
        rms = np.sqrt((chunk ** 2).mean())
        line.append(f'{s}s:{rms:.0f}/{np.abs(chunk).max():.0f}')
    print('rms/pico por segundo:', ' '.join(line))
    if '--wav' in sys.argv:
        out = sys.argv[sys.argv.index('--wav') + 1]
        with wave.open(out, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(data.astype('<i2').tobytes())
        print('wav:', out)


if __name__ == '__main__':
    main()
