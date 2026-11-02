"""
Function-level equivalence tests for the reconstructed application.

Each rebuilt function in build/firmware/hybrid.elf is compared with the
original function of the same name (address from ghidra/app_names.tsv) by
calling both from identical randomised states on the emulated STM32L1 and
comparing the return value, all of RAM outside the stack, and every
peripheral write.

    python -P tools/emu/test_app_functions.py [--only NAME] [--trials N] [--seed S]
"""
import argparse
import os
import random
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from stm32l1_model import Stm32l1, RAM_BASE, RAM_SIZE, reg_name   # noqa: E402
from globals_access import accesses, load as load_access          # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HYBRID_ELF = os.path.join(ROOT, "build", "firmware", "hybrid.elf")
HYBRID_BIN = os.path.join(ROOT, "build", "firmware", "hybrid.bin")
HYBRID_BASE = 0x08100000
STACK_LO, STACK_HI = 0x20000240, 0x20001240     # original CSTACK; contents not compared
INITIAL_SP = 0x20001240
EEPROM = open(os.path.join(ROOT, "dumps", "eeprom.bin"), "rb").read()

BYTE_POOL = [0, 0, 1, 1, 2, 3, 5, 10, 0x1E, 0x1F, 0x31, 0x32, 0x33, 0x20, 0x28, 0x40, 0x80, 0xFF]
KEY_IDR_POOL = [0xFFFF, 0xFFFF & ~(1 << 8), 0xFFFF & ~(1 << 13), 0xFFFF & ~(1 << 14),
                0xFFFF & ~(1 << 15), 0xFFFF & ~((1 << 11) | (1 << 13)), 0xFFFF & ~(1 << 3)]

SCRATCH = 0x20004000      # randomised buffer for pointer arguments (outside the stack)


def pin_pattern(port, pin):
    """IDR source whose 'pin' follows a seeded random bit sequence, indexed by
    how many times the port has been read (identical for both runs)."""
    def make(r):
        bits = r.getrandbits(256)
        return {port: lambda m: (((bits >> (m.idr_reads.get(port, 0) % 256)) & 1) << pin)
                | (0xFFFF & ~(1 << pin))}
    return make


SCRATCH_BYTES = [(SCRATCH + i, 1) for i in range(64)]


def float_bits(r):
    """Realistic float values plus edge cases (zeros, negatives, denormals, huge
    values). NaN, infinity and magnitudes near FLT_MAX are excluded: IAR's
    runtime produces negative default NaNs where libgcc produces positive
    ones, and IAR's fdiv overflows early for dividends within a factor ~1.03
    of FLT_MAX (documented deviations; real values are many orders smaller)."""
    import struct as _s
    choice = r.random()
    if choice < 0.6:
        v = r.choice([1.0, 0.25, 1.5e-3, 3.3, 12.5, 1e6, -2.0, 0.1]) * r.uniform(0.5, 2.0)
        return _s.unpack("<I", _s.pack("<f", v))[0]
    if choice < 0.75:
        return r.choice([0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x00800000])
    while True:
        b = r.getrandbits(32)
        if (b >> 23) & 0xFF <= 0xF0:
            return b


def floats_at(*addrs):
    return lambda r: {a + i: (v >> (8 * i)) & 0xFF for a in addrs for v in [float_bits(r)] for i in range(4)}

# Per-function test specs:
#   ret   - return width in bits (None for void)
#   args  - generator of argument tuples
#   idr   - generator of {port: IDR value}
#   extra - additional RAM addresses (address, width) to randomise
SPECS = {
    # ---- board I/O, HY3131 bus, RTC ----
    "delay_loop": dict(ret=None, args=lambda r: (r.randrange(0, 40),)),
    "hy3131_clock_pulse": dict(ret=None, trials=50),
    "hy3131_write_byte": dict(ret=None, args=lambda r: (r.getrandbits(8), r.choice([0, 1, 2]))),
    "hy3131_read_byte": dict(ret=8, idr=pin_pattern("F", 3)),
    "hy3131_write": dict(ret=None, args=lambda r: (SCRATCH, r.getrandbits(7), r.randrange(0, 9)),
                         extra=SCRATCH_BYTES, trials=150),
    "hy3131_read": dict(ret=None, args=lambda r: (0x200077D0, r.getrandbits(7), r.randrange(0, 0x35)),
                        idr=pin_pattern("F", 3), trials=150),
    "detect_pf8_low": dict(ret=None, idr=pin_pattern("F", 8)),
    "rtc_select": dict(ret=None, trials=30),
    "rtc_deselect": dict(ret=None, trials=30),
    "rtc_data_pin_output": dict(ret=None, trials=30),
    "rtc_data_pin_input": dict(ret=None, trials=30),
    "rtc_write_address4": dict(ret=None, args=lambda r: (r.getrandbits(8),)),
    "rtc_write_byte": dict(ret=None, args=lambda r: (r.getrandbits(8),)),
    "rtc_read_byte": dict(ret=8, idr=pin_pattern("B", 7)),
    "bin_to_bcd": dict(ret=8, args=lambda r: (r.getrandbits(8),)),
    "rtc_write_register": dict(ret=None, args=lambda r: (r.getrandbits(4), r.getrandbits(8))),
    "rtc_read_register": dict(ret=8, args=lambda r: (r.getrandbits(4),), idr=pin_pattern("B", 7)),
    "rtc_write_datetime": dict(ret=None, trials=100),
    # ---- start-up ----
    "SystemInit": dict(ret=None, trials=20),
    "clock_init": dict(ret=None, args=lambda r: (r.choice([0, 1]),), trials=40),
    "rtc_clock_init": dict(ret=None, trials=20),
    "enable_gpio_clocks": dict(ret=None, trials=20),
    "rotary_pins_config": dict(ret=None, args=lambda r: (lambda sel, fb: (sel, fb, fb))(
        r.choice([0, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, r.getrandbits(8)]), r.getrandbits(16))),
    "porte_inputs_init": dict(ret=None, trials=20),
    "adc_pins_analog": dict(ret=None, trials=20),
    "adc_init": dict(ret=None, trials=20),
    "nvic_init": dict(ret=None, trials=20),
    "tim2_init": dict(ret=None, trials=20),
    "tim3_init": dict(ret=None, trials=20),
    "front_end_select": dict(ret=None, args=lambda r: (r.choice(list(range(0x18)) + [100, r.getrandbits(8)]),)),
    "board_gpio_init": dict(ret=None, trials=20),
    "system_init": dict(ret=None, trials=20),
    "system_init@bor_set": dict(ret=None, trials=10, model=dict(obr=0x00F900AA)),
    "system_init@woke": dict(ret=None, trials=10, model=dict(pwr_csr=0x9)),
    "clock_init@no_hse": dict(ret=None, args=lambda r: (r.choice([0, 1]),), trials=20,
                              model=dict(hse_ok=False)),
    "SystemInit@no_hse": dict(ret=None, trials=10, model=dict(hse_ok=False)),
    "flash_data_eeprom_unlock": dict(ret=None, trials=20),
    "flash_ob_unlock": dict(ret=None, trials=20),
    "flash_get_status": dict(ret=8, trials=20),
    "flash_wait_for_last_operation": dict(ret=8, args=lambda r: (r.choice([0, 1, 2, 0x8000]),)),
    # ---- HY3131 control ----
    # Rows are kept within the original table blocks (4 rows in the smallest
    # tables); beyond them the original reads the code that follows.
    "hy3131_write_mode_registers": dict(ret=None, trials=600,
        args=lambda r: (r.randrange(0, 0x19), r.randrange(0, 4)),
        ram_set=lambda r: {0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4)}),
    "hy3131_set_irq_enables": dict(ret=None, trials=300, args=lambda r: (r.randrange(0, 0x19),)),
    "hy3131_restart_counter": dict(ret=None, trials=60, idr=pin_pattern("F", 3),
                                   extra=[(0x2000001C, 4)]),
    "hy3131_configure_mode": dict(ret=None, trials=80, idr=pin_pattern("F", 3),
        args=lambda r: (r.randrange(0, 0x19), r.randrange(0, 4)),
        ram_set=lambda r: {0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4)},
        with_globals_of=["hy3131_write_mode_registers", "hy3131_set_irq_enables",
                         "hy3131_restart_counter"]),
    "hy3131_service": dict(ret=None, trials=40, idr=pin_pattern("F", 3)),
    "hy3131_poll": dict(ret=None, trials=40, idr=pin_pattern("F", 3)),
    # ---- measurement: RMS ----
    "rms_read_value": dict(ret=32, trials=2000, extra=[(0x200077D9 + i, 1) for i in range(5)]),
    "rms_average": dict(ret=8, args=lambda r: (r.getrandbits(32),), trials=150, steps=12),
    "rms_post_func0": dict(ret=None, trials=10),
    "rms_post_func2": dict(ret=None, trials=10),
    "rms_post_func4": dict(ret=None, trials=10),
    "rms_post_func16": dict(ret=None, trials=10),
    "hy3131_on_rms_ready": dict(ret=None, trials=150, steps=6,
                                extra=[(0x200077D9 + i, 1) for i in range(5)],
                                with_globals_of=["rms_read_value", "rms_average"]),
    # ---- measurement: scaling ----
    "subtract_zero_offset": dict(ret=32, args=lambda r: (r.choice([0, 1, -1, 5000, -5000, r.getrandbits(32)]),),
                                 extra=[(0x20007E00, 4)]),
    "select_gain": dict(ret=32, ram_set=floats_at(0x20007E10, 0x20007E2C, 0x20007E30), trials=300),
    "apply_gain": dict(ret=32, trials=1500, ram_set=floats_at(0x20007E10, 0x20007E2C, 0x20007E30),
                       args=lambda r: (r.choice([0, 1, -1, 12345, -12345, 0x7FFFFFFF, r.getrandbits(32)]),),
                       with_globals_of=["select_gain"]),
    "average16": dict(ret=32, args=lambda r: (r.getrandbits(20) - (1 << 19),), trials=40, steps=40,
                      ram_set=lambda r: {0x20008095: r.choice([16, 16, 1, 2, 0])}),
    "gain_set_divisor": dict(ret=None, ram_set=floats_at(0x20007E20), trials=300,
                             args=lambda r: (0, float_bits(r))),
    "gain_set_50000": dict(ret=None, ram_set=floats_at(0x20007E20), args=lambda r: (0,)),
    "gain_set_30000": dict(ret=None, ram_set=floats_at(0x20007E20), args=lambda r: (0,)),
    "gain_set_10000": dict(ret=None, ram_set=floats_at(0x20007E20), args=lambda r: (0,)),
    "gain_set_6000": dict(ret=None, ram_set=floats_at(0x20007E20), args=lambda r: (0,)),
    "gain_set_ac_peak": dict(ret=None, ram_set=floats_at(0x20007E20)),
    "gain_select_for_range": dict(ret=None, trials=800, args=lambda r: (r.choice([0, 1]),),
                                  ram_set=lambda r: {**floats_at(0x20007E20)(r),
                                                     0x2000803C: r.randrange(0, 0x1A),
                                                     0x2000803D: r.randrange(0, 7)}),
    # ---- LCD driver ----
    "lcd_update": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "lcd_update_partial": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "lcd_clear_shadow": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(16)]),
    "number_to_digits": dict(ret=None, args=lambda r: (r.choice([0, 9, 10, 99999, 100000,
                                                                  r.getrandbits(17), r.getrandbits(32)]),)),
    "digits_to_text": dict(ret=None, args=lambda r: (r.randrange(0, 7),),
                           extra=[(0x20007DB0 + 4 * i, 4) for i in range(5)], trials=400),
    "lcd_set_main_dp": dict(ret=None, args=lambda r: (r.randrange(0, 7),), extra=[(0x20007790, 4)]),
    "lcd_set_sub_dp": dict(ret=None, args=lambda r: (r.randrange(0, 7),),
                           extra=[(0x20007790, 4), (0x20007794, 4)]),
    "lcd_set_pulse_duration": dict(ret=None, args=lambda r: (r.randrange(8, 20),)),
    "lcd_reapply_pulse_duration": dict(ret=None, ram_set=lambda r: {0x20000237: r.randrange(10, 18)}),
    "lcd_gpio_init": dict(ret=None, trials=20),
    "lcd_init": dict(ret=None, args=lambda r: (r.randrange(10, 18), r.choice([0, 1])), trials=60,
                     extra=[(0x20007790 + 4 * i, 4) for i in range(16)],
                     with_globals_of=["lcd_update"]),
    # ---- keys ----
    "keys_read_raw": dict(ret=None, idr=lambda r: {"G": r.choice(KEY_IDR_POOL + [r.getrandbits(16)])}),
    "rotary_read_raw": dict(ret=None, idr=lambda r: {"G": r.getrandbits(16)}),
    "key_can_long_press": dict(ret=8, args=lambda r: (r.choice(BYTE_POOL + [r.getrandbits(8)]),)),
    "key_is_single_key": dict(ret=8, args=lambda r: (r.choice(BYTE_POOL + [r.getrandbits(8)]),)),
    "key_is_update_combo": dict(ret=8, args=lambda r: (r.choice(BYTE_POOL + [r.getrandbits(8)]),)),
    "key_autorepeat": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 1, 1, 1, 1, 1]),),
                           trials=150, steps=80),
    "key_scan": dict(ret=None, idr=lambda r: {"G": r.choice(KEY_IDR_POOL)},
                     trials=100, steps=150, idr_change=0.05),
    # The two below call the original key handlers; their whole effect is compared.
    "keys_dispatch_events": dict(ret=None, trials=60),
    "keys_task": dict(ret=None, idr=lambda r: {"G": r.choice(KEY_IDR_POOL)}, trials=30,
                      steps=100, idr_change=0.05,
                      with_globals_of=["key_scan", "key_autorepeat", "keys_dispatch_events"]),
}


def load_names():
    out = {}
    for line in open(os.path.join(ROOT, "ghidra", "app_names.tsv")):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        out[f[2]] = int(f[0], 16)
    return out


def hybrid_functions():
    nm = subprocess.run(["arm-none-eabi-nm", HYBRID_ELF], capture_output=True, text=True,
                        check=True).stdout
    out = {}
    for line in nm.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "Tt" and int(p[0], 16) >= HYBRID_BASE:
            out[p[2]] = int(p[0], 16) & ~1
    return out


def baseline_ram():
    ram = bytearray(RAM_SIZE)
    init = open(os.path.join(ROOT, "dumps", "app_data_init.bin"), "rb").read()
    ram[0:len(init)] = init
    return ram


def code_constants(flash, func, size):
    """Immediate operands of compare/add/sub instructions in the original
    function, plus their neighbours: the boundary values its logic tests."""
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_MCLASS
    from capstone.arm import ARM_OP_IMM
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
    md.detail = True
    vals = set()
    for ins in md.disasm(flash[func - 0x08000000:func - 0x08000000 + size], func):
        if ins.mnemonic.split(".")[0] in ("cmp", "cmn", "adds", "subs", "add", "sub", "movs", "mov"):
            for op in ins.operands:
                if op.type == ARM_OP_IMM and 0 <= op.imm <= 0xFFFF:
                    vals |= {op.imm - 1, op.imm, op.imm + 1}
    return sorted(v for v in vals if v >= 0)


def randomise(ram, rnd, targets, pool):
    for addr, width in targets:
        off = addr - RAM_BASE
        mask = (1 << (8 * width)) - 1
        roll = rnd.random()
        if roll < 0.5 and pool:
            v = rnd.choice(pool)
        elif roll < 0.75:
            v = rnd.choice(BYTE_POOL + [500, 0xFFFF])
        else:
            v = rnd.getrandbits(8 * width)
        ram[off:off + width] = (v & mask).to_bytes(width, "little")


def new_machine(flash, hybrid, ram, **model):
    m = Stm32l1(flash, extra_code=(HYBRID_BASE, hybrid), stop_on_app_entry=False,
                eeprom_image=EEPROM, **model)
    m.uc.mem_write(RAM_BASE, bytes(ram))
    return m


def step(m, func, args, idr):
    """One call on an existing machine; RAM carries over between steps."""
    m.idr.update(idr)
    m.stop_reason = None
    m.blocks = 0
    r0 = m.call(func, args, sp=INITIAL_SP, max_blocks=200_000)
    return r0, bytes(m.uc.mem_read(RAM_BASE, RAM_SIZE))


def ram_diff(a, b):
    for i in range(RAM_SIZE):
        addr = RAM_BASE + i
        if STACK_LO <= addr < STACK_HI:
            continue
        if a[i] != b[i]:
            return addr
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only")
    ap.add_argument("--trials", type=int, default=300)
    ap.add_argument("--seed", type=int, default=121)
    args = ap.parse_args()

    flash = open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read()
    hybrid = open(HYBRID_BIN, "rb").read()
    names = load_names()
    rebuilt = hybrid_functions()
    afl, sizes, _ = load_access()

    failures = 0
    tested = 0
    for key, spec in SPECS.items():
        name = key.split("@")[0]
        if args.only and args.only not in (key, name):
            continue
        if name not in rebuilt:
            print("%-22s not in hybrid image" % name)
            continue
        orig = names[name]
        targets = {(ea, w) for _, ea, w, _ in accesses(afl, orig, sizes[orig])
                   if RAM_BASE <= ea < RAM_BASE + RAM_SIZE and w in (1, 2, 4)}
        for other in spec.get("with_globals_of", []):
            oa = names[other]
            targets |= {(ea, w) for _, ea, w, _ in accesses(afl, oa, sizes[oa])
                        if RAM_BASE <= ea < RAM_BASE + RAM_SIZE and w in (1, 2, 4)}
        targets |= set(spec.get("extra", []))
        pool = code_constants(afl, orig, sizes[orig])
        for other in spec.get("with_globals_of", []):
            pool = sorted(set(pool) | set(code_constants(afl, names[other], sizes[names[other]])))
        rnd = random.Random("%s/%d" % (name, args.seed))
        n = spec.get("trials", args.trials)
        steps = spec.get("steps", 1)
        bad = None
        for t in range(n):
            ram = baseline_ram()
            randomise(ram, rnd, sorted(targets), pool)
            for addr, val in (spec["ram_set"](rnd).items() if "ram_set" in spec else []):
                ram[addr - RAM_BASE] = val
            mo = new_machine(flash, hybrid, ram, **spec.get("model", {}))
            mn = new_machine(flash, hybrid, ram, **spec.get("model", {}))
            idr = spec["idr"](rnd) if "idr" in spec else {}
            for s in range(steps):
                a = spec["args"](rnd) if "args" in spec else ()
                if "idr" in spec and rnd.random() < spec.get("idr_change", 1.0):
                    idr = spec["idr"](rnd)
                ro, ram_o = step(mo, orig, a, idr)
                rn, ram_n = step(mn, rebuilt[name], a, idr)
                problems = compare_step(spec, mo, mn, ro, rn, ram_o, ram_n)
                if problems:
                    problems.insert(0, "step %d of %d" % (s, steps))
                    break
            if problems:
                bad = (t, a, idr, problems)
                break
        tested += 1
        if bad:
            failures += 1
            t, a, idr, problems = bad
            print("%-22s FAIL trial %d args=%s idr=%s" % (key, t, a, idr))
            for p in problems:
                print("    " + p)
        else:
            print("%-22s pass  %4d trials x %-3d steps (orig 0x%08X, %d globals, %d boundary values)"
                  % (key, n, steps, orig, len(targets), len(pool)))
    print("\n%d/%d functions equivalent" % (tested - failures, tested))
    sys.exit(1 if failures else 0)


def compare_step(spec, mo, mn, ro, rn, ram_o, ram_n):
    problems = []
    if mo.stop_reason != mn.stop_reason:
        problems.append("stop: %s vs %s" % (mo.stop_reason, mn.stop_reason))
    if spec["ret"]:
        mask = (1 << spec["ret"]) - 1
        if (ro & mask) != (rn & mask):
            problems.append("return 0x%X vs 0x%X" % (ro & mask, rn & mask))
    d = ram_diff(ram_o, ram_n)
    if d is not None:
        problems.append("RAM differs at 0x%08X: 0x%02X vs 0x%02X"
                        % (d, ram_o[d - RAM_BASE], ram_n[d - RAM_BASE]))
    if mo.trace != mn.trace:
        k = next((i for i, (x, y) in enumerate(zip(mo.trace, mn.trace)) if x != y),
                 min(len(mo.trace), len(mn.trace)))
        problems.append("peripheral writes differ at #%d (%d vs %d writes)%s"
                        % (k, len(mo.trace), len(mn.trace),
                           (": %s vs %s" % (mo.trace[k], mn.trace[k]))
                           if k < min(len(mo.trace), len(mn.trace)) else ""))
    return problems


if __name__ == "__main__":
    main()
