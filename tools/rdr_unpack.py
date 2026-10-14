"""Unpacks the Red Dead Revolver (SLUS-20500) boot ELF into a plain ELF for Ghidra / PS2Recomp.

The disc ELF is only a ~1.5 KB stub at 0x00800000 plus a compressed blob. The stub (0x00800028)
is aPLib with every input byte XORed by an 8-bit key that starts at 1 and increments per byte
read (the very first literal is copied raw). It unpacks 0x00800600 -> 0x00100000, then jumps to
0x00100008. The crt0 at 0x00100008 clears BSS up to 0x007A26CC.

Usage: python tools/rdr_unpack.py game/SLUS_205.00 game/SLUS_205.00.unpacked.elf
"""
import hashlib
import struct
import sys

EXPECTED_SHA256 = '2101613bc97f3f34eb965ff1d7af5adf465d85058292f4ad7a3cb350cebc8bae'
PACKED_OFFSET = 0x1000 + 0x600   # file offset of vaddr 0x00800600
LOAD_ADDR = 0x00100000
ENTRY = 0x00100008
TEXT_END = 0x004FEA80             # last code: jr $ra at 0x004FEA68, then zero padding and data tables
BSS_END = 0x007A26CC              # from the crt0 BSS clear loop


class Src:
    def __init__(self, data, pos):
        self.d, self.p, self.key, self.tag, self.bits = data, pos, 1, 0, 0

    def byte(self):
        b = self.d[self.p] ^ self.key
        self.p += 1
        self.key = (self.key + 1) & 0xFF
        return b

    def bit(self):
        self.bits = (self.bits - 1) & 0xFF
        if self.bits == 0xFF:
            self.tag = self.byte()
            self.bits = 7
        b = self.tag >> 7
        self.tag = (self.tag << 1) & 0xFF
        return b

    def gamma(self):
        v = 1
        while True:
            v = (v << 1) + self.bit()
            if not self.bit():
                return v


def depack(data, pos):
    s = Src(data, pos)
    out = bytearray([data[pos]])  # first literal is copied raw (no XOR, key unchanged)
    s.p += 1
    lwm, r0 = 0, 0

    def copy(off, n):
        for _ in range(n):
            out.append(out[-off])

    while True:
        if not s.bit():                       # 0: literal
            out.append(s.byte())
            lwm = 0
        elif not s.bit():                     # 10: gamma-coded match
            hi = s.gamma()
            if lwm == 0 and hi == 2:          # reuse last offset
                copy(r0, s.gamma())
            else:
                hi -= 2 if lwm else 3
                off = (hi << 8) + s.byte()
                n = s.gamma()
                if off >= 32000:
                    n += 1
                if off >= 1280:
                    n += 1
                if off < 128:
                    n += 2
                copy(off, n)
                r0 = off
            lwm = 1
        elif not s.bit():                     # 110: short match / end marker
            b = s.byte()
            n, off = 2 + (b & 1), b >> 1
            if off == 0:
                break
            copy(off, n)
            r0 = off
            lwm = 1
        else:                                 # 111: single byte, 4-bit offset (0 = zero byte)
            off = 0
            for _ in range(4):
                off = (off << 1) + s.bit()
            out.append(out[-off] if off else 0)
            lwm = 0
    return bytes(out)


def build_elf(img):
    """ELF32 LE MIPS: PT_LOAD R-X for .text and PT_LOAD RW- for .data+.bss, with section headers
    so Ghidra / ps2_recomp only treat .text as code."""
    seg_off = 0x1000
    text_size = TEXT_END - LOAD_ADDR
    data_off = seg_off + text_size
    data_size = len(img) - text_size
    bss_addr = LOAD_ADDR + len(img)
    shstr = b'\0.text\0.data\0.bss\0.shstrtab\0'
    shstr_off = seg_off + len(img)
    sh_off = (shstr_off + len(shstr) + 3) & ~3

    ehdr = b'\x7fELF' + bytes([1, 1, 1, 0]) + bytes(8)
    # e_flags copied from the original ELF (0x20924001: R5900, noreorder)
    ehdr += struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, ENTRY, 52, sh_off, 0x20924001,
                        52, 32, 2, 40, 5, 4)
    phdrs = (struct.pack('<8I', 1, seg_off, LOAD_ADDR, LOAD_ADDR, text_size, text_size, 5, 0x10)
             + struct.pack('<8I', 1, data_off, TEXT_END, TEXT_END, data_size, BSS_END - TEXT_END, 6, 0x10))

    def sh(name, typ, flags, addr, off, size, align):
        return struct.pack('<10I', name, typ, flags, addr, off, size, 0, 0, align, 0)

    # flags: SHF_WRITE=1, SHF_ALLOC=2, SHF_EXECINSTR=4
    shdrs = (sh(0, 0, 0, 0, 0, 0, 0)
             + sh(1, 1, 6, LOAD_ADDR, seg_off, text_size, 16)                   # .text
             + sh(7, 1, 3, TEXT_END, data_off, data_size, 16)                   # .data (rodata+data)
             + sh(13, 8, 3, bss_addr, shstr_off, BSS_END - bss_addr, 16)        # .bss
             + sh(18, 3, 0, 0, shstr_off, len(shstr), 1))

    out = bytearray(ehdr + phdrs)
    out += bytes(seg_off - len(out))
    out += img + shstr
    out += bytes(sh_off - len(out))
    out += shdrs
    return bytes(out)


def main():
    src_path, dst_path = sys.argv[1], sys.argv[2]
    data = open(src_path, 'rb').read()
    sha = hashlib.sha256(data).hexdigest()
    if sha != EXPECTED_SHA256:
        sys.exit('unexpected input ELF (sha256 %s); this tool targets SLUS-20500 v1.03' % sha)
    img = depack(data, PACKED_OFFSET)
    elf = build_elf(img)
    open(dst_path, 'wb').write(elf)
    print('text 0x%08X..0x%08X, data ..0x%08X, bss ..0x%08X, entry 0x%08X'
          % (LOAD_ADDR, TEXT_END, LOAD_ADDR + len(img), BSS_END, ENTRY))
    print('output sha256 %s' % hashlib.sha256(elf).hexdigest())


if __name__ == '__main__':
    main()
