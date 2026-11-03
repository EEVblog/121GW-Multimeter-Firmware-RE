"""
Runs the original and the rebuilt bootloader through the same emulated
scenarios and compares how they drive the hardware.

    python -P tools/emu/test_bootloader.py [--only NAME] [--rebuilt PATH]
"""
import argparse
import os
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fat16 import build_fat16_image          # noqa: E402
from stm32l1_model import Stm32l1, reg_name, APP_BASE, FLASH_BASE   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ORIGINAL = os.path.join(ROOT, "dumps", "bootloader.bin")
REBUILT = os.path.join(ROOT, "build", "bootloader", "bootloader.bin")
APP_IMAGE = os.path.join(ROOT, "dumps", "flash.bin")   # app region = released v2.05

KEYS_IDLE = 0xFFFF
KEYS_COMBO = 0xFFFF & ~((1 << 11) | (1 << 13))
KEYS_COMBO_START = KEYS_COMBO & ~(1 << 9)
COMBO_READS = 7          # IDR reads made by the power-on key check

DUMMY_APP_SP = 0x20004000


def dummy_app():
    app = bytearray(0x200)
    struct.pack_into("<II", app, 0, DUMMY_APP_SP, APP_BASE + 0x101)
    app[0x100:0x102] = b"\xFE\xE7"
    return bytes(app)


def flash_with(bootloader, app):
    img = bytearray(0x60000)
    img[0:len(bootloader)] = bootloader
    img[0x6000:0x6000 + len(app)] = app
    return img


def v205_app():
    data = open(APP_IMAGE, "rb").read()[0x6000:]
    return data[:144616]


SCENARIOS = {
    "normal_boot": dict(keys=KEYS_IDLE, expect_stop="jump to application", expect_sp=DUMMY_APP_SP),
    "hse_fail_boot": dict(keys=KEYS_IDLE, hse_ok=False, expect_stop="jump to application",
                          expect_sp=DUMMY_APP_SP),
    # BOR_LEV=9 with the user bits of this unit (0xF0): expect USER = ~0xF0<<16 | 0xF0
    "bor_forced_off": dict(keys=KEYS_IDLE, obr=0x00F900AA, expect_stop="option-byte launch (reset)",
                           expect_ob=[0xFF0F00F0]),
    "iap_timeout": dict(keys=KEYS_COMBO, tick_blocks=400, expect_stop="jump to application",
                        expect_sp=DUMMY_APP_SP),
    "update_sdsc_v2": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)],
                           sd_files={"EEVBLOG.BIN": "v205"}, expect_stop="jump to application",
                           expect_flash="v205", expect_sp=0x20001240),
    "update_sdhc": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)], sd_hc=True,
                        sd_files={"EEVBLOG.BIN": "v205"}, expect_stop="jump to application",
                        expect_flash="v205", expect_sp=0x20001240),
    "update_sd_v1": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)], sd_v2=False,
                         sd_files={"EEVBLOG.BIN": "small"}, expect_stop="jump to application",
                         expect_flash="small"),
    "update_no_file": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)],
                           sd_files={"OTHER.TXT": "small"}, max_blocks=3_000_000,
                           expect_stop="instruction budget exhausted"),
    "update_no_card": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)],
                           sd_files={}, sd_absent=True, max_blocks=3_000_000,
                           expect_stop="instruction budget exhausted"),
    # A word that reads back wrong after programming: expect " FAIL", then boot.
    "update_verify_fail": dict(keys=[(0, KEYS_COMBO), (COMBO_READS, KEYS_COMBO_START)],
                               sd_files={"EEVBLOG.BIN": "v205"}, corrupt_flash_at=0x08007004,
                               expect_stop="jump to application", expect_sp=0x20001240),
}

# A small non-multiple-of-512 test image (valid vector table, odd tail)
SMALL_APP = struct.pack("<II", 0x20002000, APP_BASE + 0x101) + bytes(range(256)) * 5 + b"\x11\x22\x33\x44"


def payload(kind):
    return v205_app() if kind == "v205" else SMALL_APP


def run_one(bootloader, sc):
    sd_image = None
    if "sd_files" in sc:
        sd_image = build_fat16_image({n: payload(k) for n, k in sc["sd_files"].items()})
    m = Stm32l1(flash_with(bootloader, dummy_app()), keys_idr=sc["keys"],
                obr=sc.get("obr", 0x00F000AA), hse_ok=sc.get("hse_ok", True),
                sd_image=sd_image, sd_high_capacity=sc.get("sd_hc", False),
                sd_v2=sc.get("sd_v2", True), sd_absent=sc.get("sd_absent", False),
                tick_blocks=sc.get("tick_blocks", 0),
                corrupt_flash_at=sc.get("corrupt_flash_at"))
    return m.run(max_blocks=sc.get("max_blocks", 40_000_000))


# RAM buffers whose addresses reach the hardware (as DMA memory addresses).
# Original image addresses come from the analysis (docs/bootloader.md).
ORIGINAL_RAM_SYMBOLS = {"fs": (0x20000104, 0x230), "update_file": (0x20000334, 0x224),
                        "read_buffer": (0x20000558, 0x200)}
DMA_ADDRESS_REGS = {0x40026450}   # DMA2 channel 4 CMAR


def elf_ram_symbols(elf):
    out = subprocess.run(["arm-none-eabi-nm", "-S", elf], capture_output=True, text=True,
                         check=True).stdout
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[3] in ORIGINAL_RAM_SYMBOLS:
            syms[parts[3]] = (int(parts[0], 16), int(parts[1], 16))
    return syms


def symbolize(value, symbols):
    for name, (base, size) in symbols.items():
        if base <= value < base + size:
            return "%s+0x%X" % (name, value - base)
    return value


def split(trace, symbols):
    def norm(a, v):
        return (a, symbolize(v, symbols)) if a in DMA_ADDRESS_REGS else (a, v)
    main = [norm(a, v) for c, a, v in trace if c == "main"]
    irq = [(c,) + norm(a, v) for c, a, v in trace if c != "main"]
    return main, irq


def fmt(e):
    *ctx, addr, value = e
    name = addr if isinstance(addr, str) else reg_name(addr)
    val = value if isinstance(value, str) else "0x%08X" % value
    return ("%-6s " % ctx[0] if ctx else "") + "%-18s = %s" % (name, val)


def compare(label, a, b, prefix_ok):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            print("    %s differs at event %d of %d/%d:" % (label, i, len(a), len(b)))
            for j in range(max(0, i - 4), min(n, i + 4)):
                mark = ">>" if j == i else "  "
                print("     %s orig %-40s rebuilt %s" % (mark, fmt(a[j]), fmt(b[j])))
            return False
    if len(a) != len(b) and not prefix_ok:
        print("    %s length differs: original %d, rebuilt %d" % (label, len(a), len(b)))
        longer = a if len(a) > len(b) else b
        for e in longer[n:n + 6]:
            print("       extra:", fmt(e))
        return False
    return True


def check(name, sc, orig, new, new_symbols):
    ok = True
    budget = sc["expect_stop"] == "instruction budget exhausted"
    for tag, m in (("original", orig), ("rebuilt", new)):
        if m.stop_reason != sc["expect_stop"]:
            print("    %s stopped with '%s', expected '%s'" % (tag, m.stop_reason, sc["expect_stop"]))
            ok = False
        if "expect_sp" in sc and m.stop_reason == "jump to application" and m.final_sp != sc["expect_sp"]:
            print("    %s jumped with SP=0x%08X, expected 0x%08X" % (tag, m.final_sp, sc["expect_sp"]))
            ok = False
        if "expect_flash" in sc:
            data = payload(sc["expect_flash"])
            want = data[:len(data) & ~3]
            got = m.flash(APP_BASE, len(want))
            if got != want:
                bad = next(i for i in range(len(want)) if got[i] != want[i])
                print("    %s flash mismatch vs file at 0x%08X" % (tag, APP_BASE + bad))
                ok = False
        if "expect_ob" in sc and m.ob_writes != sc["expect_ob"]:
            print("    %s option-byte writes %s, expected %s"
                  % (tag, [hex(x) for x in m.ob_writes], [hex(x) for x in sc["expect_ob"]]))
            ok = False

    om, oi = split(orig.trace, ORIGINAL_RAM_SYMBOLS)
    nm, ni = split(new.trace, new_symbols)
    if budget:
        # prefix comparison is vacuous on empty traces: require activity
        for tag, m in (("original", orig), ("rebuilt", new)):
            if not m.trace or not m.lcd_frames:
                print("    %s produced an empty trace/LCD sequence in a budget run" % tag)
                ok = False
    ok &= compare("main-context writes", om, nm, budget)
    ok &= compare("interrupt writes", oi, ni, budget)

    frames_o = [("LCD", hash(f)) for f in orig.lcd_frames]
    frames_n = [("LCD", hash(f)) for f in new.lcd_frames]
    ok &= compare("LCD frames", frames_o, frames_n, budget)

    if orig.ob_writes != new.ob_writes:
        print("    option-byte writes differ: %s vs %s" % (orig.ob_writes, new.ob_writes))
        ok = False
    app_len = FLASH_BASE + 0x60000 - APP_BASE
    # also for budget runs: "app not erased when the file/card is missing"
    if orig.flash(APP_BASE, app_len) != new.flash(APP_BASE, app_len):
        print("    final application flash differs")
        ok = False
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--rebuilt", default=REBUILT)
    args = ap.parse_args()
    original = open(ORIGINAL, "rb").read()[:0x6000]
    rebuilt = open(args.rebuilt, "rb").read()
    new_symbols = elf_ram_symbols(os.path.splitext(args.rebuilt)[0] + ".elf")
    missing = set(ORIGINAL_RAM_SYMBOLS) - set(new_symbols)
    if missing:
        sys.exit("rebuilt ELF lacks symbols: %s" % ", ".join(sorted(missing)))
    results = []
    for name, sc in SCENARIOS.items():
        if args.only and name != args.only:
            continue
        t = time.time()
        orig = run_one(original, sc)
        new = run_one(rebuilt, sc)
        print("%-16s orig: %-28s %7d writes %4d frames | rebuilt: %-28s %7d writes %4d frames (%.0fs)"
              % (name, orig.stop_reason[:28], len(orig.trace), len(orig.lcd_frames),
                 new.stop_reason[:28], len(new.trace), len(new.lcd_frames), time.time() - t))
        ok = check(name, sc, orig, new, new_symbols)
        results.append((name, ok))
        print("    -> %s" % ("PASS" if ok else "FAIL"))
    print()
    print("%d/%d scenarios passed" % (sum(ok for _, ok in results), len(results)))
    sys.exit(0 if all(ok for _, ok in results) else 1)


if __name__ == "__main__":
    main()
