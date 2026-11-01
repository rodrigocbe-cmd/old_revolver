"""Duracao (s) de um .PSS (MPEG-2 PS): SCR do ultimo pack header. Uso: python tools/pss_duration.py <arquivo.pss>"""
import sys

data = open(sys.argv[1], 'rb').read()
i = data.rfind(b'\x00\x00\x01\xba')
b = data[i + 4:i + 10]
scr = (((b[0] >> 3) & 7) << 30) | (((b[0] & 3) << 28) | (b[1] << 20) | ((b[2] >> 3) << 15) | ((b[2] & 3) << 13) | (b[3] << 5) | (b[4] >> 3))
print(int(scr / 90000) + 1)
