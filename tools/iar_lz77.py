"""
Decompressor for IAR EWARM's LZ77 data initialisers (__iar_lz77_init_single3),
reimplemented from the routine at 0x080287C0 in the 121GW application.

Token byte t:
  literal count  = (t & 3) - 1, or next_byte + 2 when (t & 3) == 0
  match length   = (t >> 4) + 2, with (t >> 4) == 15 extended by next_byte;
                   no match when (t >> 4) == 0
  match offset   = next_byte + 256 * ((t >> 2) & 3), with ((t >> 2) & 3) == 3
                   replaced by a further byte as the high part
Literals come before the match.

    python -P tools/iar_lz77.py dumps/app.bin 0x08006000 0x08029088 out.bin
"""
import struct
import sys


def decompress(src):
    out = bytearray()
    i = 0
    while i < len(src):
        t = src[i]; i += 1
        lit = t & 3
        if lit == 0:
            lit = src[i] + 3; i += 1
        length = t >> 4
        if length == 15:
            length = src[i] + 15; i += 1
        out += src[i:i + lit - 1]
        i += lit - 1
        if length == 0:
            continue
        hi = (t >> 2) & 3
        lo = src[i]; i += 1
        if hi == 3:
            hi = src[i]; i += 1
        offset = lo + (hi << 8)
        start = len(out) - offset
        for k in range(length + 2):
            out.append(out[start + k])
    return bytes(out)


def entry(image, base, entry_addr):
    """Decodes one init-table entry whose function is the LZ77 routine."""
    rd = lambda a: struct.unpack_from("<I", image, a - base)[0]
    arg = entry_addr + 4
    src = (arg + rd(arg)) & 0xFFFFFFFF
    packed = rd(arg + 4) >> 1
    dest = rd(arg + 8)
    return src, dest, decompress(image[src - base:src - base + packed])


if __name__ == "__main__":
    img = open(sys.argv[1], "rb").read()
    src, dest, data = entry(img, int(sys.argv[2], 16), int(sys.argv[3], 16))
    open(sys.argv[4], "wb").write(data)
    print("src 0x%08X -> dest 0x%08X, %d bytes" % (src, dest, len(data)))
