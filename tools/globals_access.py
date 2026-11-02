"""
Lists the global (absolute-address) memory accesses of functions in the
original application, with access width, by tracking literal-pool loads into
registers through each function's disassembly.

    python -P tools/globals_access.py 0x0801688C [0x08016A64 ...]
    python -P tools/globals_access.py --summary 0x0801688C 0x08016A64
"""
import csv
import os
import struct
import sys

from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
from capstone.arm import ARM_OP_MEM, ARM_OP_REG, ARM_REG_PC

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BASE = 0x08000000
WIDTH = {"ldrb": 1, "strb": 1, "ldrsb": 1, "ldrh": 2, "strh": 2, "ldrsh": 2,
         "ldr": 4, "str": 4, "ldrd": 8, "strd": 8}


def load():
    flash = open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read()
    sizes = {}
    for name in ("app", "bootloader"):
        p = os.path.join(ROOT, "ghidra", "export", name, "functions.tsv")
        for r in csv.DictReader(open(p), delimiter="\t"):
            sizes[int(r["addr"], 16)] = int(r["size"])
    names = {}
    p = os.path.join(ROOT, "ghidra", "app_names.tsv")
    for line in open(p):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        names[int(f[0], 16)] = f[2]
    return flash, sizes, names


def accesses(flash, addr, size):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = True
    code = flash[addr - BASE:addr - BASE + size]
    consts = {}
    out = []
    for ins in md.disasm(code, addr):
        mn = ins.mnemonic.split(".")[0]
        ops = ins.operands
        if mn in WIDTH and len(ops) >= 2 and ops[-1].type == ARM_OP_MEM:
            mem = ops[-1].mem
            if mem.base == ARM_REG_PC and mn == "ldr":
                lit = ((ins.address + 4) & ~3) + mem.disp
                val = struct.unpack_from("<I", flash, lit - BASE)[0]
                consts[ops[0].reg] = val
                continue
            if mem.base in consts and mem.index == 0:
                ea = (consts[mem.base] + mem.disp) & 0xFFFFFFFF
                kind = "W" if mn.startswith("str") else "R"
                out.append((ins.address, ea, WIDTH[mn], kind))
            if mn.startswith("ldr") and ops[0].type == ARM_OP_REG:
                consts.pop(ops[0].reg, None)
            continue
        # any other instruction writing a register invalidates its constant
        try:
            _, written = ins.regs_access()
            for r in written:
                consts.pop(r, None)
        except Exception:
            consts.clear()
    return out


def main():
    args = sys.argv[1:]
    summary = "--summary" in args
    funcs = [int(a, 16) for a in args if not a.startswith("--")]
    flash, sizes, names = load()
    agg = {}
    for f in funcs:
        acc = accesses(flash, f, sizes[f])
        if not summary:
            print("== %08x %s" % (f, names.get(f, "")))
            for pc, ea, w, k in acc:
                print("   %08x %s%d %08x %s" % (pc, k, w, ea, names.get(ea, "")))
        for pc, ea, w, k in acc:
            agg.setdefault(ea, set()).add("%s%d" % (k, w))
    if summary:
        for ea in sorted(agg):
            if 0x20000000 <= ea < 0x2000C000:
                print("%08x %-10s %s" % (ea, ",".join(sorted(agg[ea])), names.get(ea, "")))


if __name__ == "__main__":
    main()
