"""
Boots the original v2.05 image and the rebuilt standalone application
(build/firmware/app.bin at 0x08006000, behind the original bootloader) in
the STM32L1 model from reset, without timer interrupts so both runs are
deterministic, and compares every peripheral write and the final RAM.

    python -P tools/emu/compare_app.py [blocks] [tick_blocks] [--points]

--points ticks the timers at the loop heads of the start-up screen and the
main loop instead of every tick_blocks blocks: one TIM3 tick per visit and
a TIM2 tick every tenth, so both images see interrupts at the same points
of their programs; --ticks=N stops both after N visits, for a RAM
comparison at the same point.

Without timer ticks the application gets through start-up (clock, GPIO,
EEPROM settings, calibration load, LCD) and then waits on the start-up
screen, so this covers the boot path end to end.
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from stm32l1_model import Stm32l1, RAM_BASE, RAM_SIZE, reg_name   # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
APP_BASE = 0x6000
STACK = (0x20000240, 0x20001240)


# Loop heads of the start-up screen and the main loop in the original image
# (each is entered by returning from a call and by the loop's back edges).
ORIG_TICK_POINTS = (0x0800EA8E, 0x08024B6A)


def rebuilt_tick_points():
    """The same loop heads in the rebuilt image: the return address of the
    call before each loop and every back-edge target inside it."""
    elf = os.path.join(ROOT, "build", "firmware", "app.elf")
    dis = subprocess.run(["arm-none-eabi-objdump", "-d", "--no-show-raw-insn", elf],
                         capture_output=True, text=True, check=True).stdout
    points = set()
    for func, before in (("startup_screen", "hy3131_configure_mode"), ("main", "startup_screen")):
        body = dis.split("<%s>:" % func, 1)[1].split("\n\n", 1)[0]
        lines = [l.split() for l in body.splitlines() if re.match(r"\s*[0-9a-f]+:", l)]
        addrs = [int(p[0].rstrip(":"), 16) for p in lines]
        call = next(i for i, p in enumerate(lines) if "<%s>" % before in p)
        entry = addrs[call + 1]
        points.add(entry)
        for here, p in zip(addrs[call + 1:], lines[call + 1:]):
            mnemonic = p[1]
            if (mnemonic.startswith("b") and not mnemonic.startswith(("bl", "bx", "bic", "bkpt"))
                    and len(p) > 2 and re.fullmatch(r"[0-9a-f]+", p[2])):
                target = int(p[2], 16)
                if entry <= target < here and target - entry < 0x10:
                    points.add(target)
            elif mnemonic in ("cbz", "cbnz") and len(p) > 3:
                target = int(p[3], 16)
                if entry <= target < here and target - entry < 0x10:
                    points.add(target)
    return points


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    blocks = int(args[0]) if args else 3_000_000
    tick = int(args[1]) if len(args) > 1 else 0
    orig = bytearray(open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read())
    app = open(os.path.join(ROOT, "build", "firmware", "app.bin"), "rb").read()
    new = bytearray(orig[:APP_BASE]) + bytearray(b"\x00" * (len(orig) - APP_BASE))  # erased L1 flash reads 0x00
    new[APP_BASE:APP_BASE + len(app)] = app
    eeprom = open(os.path.join(ROOT, "dumps", "eeprom.bin"), "rb").read()

    use_points = "--points" in sys.argv
    runs = []
    for tag, image in (("original", orig), ("rebuilt", new)):
        points = (ORIG_TICK_POINTS if tag == "original" else rebuilt_tick_points()) if use_points else None
        if points:
            print("%-9s tick points: %s" % (tag, ", ".join("0x%08X" % a for a in sorted(points))))
        m = Stm32l1(bytes(image), stop_on_app_entry=False, eeprom_image=eeprom, tick_blocks=tick,
                    tick_points=points)
        for a in sys.argv[1:]:
            if a.startswith("--ticks="):
                m.tick_limit = int(a.split("=")[1])
        m.run(max_blocks=blocks)
        ram = bytes(m.uc.mem_read(RAM_BASE, RAM_SIZE))
        print("%-9s stop: %s, %d peripheral writes, pc 0x%08X" % (tag, m.stop_reason, len(m.trace), m.final_pc))
        runs.append((m, ram))

    (mo, ro), (mn, rn) = runs
    stop_differs = mo.stop_reason != mn.stop_reason
    if stop_differs:
        print("STOP REASONS DIFFER: original '%s', rebuilt '%s'" % (mo.stop_reason, mn.stop_reason))
    if tick or use_points:
        # With timer interrupts the two images take interrupts at different
        # points of their (differently fast) main loops, so compare the main
        # program's writes and each interrupt's writes as separate sequences.
        for ctx in sorted({c for c, _, _ in mo.trace} | {c for c, _, _ in mn.trace}):
            a = [(r, v) for c, r, v in mo.trace if c == ctx]
            b = [(r, v) for c, r, v in mn.trace if c == ctx]
            m = min(len(a), len(b))
            k = next((i for i in range(m) if a[i] != b[i]), m)
            state = "identical" if k == m else "differ at #%d" % k
            print("  %-6s %6d vs %6d writes, common prefix %s" % (ctx, len(a), len(b), state))
            if k < m:
                for i in range(max(0, k - 2), min(m, k + 3)):
                    print("     %s %-12s %08X | %-12s %08X" % ("*" if a[i] != b[i] else " ", reg_name(a[i][0]),
                                                             a[i][1], reg_name(b[i][0]), b[i][1]))
    n = min(len(mo.trace), len(mn.trace))
    k = next((i for i in range(n) if mo.trace[i] != mn.trace[i]), n)
    if k < n or len(mo.trace) != len(mn.trace):
        print("peripheral writes differ at #%d" % k)
        for i in range(max(0, k - 3), min(n, k + 4)):
            a, b = mo.trace[i], mn.trace[i]
            print("  %s %-12s %08X | %-12s %08X" % ("*" if a != b else " ", reg_name(a[1]), a[2],
                                                     reg_name(b[1]), b[2]))
    else:
        print("peripheral writes identical (%d)" % n)
    diffs = [RAM_BASE + i for i in range(RAM_SIZE)
             if ro[i] != rn[i] and not (STACK[0] <= RAM_BASE + i < STACK[1])]
    print("RAM differences outside the stack: %d" % len(diffs))
    for a in diffs[:20]:
        print("  0x%08X: %02X vs %02X" % (a, ro[a - RAM_BASE], rn[a - RAM_BASE]))
    sys.exit(1 if stop_differs or diffs or k < n or len(mo.trace) != len(mn.trace) else 0)


if __name__ == "__main__":
    main()
