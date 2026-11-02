"""
Finds application functions that are byte-identical (apart from branch targets
and literal-pool offsets) to already-identified bootloader functions.

Names for bootloader functions come from the reconstruction itself:
`inlines:` tags ("0x0800193E SDIO_ClockCmd"), `orig:` tags followed by a C
function definition, and the library table in tools/coverage.py.

    python -P tools/match_functions.py > docs/app_matches.tsv
"""
import csv
import hashlib
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import LIBRARY   # noqa: E402

C_FUNC = re.compile(r"^[A-Za-z_][\w \*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")


def load_functions(name, base, image):
    funcs = []
    for r in csv.DictReader(open(os.path.join(ROOT, "ghidra", "export", name, "functions.tsv")),
                            delimiter="\t"):
        a, n = int(r["addr"], 16), int(r["size"])
        funcs.append((a, r["name"], image[a - base:a - base + n]))
    return funcs


def normalise(code):
    """Masks BL/B.W targets and PC-relative literal offsets."""
    hw = [code[i] | code[i + 1] << 8 for i in range(0, len(code) - 1, 2)]
    out = []
    i = 0
    while i < len(hw):
        h = hw[i]
        if (h >> 11) in (0x1D, 0x1E, 0x1F) and i + 1 < len(hw):
            h2 = hw[i + 1]
            if (h & 0xF800) == 0xF000 and (h2 & 0xD000) in (0xD000, 0x9000, 0xC000):
                out += [0xF000, h2 & 0xD000]           # BL / B.W / BLX
            elif (h & 0xFF7F) == 0xF85F:
                out += [h, h2 & 0xF000]                 # LDR.W Rt, [PC, #imm]
            else:
                out += [h, h2]
            i += 2
            continue
        if (h & 0xF800) == 0x4800:
            out.append(h & 0xFF00)                      # LDR Rt, [PC, #imm8]
        elif (h & 0xF800) == 0xA000:
            out.append(h & 0xFF00)                      # ADR
        else:
            out.append(h)
        i += 1
    return hashlib.sha1(bytes(b for x in out for b in (x & 0xFF, x >> 8))).hexdigest()


def bootloader_names():
    names = dict(LIBRARY["bootloader"])
    src = os.path.join(ROOT, "bootloader")
    for dirpath, _, files in os.walk(src):
        for f in files:
            if not f.endswith(".c"):
                continue
            lines = open(os.path.join(dirpath, f), encoding="utf-8").read().splitlines()
            for i, line in enumerate(lines):
                for m in re.finditer(r"0x([0-9A-Fa-f]{8})\s+([A-Za-z_]\w+)", line):
                    names.setdefault(int(m.group(1), 16), m.group(2))
                m = re.search(r"orig:\s*0x([0-9A-Fa-f]{8})", line)
                if m:
                    for nxt in lines[i + 1:i + 8]:
                        fm = C_FUNC.match(nxt)
                        if fm:
                            names.setdefault(int(m.group(1), 16), fm.group(1))
                            break
    return names


def main():
    flash = open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read()
    bl = load_functions("bootloader", 0x08000000, flash)
    app = load_functions("app", 0x08000000, flash)
    names = bootloader_names()

    by_hash = {}
    for a, n, code in bl:
        if len(code) >= 8:
            by_hash.setdefault((len(code), normalise(code)), []).append(a)

    matched = 0
    print("app_addr\tsize\tbootloader_addr\tname")
    for a, n, code in app:
        hit = by_hash.get((len(code), normalise(code))) if len(code) >= 8 else None
        if hit:
            matched += 1
            label = "/".join(names.get(b, "FUN_%08x" % b) for b in hit)
            print("%08x\t%d\t%s\t%s" % (a, len(code), ",".join("%08x" % b for b in hit), label))
    print("matched %d of %d application functions" % (matched, len(app)), file=sys.stderr)


if __name__ == "__main__":
    main()
