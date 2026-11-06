"""Converte os videos do jogo (.PSS / movies/*.mpg) em .mp4 com som, para editar (por exemplo, ampliar para 16:9).

O resultado editado vai para movies/ com o mesmo nome (RSLOGO.mp4) e toca no lugar do original (FUNCOES.md #104).
Um PSS e um MPEG Program Stream com video MPEG-2 (0xE0) e audio no canal privado 0xBD da Sony (cabecalho SShd/SSbd,
ADPCM usado pelo player do jogo, nao decodificado aqui). O som vem da gravacao da saida do jogo:
tools/fmv_audio_test.ps1 -Only <NOME> grava logs/fmv_audio/<NOME>.pcm (48 kHz, 16 bits, estereo).
A saida deriva do jogo: fica em pasta ignorada pelo git (padrao out/movies_mp4).

Uso: python tools/export_movies.py [pasta_dos_videos] [pasta_saida] [--only RSLOGO]
"""
import os
import struct
import subprocess
import sys

FFMPEG = 'out/build/ThirdParty/ffmpeg-prefix/src/ffmpeg_external/bin/ffmpeg.exe'


def demux(data):
    video, audio = bytearray(), bytearray()
    o = 0
    while o + 6 <= len(data):
        if data[o:o + 3] != b'\0\0\1':
            o += 1
            continue
        sid = data[o + 3]
        if sid == 0xBA:
            o += 14 + (data[o + 13] & 7)
        elif sid == 0xB9:
            o += 4
        elif sid >= 0xBB:
            size = struct.unpack('>H', data[o + 4:o + 6])[0]
            if sid == 0xE0 or sid == 0xBD:
                start = o + 9 + data[o + 8]
                body = data[start:o + 6 + size]
                if sid == 0xE0:
                    video += body
                else:
                    audio += body[4:]
            o += 6 + size
        else:
            o += 1
    return video, audio


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = sys.argv[sys.argv.index('--only') + 1].upper() if '--only' in sys.argv else None
    if only:
        args = [a for a in args if a.upper() != only]
    src = args[0] if args else 'game/disc/PSS'
    out = args[1] if len(args) > 1 else 'out/movies_mp4'
    pcm_dir = 'logs/fmv_audio'
    os.makedirs(out, exist_ok=True)
    for name in sorted(os.listdir(src)):
        stem, ext = os.path.splitext(name)
        if ext.upper() not in ('.PSS', '.MPG') or (only and stem.upper() != only):
            continue
        video, audio = demux(open(os.path.join(src, name), 'rb').read())
        pcm_path = os.path.join(pcm_dir, stem + '.pcm')
        if not os.path.exists(pcm_path):
            print('%s: sem audio gravado (rode tools/fmv_audio_test.ps1 -Only %s)' % (stem, stem))
            continue
        v = os.path.join(out, stem + '.m2v')
        open(v, 'wb').write(video)
        # desentrelaca e passa de 512x448 para 4:3 quadrado, o formato que os editores e a IA esperam
        subprocess.run([FFMPEG, '-y', '-loglevel', 'error', '-i', v, '-f', 's16le', '-ar', '48000', '-ac', '2', '-i', pcm_path,
                        '-vf', 'yadif,scale=960:720:flags=lanczos', '-c:v', 'mpeg4', '-q:v', '2', '-pix_fmt', 'yuv420p',
                        '-c:a', 'aac', '-b:a', '256k', '-shortest', os.path.join(out, stem + '.mp4')], check=True)
        print(stem + '.mp4')
        os.remove(v)


if __name__ == '__main__':
    main()
