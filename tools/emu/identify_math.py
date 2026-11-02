"""
Identifies runtime helper functions in the original application by calling
them in the emulator with test operands and comparing the results with
reference IEEE-754 / integer operations computed in Python.

    python -P tools/emu/identify_math.py 0x08012F94 0x08013090 ...
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stm32l1_model import Stm32l1, RAM_BASE, RAM_SIZE   # noqa: E402
from unicorn.arm_const import UC_ARM_REG_R1           # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FLASH = open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read()
INIT = open(os.path.join(ROOT, "dumps", "app_data_init.bin"), "rb").read()


def f2u(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def u2f(u):
    return struct.unpack("<f", struct.pack("<I", u & 0xFFFFFFFF))[0]


def d2u(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def u2d(u):
    return struct.unpack("<d", struct.pack("<Q", u & 0xFFFFFFFFFFFFFFFF))[0]


def call(func, args):
    m = Stm32l1(FLASH, stop_on_app_entry=False)
    ram = bytearray(RAM_SIZE)
    ram[:len(INIT)] = INIT
    m.uc.mem_write(RAM_BASE, bytes(ram))
    r0 = m.call(func, args, max_blocks=50_000)
    r1 = m.uc.reg_read(UC_ARM_REG_R1)
    return m.stop_reason, r0, r1


def lo_hi(q):
    return q & 0xFFFFFFFF, q >> 32


A_F, B_F = 1.5, -2.25
A_D, B_D = 3.75, 0.5

# hypothesis name -> (args, check(r0, r1) -> bool)
HYP = {
    "fadd": ((f2u(A_F), f2u(B_F)), lambda r0, r1: r0 == f2u(A_F + B_F)),
    "fsub": ((f2u(A_F), f2u(B_F)), lambda r0, r1: r0 == f2u(A_F - B_F)),
    "frsub": ((f2u(A_F), f2u(B_F)), lambda r0, r1: r0 == f2u(B_F - A_F)),
    "fmul": ((f2u(A_F), f2u(B_F)), lambda r0, r1: r0 == f2u(A_F * B_F)),
    "fdiv": ((f2u(A_F), f2u(B_F)), lambda r0, r1: r0 == f2u(A_F / B_F)),
    "i2f": ((0xFFFFFFF9,), lambda r0, r1: r0 == f2u(-7.0)),
    "ui2f": ((7,), lambda r0, r1: r0 == f2u(7.0)),
    "f2iz": ((f2u(-7.75),), lambda r0, r1: r0 == 0xFFFFFFF9),
    "f2uiz": ((f2u(7.75),), lambda r0, r1: r0 == 7),
    "f2d": ((f2u(A_F),), lambda r0, r1: (r0 | r1 << 32) == d2u(A_F)),
    "d2f": (lo_hi(d2u(A_D)), lambda r0, r1: r0 == f2u(A_D)),
    "dadd": (lo_hi(d2u(A_D)) + lo_hi(d2u(B_D)), lambda r0, r1: (r0 | r1 << 32) == d2u(A_D + B_D)),
    "dsub": (lo_hi(d2u(A_D)) + lo_hi(d2u(B_D)), lambda r0, r1: (r0 | r1 << 32) == d2u(A_D - B_D)),
    "drsub": (lo_hi(d2u(A_D)) + lo_hi(d2u(B_D)), lambda r0, r1: (r0 | r1 << 32) == d2u(B_D - A_D)),
    "dmul": (lo_hi(d2u(A_D)) + lo_hi(d2u(B_D)), lambda r0, r1: (r0 | r1 << 32) == d2u(A_D * B_D)),
    "ddiv": (lo_hi(d2u(A_D)) + lo_hi(d2u(B_D)), lambda r0, r1: (r0 | r1 << 32) == d2u(A_D / B_D)),
    "i2d": ((0xFFFFFFF9,), lambda r0, r1: (r0 | r1 << 32) == d2u(-7.0)),
    "ui2d": ((7,), lambda r0, r1: (r0 | r1 << 32) == d2u(7.0)),
    "d2iz": (lo_hi(d2u(-7.75)), lambda r0, r1: r0 == 0xFFFFFFF9),
    "d2uiz": (lo_hi(d2u(7.75)), lambda r0, r1: r0 == 7),
    "l2d": (lo_hi(-7 & 0xFFFFFFFFFFFFFFFF), lambda r0, r1: (r0 | r1 << 32) == d2u(-7.0)),
    "ul2d": ((7, 0), lambda r0, r1: (r0 | r1 << 32) == d2u(7.0)),
    "d2lz": (lo_hi(d2u(-7.75)), lambda r0, r1: (r0 | r1 << 32) == (-7 & 0xFFFFFFFFFFFFFFFF)),
    "d2ulz": (lo_hi(d2u(7.75)), lambda r0, r1: (r0 | r1 << 32) == 7),
    "ldivmod": ((0xFFFFFF9C, 0xFFFFFFFF, 7, 0), lambda r0, r1: (r0 | r1 << 32) == (-14 & 0xFFFFFFFFFFFFFFFF)),
    "uldivmod": ((100, 0, 7, 0), lambda r0, r1: r0 == 14 and r1 == 0),
    "lmul": ((3, 0, 5, 0), lambda r0, r1: r0 == 15 and r1 == 0),
    "sqrt": (lo_hi(d2u(2.25)), lambda r0, r1: (r0 | r1 << 32) == d2u(1.5)),
    "log10": (lo_hi(d2u(1000.0)), lambda r0, r1: abs(u2d(r0 | r1 << 32) - 3.0) < 1e-12),
    "log": (lo_hi(d2u(math.e)), lambda r0, r1: abs(u2d(r0 | r1 << 32) - 1.0) < 1e-12),
    "pow": (lo_hi(d2u(2.0)) + lo_hi(d2u(10.0)), lambda r0, r1: (r0 | r1 << 32) == d2u(1024.0)),
    "exp": (lo_hi(d2u(0.0)), lambda r0, r1: (r0 | r1 << 32) == d2u(1.0)),
    "fabs_d": (lo_hi(d2u(-2.5)), lambda r0, r1: (r0 | r1 << 32) == d2u(2.5)),
}


def identify(func):
    hits = []
    for name, (args, check) in HYP.items():
        stop, r0, r1 = call(func, args)
        if stop == "returned" and check(r0, r1):
            hits.append(name)
    return hits


if __name__ == "__main__":
    for a in sys.argv[1:]:
        f = int(a, 16)
        print("%08X  %s" % (f, ", ".join(identify(f)) or "?"))
