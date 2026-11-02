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
import struct
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


def gains_at(*addrs):
    """Realistic gain values (no zeros/denormals, which can make NaN; see float_bits)."""
    import struct as _s
    return lambda r: {a + i: b for a in addrs
                      for b4 in [_s.pack("<f", r.choice([1.0, -1.0, 1.0, 1.0]) * 10 ** r.uniform(-3, 3))]
                      for i, b in enumerate(b4)}


def eeprom_words(lo, hi, width, boundary=()):
    """Randomises EEPROM words in [lo, hi): each big-endian word keeps its real
    value, becomes a boundary value or random bits."""
    def make(r):
        out = {}
        for a in range(lo, hi, width):
            roll = r.random()
            if roll < 0.35:
                continue
            if roll < 0.7 and boundary:
                v = r.choice(boundary)
            else:
                v = r.getrandbits(8 * width)
            v &= (1 << (8 * width)) - 1
            for i in range(width):
                out[a + i] = (v >> (8 * (width - 1 - i))) & 0xFF
        return out
    return make


def le32_at(addr, values):
    return lambda r: {addr + i: (v >> (8 * i)) & 0xFF for v in [r.choice(values + [r.getrandbits(32)])]
                      for i in range(4)}


def merge(*fns):
    def make(r):
        out = {}
        for f in fns:
            out.update(f(r))
        return out
    return make


def double_args(r):
    """A double argument (r0, r1): ordinary values, powers of ten, edge cases."""
    import struct as _s
    roll = r.random()
    if roll < 0.5:
        v = 10.0 ** r.uniform(-12, 12)
    elif roll < 0.7:
        v = r.choice([1.0, 10.0, 0.1, 2.0, 0.5, 0.7071067811865476, 0.7071067811865475, 1e-300, 1e300])
    elif roll < 0.85:
        u = r.choice([0, 0x8000000000000000, 0x7FF0000000000000, 0xFFF0000000000000,
                      0x7FF8000000000000, 0x0000000000000001, 0x000FFFFFFFFFFFFF, 0xBFF0000000000000])
        return (u & 0xFFFFFFFF, u >> 32)
    else:
        u = r.getrandbits(64)
        return (u & 0xFFFFFFFF, u >> 32)
    u = _s.unpack("<Q", _s.pack("<d", v))[0]
    return (u & 0xFFFFFFFF, u >> 32)


def _sd_image():
    sys.path.insert(0, os.path.dirname(__file__))
    from fat16 import build_fat16_image
    data = bytes((i * 7 + 3) & 0xFF for i in range(3000))
    return bytes(build_fat16_image({"TEST.TXT": data, "EEVBLOG.CSV": b"ID,START\r\n"},
                                   total_sectors=8192))


SD_IMAGE = _sd_image()
SD_IRQS = {0xE000E104: (1 << 13) | (1 << 21), 0xE000ED08: 0x08006000}   # SDIO, DMA2_CH4; app VTOR
SD_BUF = 0x20004000
SD_BUF_BYTES = lambda r: {SD_BUF + i: r.getrandbits(8) for i in range(1024)}
SD_INIT = [("sd_init", ())]

FS_OBJ, FIL_OBJ, DIR_OBJ, FINFO, BW = 0x20006830, 0x20006A60, 0x20004800, 0x20004840, 0x20004880
PATHS = {0x20004900: b"TEST.TXT", 0x20004920: b"NEW.CSV", 0x20004940: b"/", 0x20004960: b"EEVBLOG.CSV",
         0x20004980: b"NOFILE.TXT", 0x200049A0: b"0:test.txt", 0x200049C0: b"BAD|NAME"}
P_TEST, P_NEW, P_ROOT, P_CSV = 0x20004900, 0x20004920, 0x20004940, 0x20004960


def fs_ram(r):
    out = {a + i: c for a, txt in PATHS.items() for i, c in enumerate(txt + b"\0")}
    out.update({SD_BUF + i: r.getrandbits(8) for i in range(1024)})
    out.update({FINFO + i: r.getrandbits(8) for i in range(24)})
    return out


FS_MOUNT = SD_INIT + [("f_mount", (0, FS_OBJ))]
FS_READ = FS_MOUNT + [("f_open", (FIL_OBJ, P_TEST, 0x01))]
FS_WRITE = FS_MOUNT + [("f_open", (FIL_OBJ, P_NEW, 0x0A))]
FS_APPEND = FS_MOUNT + [("f_open", (FIL_OBJ, P_CSV, 0x03))]
FS_MODEL = dict(sd_image=SD_IMAGE)


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
    # ---- EEPROM and calibration storage ----
    "eeprom_read_byte_raw": dict(ret=8, args=lambda r: (r.randrange(0, 0x2FFC),)),
    "eeprom_read_byte": dict(ret=8, args=lambda r: (r.randrange(0, 0x2FFC),)),
    "eeprom_program_byte": dict(ret=8, args=lambda r: (r.randrange(0, 0x3000), r.getrandbits(8))),
    "eeprom_write_byte_retry": dict(ret=None, args=lambda r: (r.randrange(0, 0x3000), r.getrandbits(8))),
    "eeprom_write_byte": dict(ret=None, args=lambda r: (r.randrange(0, 0x3000), r.getrandbits(8))),
    "eeprom_write_u32_be": dict(ret=None, args=lambda r: (r.randrange(0, 0x2FFC), r.getrandbits(32))),
    "eeprom_write_u16_be": dict(ret=None, args=lambda r: (r.randrange(0, 0x2FFE), r.getrandbits(16))),
    "eeprom_set_marker_a": dict(ret=None, trials=20),
    "eeprom_set_marker_b": dict(ret=None, trials=20),
    "cal_address_for_range": dict(ret=32, args=lambda r: (r.getrandbits(32),),
                                  ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 16)}),
    "cal_clamp_e14": dict(ret=None, extra=[(0x20007E14, 4)],
                          ram_set=lambda r: {0x20007E14 + i: (v >> (8 * i)) & 0xFF
                                             for v in [r.choice([0, 2999, 3000, 3001, -3000 & 0xFFFFFFFF,
                                                                 -3001 & 0xFFFFFFFF, r.getrandbits(32)])]
                                             for i in range(4)}),
    "cal_store_dual": dict(ret=None, args=lambda r: (r.randrange(0, 3),), trials=150,
                           ram_set=lambda r: {**floats_at(0x20007E2C, 0x20007E30)(r),
                                              0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                              0x2000803D: r.randrange(0, 4)}),
    "cal_store_value": dict(ret=None, trials=600,
                            args=lambda r: (r.choice([0, 1, 2, 3, 150, 200, 5]),
                                            r.choice([0, 1, -1, 123456, r.getrandbits(32)])),
                            ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                               0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 8)},
                            with_globals_of=["cal_store_dual", "cal_clamp_e14"]),
    "calibrate_zero_and_gain": dict(ret=None, trials=60, steps=20,
        args=lambda r: (r.randrange(-200000, 200000), r.randrange(-1000, 1000)),
        ram_set=lambda r: {0x20008095: r.choice([16, 16, 1, 2]),
                           0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 7)},
        with_globals_of=["gain_select_for_range", "cal_store_value"]),
    # ---- measurement: AD1 ----
    "ad1_read_value": dict(ret=32, trials=2000, extra=[(0x200077D0 + i, 1) for i in range(3)]),
    "ad1_average": dict(ret=8, trials=80, steps=20, args=lambda r: (r.randrange(-0x800000, 0x800000),),
                        ram_set=lambda r: {0x2000803C: r.choice([0, 1, 9, 9, 17]), 0x2000803D: r.randrange(0, 8)}),
    "ad1_process_func0": dict(ret=None, trials=40),
    "hy3131_on_ad1_ready": dict(ret=None, trials=120, extra=[(0x200077D0 + i, 1) for i in range(3)],
                                ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 4)},
                                with_globals_of=["ad1_read_value", "ad1_average"]),
    # ---- front end ----
    "front_end_pd1_set": dict(ret=None, args=lambda r: (r.getrandbits(8), r.getrandbits(8)), trials=30),
    "front_end_pe15_pf6": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),)),
    "front_end_pa11": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),)),
    "front_end_ph2": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),)),
    "front_end_pa12": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),)),
    "front_end_pe12": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),)),
    "front_end_pe5": dict(ret=None, args=lambda r: (r.choice(list(range(26)) + [r.getrandbits(8)]),
                                                    r.choice([0, 0, 1, 2, r.getrandbits(8)]))),
    "front_end_configure": dict(ret=None, trials=400,
                                args=lambda r: (r.choice(list(range(26)) + [100]), r.randrange(0, 4)),
                                with_globals_of=["front_end_select", "front_end_pe15_pf6", "front_end_pe5"]),
    # ---- buzzer, measurement helpers, display state ----
    "buzzer_start": dict(ret=None, args=lambda r: (r.getrandbits(16),)),
    "buzzer_start_forced": dict(ret=None, args=lambda r: (r.getrandbits(16),)),
    "apo_reset": dict(ret=None),
    "beep": dict(ret=None, args=lambda r: (r.choice([0, 1, 2]), r.getrandbits(8), r.getrandbits(16)),
                 with_globals_of=["apo_reset", "buzzer_start"]),
    "beep_short": dict(ret=None, with_globals_of=["apo_reset", "buzzer_start"]),
    "beep_triple": dict(ret=None, with_globals_of=["apo_reset", "buzzer_start"]),
    "remove_offset": dict(ret=32, args=lambda r: (r.getrandbits(32),),
                          ram_set=lambda r: {0x2000803C: r.randrange(0, 26)}),
    "offset_stage": dict(ret=32, args=lambda r: (r.getrandbits(24),), trials=200,
                         ram_set=lambda r: {0x2000803C: r.randrange(0, 26)},
                         with_globals_of=["remove_offset"]),
    "gain_stage": dict(ret=32, args=lambda r: (r.getrandbits(20),), trials=200,
                       ram_set=lambda r: floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       with_globals_of=["apply_gain", "select_gain"]),
    "scale_by_decimal": dict(ret=32, args=lambda r: (r.randrange(0, 7), r.getrandbits(32))),
    "sel_code": dict(ret=8),
    "lcd_bar_flag_dual": dict(ret=None, args=lambda r: (r.choice([15, 24, 3, r.getrandbits(8)]), r.randrange(0, 5)),
                              extra=[(0x20007798, 4)]),
    "measure_display_reset": dict(ret=None, extra=[(0x20007790, 4)]),
    "cal_zero_stage": dict(ret=32, trials=80, steps=20, args=lambda r: (r.randrange(-500000, 500000),),
                           ram_set=lambda r: {0x20008095: r.choice([16, 16, 10, 2, 1]),
                                              0x2000803C: r.choice([9, 9, 2, 13]), 0x2000803D: r.randrange(3, 8)},
                           with_globals_of=["cal_store_value"]),
    "cal_gain_stage": dict(ret=32, trials=80, steps=20, args=lambda r: (r.randrange(-500000, 500000),),
                           ram_set=lambda r: {**floats_at(0x20007E10)(r), 0x20008095: r.choice([16, 16, 10, 2, 1]),
                                              0x20008090: r.choice([0, 0, 1]),
                                              0x2000803C: r.choice([9, 9, 2, 13]), 0x2000803D: r.randrange(3, 8)},
                           extra=[(0x20007C38, 4), (0x20007C3C, 4)],
                           with_globals_of=["cal_store_value", "gain_select_for_range"]),
    # ---- derived values ----
    "holdoff_on_low_reading": dict(ret=None, trials=400,
                                   ram_set=lambda r: {0x2000803C: r.choice([2, 4, 16, 18, 20, 3, 9]),
                                                      0x2000803D: r.randrange(0, 5)}),
    "range_threshold": dict(ret=32, ram_set=lambda r: {0x200080B6: r.randrange(0, 7)}),
    "scale_reading": dict(ret=None, args=lambda r: (r.randrange(-300000, 300000),), trials=300,
                          ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                             0x2000803C: r.randrange(0, 26)},
                          with_globals_of=["subtract_zero_offset", "apply_gain", "select_gain"]),
    "product_scaled": dict(ret=None, args=lambda r: (r.choice([0, -1, 0x7FFFFFFF, -0x80000000, r.getrandbits(32)]),
                                                     r.choice([0, 7, -3, r.getrandbits(32)])), trials=600,
                           ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 4)}),
    "vector_magnitude": dict(ret=None, trials=1500,
                             ram_set=lambda r: {a + i: (v >> (8 * i)) & 0xFF for a in (0x20007E9C, 0x20007EA0)
                                                for v in [r.choice([0, 60000, 60001, -60000 & 0xFFFFFFFF,
                                                                    0x80000000, r.randrange(0, 60001),
                                                                    r.getrandbits(32)])] for i in range(4)}),
    # ---- IAR maths ----
    "iar_log10": dict(ret=64, trials=4000, args=lambda r: double_args(r)),
    "dbm_from_voltage": dict(ret=None, trials=800,
                             ram_set=lambda r: {**{0x20007D64 + i: (v >> (8 * i)) & 0xFF
                                                   for v in [r.choice([0, 1, 7071, 123456, -5000,
                                                                       r.getrandbits(20), r.getrandbits(32)])]
                                                   for i in range(4)},
                                                0x2000803D: r.randrange(0, 4)}),
    # ---- calibration loading ----
    "eeprom_read_u32_be": dict(ret=32, args=lambda r: (r.randrange(0, 0x2FFC),),
                               eeprom=eeprom_words(0, 0x600, 4)),
    "eeprom_read_u16_be": dict(ret=32, args=lambda r: (r.randrange(0, 0x2FFE),),
                               eeprom=eeprom_words(0, 0x600, 2)),
    "eeprom_read_s16_be": dict(ret=32, args=lambda r: (r.randrange(0, 0x5FE),),
                               eeprom=eeprom_words(0, 0x600, 2, [0x7FFF, 0x8000, 0xFFFF, 0])),
    "cal_trim_address": dict(ret=32, args=lambda r: (r.randrange(0, 26), r.getrandbits(8))),
    "cal_extra_address": dict(ret=32, args=lambda r: (r.choice(list(range(15, 23)) + [r.getrandbits(32)]),)),
    "cal_load_extra": dict(ret=None, trials=600,
                           args=lambda r: (r.choice(list(range(15, 23)) + [r.getrandbits(8)]), r.choice([0, 0, 1, 9])),
                           eeprom=lambda r: {0x4D0 + 8 * k + i: (r.choice([0xA5, 0xA5, r.getrandbits(8)]) if i == 0
                                                                 else r.getrandbits(8))
                                             for k in range(6) for i in range(5)}),
    "cal_load_dual": dict(ret=None, trials=400,
                          ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                             0x2000803D: r.randrange(0, 4)},
                          eeprom=eeprom_words(0x100, 0x500, 4)),
    "cal_trim_clamp": dict(ret=None, args=lambda r: (r.randrange(0, 6),),
                           ram_set=merge(*[le32_at(0x20007E4C + 4 * i, [0, 10000, 10001, 20000, 20001, -10000, -10001, -20000, -20001]) for i in range(4)])),
    "cal_load_trims": dict(ret=None, trials=600,
                           ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)},
                           eeprom=eeprom_words(0, 0x500, 2, [10000, 10001, 20000, 20001, 0x10000 - 10000,
                                                             0x10000 - 10001, 0x10000 - 20000, 0x10000 - 20001,
                                                             0x7FFF, 0x8000])),
    "cal_store_trims": dict(ret=None, trials=400,
                            args=lambda r: (r.randrange(0, 26), r.randrange(0, 8), r.choice([0, 1, 2, 3, 4, 5, 100])),
                            ram_set=merge(*[le32_at(0x20007E4C + 4 * i, [0, 10000, 10001, 20000, 20001, -10000, -10001, -20000, -20001]) for i in range(4)])),
    "cal_load": dict(ret=None, trials=1500,
                     args=lambda r: (r.choice(list(range(26)) + [200, 200, 6, 6, 12, 12, 2, 2, 0, 9, 10]),
                                     r.randrange(0, 8)),
                     ram_set=lambda r: {0x2000803C: r.choice(list(range(26)) + [12, 12]),
                                        0x2000803D: r.choice([4, 4, 6, 6] + list(range(8))),
                                        0x2000808A: r.choice([0, 0, 1]), 0x200080B7: r.choice([0, 1, 1, 2]),
                                        0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                                        0x200080BD: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])},
                     eeprom=merge(eeprom_words(0x30, 0x500, 4, [40000000, 39999999, 60000000, 60000001,
                                                                1000000, 0]),
                                  eeprom_words(0x2E0, 0x2E2, 2, [1000, 1001, 0x10000 - 1000, 0x10000 - 1001]))),
    # ---- filters, temperature, staging ----
    "smooth3": dict(ret=32, trials=200, steps=25,
                    args=lambda r: (r.choice([r.randrange(-30, 30) + 1000, r.randrange(-15, 15) + 1000,
                                              r.getrandbits(32)]),),
                    ram_set=merge(le32_at(0x20007E78, [0, 1, 2, 3]), le32_at(0x20007E70, [1000, 990, 1011]))),
    "moving_average": dict(ret=32, trials=200, steps=30,
                           args=lambda r: (r.choice([r.randrange(-25, 25) + 500, r.getrandbits(32),
                                                     r.choice([0x7FFFFFFF, -0x80000000])]), 20, 10),
                           ram_set=lambda r: {0x20008084: r.randrange(0, 11)},
                           extra=[(0x200070A8 + 4 * i, 4) for i in range(10)]),
    "moving_average@params": dict(ret=32, trials=200, steps=30,
                                  args=lambda r: (r.randrange(-100, 100), r.choice([0, 5, 0x7FFFFFFF, -1]),
                                                  r.randrange(1, 11)),
                                  ram_set=lambda r: {0x20008084: r.randrange(0, 11)},
                           extra=[(0x200070A8 + 4 * i, 4) for i in range(10)]),
    "thermocouple_to_celsius": dict(ret=None, trials=3000,
                                    args=lambda r: (r.choice([r.randrange(-7000, 56000), -5891, -5892, 54138,
                                                              54139, 0, -6035, r.getrandbits(32)]),)),
    "celsius_to_fahrenheit": dict(ret=32, trials=3000,
                                  args=lambda r: (r.choice([r.randrange(-30000, 140000), r.randrange(-32768, 32768), r.randrange(2300000, 4700000),
                                                            -1, 0, 0x8000, 0x7FFF, r.getrandbits(32)]),)),
    "derived_from_average": dict(ret=None, trials=400,
                                 ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                    0x2000803C: r.randrange(0, 26)},
                                 with_globals_of=["offset_stage", "gain_stage", "remove_offset",
                                                  "apply_gain", "select_gain"]),
    "scaled_from_ad1": dict(ret=None, trials=300,
                            ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                               0x2000803C: r.randrange(0, 26)},
                            with_globals_of=["remove_offset", "apply_gain", "select_gain"]),
    "threshold_check": dict(ret=None, trials=800,
                            args=lambda r: (r.choice([2499, 2500, 2501, 2999, 3000, 3001, 3499, 3500, 3501, 29499,
                                                      29500, 29501, 30000, 30500, 30501, r.getrandbits(32)]),),
                            ram_set=lambda r: {0x200080B6: r.randrange(0, 6), 0x200080B5: r.choice([0, 1, 2])},
                            with_globals_of=["range_threshold", "buzzer_start_forced"]),
    "func12_set_interval": dict(ret=None, trials=300,
                                ram_set=lambda r: {0x2000803C: r.choice([12, 12, 12, 3]),
                                                   0x2000803D: r.randrange(0, 8)}),
    # ---- function/range change ----
    "main_dp_for_range": dict(ret=8, trials=6000,
                             args=lambda r: (r.choice(list(range(30)) + [r.getrandbits(8)]),
                                             r.choice(list(range(10)) + [r.getrandbits(8)])),
                             ram_set=lambda r: {0x200080B3: r.choice([0, 1, 2])}),
    "main_dp_update": dict(ret=None, trials=300,
                          ram_set=lambda r: {0x2000803C: r.randrange(0, 27), 0x2000803D: r.randrange(0, 9)},
                          with_globals_of=["lcd_set_main_dp", "lcd_update", "main_dp_for_range"],
                          extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "mode_238_select": dict(ret=None, trials=1500,
                            ram_set=lambda r: {0x2000803C: r.randrange(0, 27), 0x2000803D: r.randrange(0, 9),
                                               0x200080A5: r.choice([0, 1]), 0x200080C4: r.choice([0, 1]),
                                               0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                                               0x200080B7: r.choice([0, 1])}),
    "range_configure": dict(ret=None, trials=300, args=lambda r: (r.choice([0, 1, 1, 2]),),
                            ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])},
                            eeprom=eeprom_words(0x30, 0x500, 4, [40000000, 60000001, 1000000, 0]),
                            with_globals_of=["hy3131_configure_mode", "cal_load", "cal_load_extra",
                                             "cal_load_trims", "func12_set_interval"]),
    "range_select": dict(ret=None, trials=200, args=lambda r: (r.choice([0, 1, 1]),),
                         with_globals_of=["hy3131_configure_mode", "cal_load", "func12_set_interval"],
                         ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])}),
    "range_change": dict(ret=None, trials=200, ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])},
                         with_globals_of=["measure_display_reset", "lcd_bar_flag_dual", "main_dp_update"],
                         extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "cal_set_trim": dict(ret=None, trials=600,
                         args=lambda r: (r.choice([0, 1, 2, 3, 4, 5, 100]),
                                         r.choice([5000, 50000, 600, 6000, r.randrange(-70000, 70000),
                                                   r.getrandbits(32)])),
                         ram_set=lambda r: {0x2000803C: r.choice([2, 2, 16, 18, 0]), 0x2000803D: r.randrange(0, 5)},
                         with_globals_of=["cal_store_trims"]),
    "derived_from_average_positive": dict(ret=None, trials=300,
                                          ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                             0x2000803C: r.randrange(0, 26)},
                                          with_globals_of=["derived_from_average", "offset_stage", "gain_stage",
                                                           "remove_offset", "apply_gain", "select_gain"]),
    "moving_average_b": dict(ret=32, trials=300, steps=30,
                             args=lambda r: (r.choice([r.randrange(-25, 25) + 500, r.getrandbits(32)]),
                                             r.choice([0, 2, 5, 10, 20, 100, -1, 0x7FFFFFFF]),
                                             r.choice([0, 1, 2, 5, 10, 10, 20])),
                             ram_set=lambda r: {0x20008085: r.randrange(0, 21)},
                             extra=[(0x20007238 + 4 * i, 4) for i in range(20)]),
    "lcd_request_update": dict(ret=None, trials=20),
    "lcd_flag_set": dict(ret=8, args=lambda r: (r.choice([1, 2, 4, 8, 0x20, 0x3F, r.getrandbits(32)]),)),
    "ad1_process_func3": dict(ret=None, trials=150, steps=10,
                              ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                 0x2000803C: 3, 0x20008081: r.choice([0, 1, 1]),
                                                 0x20008085: r.randrange(0, 11)},
                              with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b",
                                               "offset_stage", "gain_stage", "remove_offset", "apply_gain"],
                              extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func11": dict(ret=None, trials=300, steps=10,
                               ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                  0x2000803C: 11, 0x20008081: r.choice([0, 1, 1]),
                                                  0x20008085: r.randrange(0, 11), 0x2000808A: r.choice([0, 0, 1]),
                                                  0x20007FDF: r.choice([0, 1, 1, 3]),
                                                  0x200080B3: r.choice([0, 1, 1, 3]),
                                                  0x200080B4: r.choice([0, 0, 100])},
                               with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b",
                                                "offset_stage", "gain_stage", "remove_offset", "apply_gain",
                                                "buzzer_start_forced", "beep_triple"],
                               extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "reconfigure_borrow_f2": dict(ret=None, trials=200, args=lambda r: (r.choice([0, 1, 2]),),
                                  ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])},
                                  with_globals_of=["hy3131_configure_mode", "cal_load", "front_end_configure",
                                                   "mode_238_select"]),
    "reconfigure_borrow_f1": dict(ret=None, trials=200, args=lambda r: (r.choice([0, 1, 2]),),
                                  ram_set=lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])},
                                  with_globals_of=["hy3131_configure_mode", "cal_load", "front_end_configure",
                                                   "mode_238_select"]),
    "cal_trim_requests": dict(ret=None, trials=600,
                              ram_set=lambda r: {0x2000803C: r.choice([2, 4, 16, 18, 20, 3, 0]),
                                                 0x2000803D: r.randrange(0, 5), 0x200080A5: r.choice([0, 0, 1]),
                                                 0x2000808A: r.choice([0, 1, 1]),
                                                 **{0x2000809C + i: r.choice([0, 1, 7]) for i in range(4)}},
                              with_globals_of=["cal_set_trim", "cal_store_trims", "beep_short"]),
    "f9r5_correction": dict(ret=32, trials=4000,
                            args=lambda r: (r.choice([r.randrange(-2000, 52000), r.randrange(0, 60) * 1000,
                                                      r.randrange(0, 60) * 1000 - 1, r.getrandbits(32)]),)),
    "f9r6_correction": dict(ret=32, trials=4000,
                            args=lambda r: (r.choice([r.randrange(-2000, 58000), r.randrange(0, 60) * 1000,
                                                      r.randrange(0, 60) * 1000 + 1, 0, 0, 56000, 55999,
                                                      r.getrandbits(32)]),
                                            r.choice([0, 100, -100, 3000, -3000, r.randrange(-3000, 3001),
                                                      r.getrandbits(32)]))),
    "ad1_process_func9": dict(ret=None, trials=300, steps=12, ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([9, 9, 10]), 0x2000803D: r.choice([5, 6, 5, 6, 0, 1, 2, 3, 4, 7]),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x2000808A: r.choice([0, 0, 1]), 0x20007FE0: r.choice([0, 0, 1])},
                              with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b",
                                               "offset_stage", "gain_stage", "remove_offset", "apply_gain",
                                               "f9r6_correction"],
                              extra=[(0x20007238 + 4 * i, 4) for i in range(20)] + [(0x20007E14, 4)]),
    "ad1_process_func10": dict(ret=None, trials=200, steps=8, ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([9, 9, 10]), 0x2000803D: r.choice([5, 6, 5, 6, 0, 1, 2, 3, 4, 7]),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x2000808A: r.choice([0, 0, 1]), 0x20007FE0: r.choice([0, 0, 1])},
                               with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b",
                                                "offset_stage", "gain_stage", "remove_offset", "apply_gain",
                                                "threshold_check", "range_threshold", "buzzer_start_forced"],
                               extra=[(0x20007238 + 4 * i, 4) for i in range(20)] + [(0x20007E14, 4)]),
    "ad1_process_func5": dict(ret=None, trials=400, steps=6,
                              ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                 0x2000803C: 5, 0x20008081: r.choice([0, 1, 1, 1]),
                                                 0x20008085: r.randrange(0, 11), 0x200080AB: r.choice([0, 1]),
                                                 **{0x20007E7C + i: (v >> (8 * i)) & 0xFF
                                                    for v in [r.choice([0, 1000, -500 & 0xFFFFFFFF,
                                                                        r.randrange(0, 3000)])]
                                                    for i in range(4)}},
                              with_globals_of=["ad1_process_func3", "derived_from_average", "scaled_from_ad1",
                                               "moving_average_b", "offset_stage", "gain_stage",
                                               "thermocouple_to_celsius", "celsius_to_fahrenheit"],
                              extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "acdc_capture": dict(ret=None, trials=300, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([2, 2, 2, 1, 0, 16, 17, 3]), 0x200080BA: r.choice([0, 1, 1]),
                       0x200080BD: r.choice([0, 1]), 0x200080BE: r.randrange(0, 7),
                       0x20008081: r.choice([0, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x20000239: r.choice([5, 5, 0, 3]), 0x20008037: r.choice([11, 11, 0, 3])},
                         with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b", "offset_stage",
                                  "gain_stage", "remove_offset", "apply_gain", "vector_magnitude",
                                  "hy3131_configure_mode", "cal_load", "dbm_from_voltage"], extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "acdc_settle": dict(ret=None, trials=300, steps=8, args=lambda r: (r.choice([0, 1, 2]),), ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([2, 2, 2, 1, 0, 16, 17, 3]), 0x200080BA: r.choice([0, 1, 1]),
                       0x200080BD: r.choice([0, 1]), 0x200080BE: r.randrange(0, 7),
                       0x20008081: r.choice([0, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x20000239: r.choice([5, 5, 0, 3]), 0x20008037: r.choice([11, 11, 0, 3])},
                        with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b", "offset_stage",
                                  "gain_stage", "remove_offset", "apply_gain", "vector_magnitude",
                                  "hy3131_configure_mode", "cal_load", "dbm_from_voltage"], extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "func2_reading_done": dict(ret=None, trials=300, steps=8, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([2, 2, 2, 1, 0, 16, 17, 3]), 0x200080BA: r.choice([0, 1, 1]),
                       0x200080BD: r.choice([0, 1]), 0x200080BE: r.randrange(0, 7),
                       0x20008081: r.choice([0, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x20000239: r.choice([5, 5, 0, 3]), 0x20008037: r.choice([11, 11, 0, 3])},
                               with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b", "offset_stage",
                                  "gain_stage", "remove_offset", "apply_gain", "vector_magnitude",
                                  "hy3131_configure_mode", "cal_load", "dbm_from_voltage"], extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func1": dict(ret=None, trials=300, steps=8, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([2, 2, 2, 1, 0, 16, 17, 3]), 0x200080BA: r.choice([0, 1, 1]),
                       0x200080BD: r.choice([0, 1]), 0x200080BE: r.randrange(0, 7),
                       0x20008081: r.choice([0, 1, 1]), 0x20008085: r.randrange(0, 21),
                       0x20000239: r.choice([5, 5, 0, 3]), 0x20008037: r.choice([11, 11, 0, 3])},
                              with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b", "offset_stage",
                                  "gain_stage", "remove_offset", "apply_gain", "vector_magnitude",
                                  "hy3131_configure_mode", "cal_load", "dbm_from_voltage"], extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "sign_offset": dict(ret=32, trials=4000,
                        args=lambda r: (r.choice([r.randrange(-50000, 50000), 0, -1, r.getrandbits(32)]),
                                        r.choice([2500, 2501, -2500, -2501, 0, r.randrange(-60000, 60000),
                                                  r.getrandbits(32)])),
                        ram_set=lambda r: {0x2000803C: r.choice([22, 23, 24, 22, 23, 24, 21, 25]),
                                           0x2000803D: r.randrange(0, 6), 0x2000808A: r.choice([0, 0, 0, 1]),
                                           0x20007FDF: r.choice([0, 0, 0, 1]), 0x20007FE0: r.choice([1, 1, 1, 0, 3])}),
    "ad1_process_func17": dict(ret=None, trials=300, steps=6,
                               ram_set=lambda r: {**floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                  0x2000803C: r.choice([17, 19, 21, 22, 23, 24, 16]),
                                                  0x2000803D: r.randrange(0, 5), 0x20008081: r.choice([0, 1, 1]),
                                                  0x20008085: r.randrange(0, 11), 0x2000808A: r.choice([0, 0, 1]),
                                                  0x20007FDF: r.choice([0, 0, 1]), 0x20007FE0: r.choice([1, 1, 0]),
                                                  0x20008037: r.choice([10, 10, 11])},
                               with_globals_of=["derived_from_average", "scaled_from_ad1", "moving_average_b",
                                                "offset_stage", "gain_stage", "sign_offset"],
                               extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func17_va": dict(ret=None, trials=200, steps=14,
                                  ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r),
                                                     **floats_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                     0x2000803C: r.choice([17, 19, 21]),
                                                     0x20008081: r.choice([0, 1, 1, 1]),
                                                     0x20008085: r.randrange(0, 11),
                                                     0x2000808A: r.choice([0, 0, 0, 1]),
                                                     0x200080A5: r.choice([1, 1, 1, 0]),
                                                     0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 5),
                                                     0x20007E78: r.randrange(0, 4),
                                                     0x20008037: r.choice([10, 11, 11])},
                                  with_globals_of=["ad1_process_func17", "ad1_process_func1", "smooth3",
                                                   "reconfigure_borrow_f1", "derived_from_average",
                                                   "scaled_from_ad1", "moving_average_b"],
                                  extra=[(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ac_response_correct": dict(ret=32, trials=5000,
                                args=lambda r: (r.choice([r.randrange(0, 100000), r.randrange(100000, 500001),
                                                          r.randrange(500001, 2000000), 100000, 99999, 500000,
                                                          500001, r.getrandbits(32)]),),
                                ram_set=lambda r: {0x2000803C: r.choice([2, 4, 2, 4, 3]),
                                                   0x200080B9: r.choice([1, 1, 1, 0]),
                                                   0x2000803E: r.choice([3, 3, 3, 2, 0, 4]),
                                                   **{0x20007D84 + i: (v >> (8 * i)) & 0xFF
                                                      for v in [r.choice([r.randrange(0, 7000), 19, 20, 21, 6000,
                                                                          6001, r.randrange(0, 70000),
                                                                          r.getrandbits(32)])]
                                                      for i in range(4)}}),
    "f2r0_freq_offset": dict(ret=32, trials=5000,
                             args=lambda r: (r.choice([r.randrange(-1000, 60000), 5000, 5001, 56999, 57000,
                                                       r.getrandbits(32)]),
                                             r.choice([r.randrange(0, 200000), 50, 51, 100000, 100001,
                                                       r.getrandbits(32)]),
                                             r.choice([3, 3, 3, 2, 1, 0, 4]))),
    "trim_correction_ac": dict(ret=32, trials=5000,
                               args=lambda r: (r.choice([r.randrange(-1000, 56000), 5000, 10000, 49999]),
                                               r.choice([r.randrange(-20000, 20001), 0, r.getrandbits(32)]),
                                               r.choice([r.randrange(-20000, 20001), 0]),
                                               r.choice([r.randrange(-10000, 10001), 0]),
                                               r.choice([r.randrange(-10000, 10001), 0]),
                                               r.choice([2, 3, 2, 3, 1]),
                                               r.choice([r.randrange(51, 120000), 20000, 20001, 100000, 100001,
                                                         51, 400, 1000]),
                                               r.choice([0, 1, 7]))),
    "trim_correction": dict(ret=32, trials=6000,
                            args=lambda r: (r.choice([r.randrange(-1000, 62000), 5000, 5001, 6599, 6600, 54999,
                                                      55000, 59999, 60000, r.getrandbits(32)]),
                                            r.choice([r.randrange(-20000, 20001), 0]),
                                            r.choice([r.randrange(-20000, 20001), 0]),
                                            r.choice([r.randrange(-10000, 10001), 0]),
                                            r.choice([r.randrange(-10000, 10001), 0]),
                                            r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]),
                                            r.choice([r.randrange(0, 150000), 50, 51, 10000, 10001,
                                                      100000, r.getrandbits(32)]),
                                            r.choice([3, 3, 3, 2, 0]), r.choice([0, 1]))),
    "trim_correction_for": dict(ret=32, trials=1500,
                                args=lambda r: (r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8]),
                                                r.choice([r.randrange(-1000, 60000), 5000, 5001])),
                                ram_set=lambda r: {0x2000803C: r.choice([2, 2, 4, 16, 18, 20, 0, 0, 3]),
                                                   0x200080BA: r.choice([0, 1]), 0x20008098: r.choice([0, 1]),
                                                   0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                                                   0x200080A1: r.choice([3, 2]), 0x20008087: r.choice([0, 1]),
                                                   **{a + i: (v >> (8 * i)) & 0xFF
                                                      for a in (0x20007D84, 0x20007E60, 0x20007E64)
                                                      for v in [r.randrange(0, 120000)] for i in range(4)},
                                                   **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                                                      for k in range(4)
                                                      for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                                                      for i in range(4)}}),
    "trim_correct_reading": dict(ret=32, trials=2000,
                                 args=lambda r: (r.choice([2, 2, 4, 16, 18, 20, 0, 3, 1]),
                                                 r.choice([r.randrange(-1000, 60000), 5000, 5001])),
                                 ram_set=lambda r: {0x2000803D: r.randrange(0, 4), 0x200080A5: r.choice([0, 0, 1]),
                                                    0x200080B9: r.choice([0, 0, 1]), 0x2000808A: r.choice([0, 0, 1]),
                                                    0x200080BA: r.choice([0, 1]), 0x20008098: r.choice([0, 1]),
                                                    0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                                                    0x200080A1: r.choice([3, 2]), 0x20008087: r.choice([0, 1, 1]),
                                                    **{a + i: (v >> (8 * i)) & 0xFF
                                                       for a in (0x20007D84, 0x20007E60, 0x20007E64)
                                                       for v in [r.randrange(0, 120000)] for i in range(4)},
                                                    **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                                                       for k in range(4)
                                                       for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                                                       for i in range(4)}}),
    "ad1_process_dual_ac": dict(ret=None, trials=200, steps=6, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([13, 14, 15]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage"],
                extra=[(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func4": dict(ret=None, trials=400, steps=6, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([4, 4, 4, 13, 2]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage"],
                extra=[(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func2": dict(ret=None, trials=500, steps=6, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([2, 2, 2, 4, 13, 16, 0, 0]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage"],
                extra=[(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func16": dict(ret=None, trials=500, steps=6, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([16, 18, 20, 16, 13, 17]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage"],
                extra=[(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "ad1_process_func16_va": dict(ret=None, trials=200, steps=16, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([16, 18, 20]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage", "ad1_process_func16", "ad1_process_func2", "smooth3", "reconfigure_borrow_f2"],
                extra=[(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "hy3131_on_ad1_ready@deep": dict(ret=None, trials=600, steps=8, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage", "ad1_read_value", "ad1_average", "thermocouple_to_celsius", "threshold_check", "sign_offset", "smooth3", "f9r6_correction"],
                extra=[(0x200077D0 + i, 1) for i in range(3)] + [(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "dual_grid_corners": dict(ret=None, trials=4000,
                              args=lambda r: (r.randrange(1, 9), r.randrange(1, 8), r.choice([0, 1, 2, 3])),
                              ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 12]),
                                                 0x2000803D: r.randrange(0, 6), 0x20007FDF: r.choice([0, 1, 3])},
                              extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "dual_correction": dict(ret=None, trials=5000,
                            ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 13, 15, 24]),
                                               0x2000803D: r.randrange(0, 5), 0x20007FDF: r.choice([0, 1]),
                                               0x2000808A: r.choice([0, 0, 0, 1]), 0x200080CB: r.choice([0, 0, 0, 1]),
                                               0x200080CC: r.choice([0, 1, 2, 1, 2, 3]),
                                               **{a + i: (v >> (8 * i)) & 0xFF
                                                  for a in (0x20007D7C, 0x20007D80)
                                                  for v in [r.choice([r.randrange(-70000, 70000), r.randrange(0, 60001),
                                                                      0, 10, 11, 1000, 1001, 5000, 5001, 10000, 10001,
                                                                      30000, 30001, 50000, 50001, 60000, 60001])
                                                            & 0xFFFFFFFF]
                                                  for i in range(4)}},
                            extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "autorange_primary": dict(ret=None, trials=600,
                              args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3199, 3200,
                                                        -55001, r.getrandbits(32)]),),
                              ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                 0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                 0x200080C7: r.choice([0, 1]), 0x20008037: r.choice([10, 11])},
                              with_globals_of=["range_change", "sel_code"],
                              extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "autorange_secondary": dict(ret=None, trials=600,
                                args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3999, 4000,
                                                          11000, 11001, 399, 400, -55001, r.getrandbits(32)]),),
                                ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                   0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                   0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                   0x200080C7: r.choice([0, 1, 1, 2]), 0x20008037: r.choice([10, 11])},
                                with_globals_of=["range_change", "sel_code"],
                                extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "ad1_process_func22": dict(ret=None, trials=300, steps=24, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([22, 23, 24]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8), 0x200080CC: r.choice([0, 1, 2]), 0x200080CB: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage", "ad1_read_value", "ad1_average", "thermocouple_to_celsius", "threshold_check", "sign_offset", "smooth3", "f9r6_correction", "dual_correction", "dual_grid_corners", "autorange_primary", "autorange_secondary", "range_select", "product_scaled", "ad1_process_func1", "ad1_process_func2", "ad1_process_func16", "ad1_process_func17"],
                extra=[(0x200077D0 + i, 1) for i in range(3)] + [(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "dual_grid_corners": dict(ret=None, trials=4000,
                              args=lambda r: (r.randrange(1, 9), r.randrange(1, 8), r.choice([0, 1, 2, 3])),
                              ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 12]),
                                                 0x2000803D: r.randrange(0, 6), 0x20007FDF: r.choice([0, 1, 3])},
                              extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "dual_correction": dict(ret=None, trials=5000,
                            ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 13, 15, 24]),
                                               0x2000803D: r.randrange(0, 5), 0x20007FDF: r.choice([0, 1]),
                                               0x2000808A: r.choice([0, 0, 0, 1]), 0x200080CB: r.choice([0, 0, 0, 1]),
                                               0x200080CC: r.choice([0, 1, 2, 1, 2, 3]),
                                               **{a + i: (v >> (8 * i)) & 0xFF
                                                  for a in (0x20007D7C, 0x20007D80)
                                                  for v in [r.choice([r.randrange(-70000, 70000), r.randrange(0, 60001),
                                                                      0, 10, 11, 1000, 1001, 5000, 5001, 10000, 10001,
                                                                      30000, 30001, 50000, 50001, 60000, 60001])
                                                            & 0xFFFFFFFF]
                                                  for i in range(4)}},
                            extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "autorange_primary": dict(ret=None, trials=600,
                              args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3199, 3200,
                                                        -55001, r.getrandbits(32)]),),
                              ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                 0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                 0x200080C7: r.choice([0, 1]), 0x20008037: r.choice([10, 11])},
                              with_globals_of=["range_change", "sel_code"],
                              extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "autorange_secondary": dict(ret=None, trials=600,
                                args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3999, 4000,
                                                          11000, 11001, 399, 400, -55001, r.getrandbits(32)]),),
                                ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                   0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                   0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                   0x200080C7: r.choice([0, 1, 1, 2]), 0x20008037: r.choice([10, 11])},
                                with_globals_of=["range_change", "sel_code"],
                                extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "ad1_process_func13": dict(ret=None, trials=300, steps=24, ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                       0x2000803C: r.choice([13, 14, 15]), 0x2000803D: r.randrange(0, 4),
                       0x20008081: r.choice([0, 1, 1, 1]), 0x200080B7: r.choice([0, 0, 0, 1]),
                       0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                       0x200080A5: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 0, 1]),
                       0x2000808A: r.choice([0, 0, 0, 1]), 0x20008087: r.choice([0, 1]),
                       0x200080BA: r.choice([0, 0, 1]), 0x200080BD: r.choice([0, 1]),
                       0x20008084: r.randrange(0, 11), 0x20008085: r.randrange(0, 11),
                       0x2000803E: r.choice([3, 3, 2]), 0x200080A0: r.choice([3, 2]),
                       0x20000239: r.choice([5, 5, 0]), 0x20008037: r.choice([10, 11, 0]),
                       0x200080C4: r.choice([0, 1]), 0x200080C5: r.randrange(0, 8), 0x200080CC: r.choice([0, 1, 2]), 0x200080CB: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]),
                       0x20007E78: r.randrange(0, 4), 0x2000808F: r.choice([0, 1]),
                       0x20008095: r.choice([16, 16, 1, 2]),
                       **{a + i: (v >> (8 * i)) & 0xFF
                          for a in (0x20007D84, 0x20007E60, 0x20007E64)
                          for v in [r.randrange(0, 120000)] for i in range(4)},
                       **{0x20007E4C + 4 * k + i: (v >> (8 * i)) & 0xFF
                          for k in range(4)
                          for v in [r.randrange(-20000, 20001) & 0xFFFFFFFF]
                          for i in range(4)}},
                with_globals_of=["derived_from_average", "scaled_from_ad1", "scale_reading", "moving_average",
                                  "moving_average_b", "offset_stage", "gain_stage", "subtract_zero_offset",
                                  "apply_gain", "average16", "calibrate_zero_and_gain", "trim_correct_reading",
                                  "ac_response_correct", "cal_trim_requests", "holdoff_on_low_reading",
                                  "func2_reading_done", "dbm_from_voltage", "ad1_read_value", "ad1_average", "thermocouple_to_celsius", "threshold_check", "sign_offset", "smooth3", "f9r6_correction", "dual_correction", "dual_grid_corners", "autorange_primary", "autorange_secondary", "range_select", "product_scaled", "ad1_process_func1", "ad1_process_func2", "ad1_process_func16", "ad1_process_func17"],
                extra=[(0x200077D0 + i, 1) for i in range(3)] + [(0x200070A8 + 4 * i, 4) for i in range(10)] + [(0x20007238 + 4 * i, 4) for i in range(10)]),
    "dual_grid_corners": dict(ret=None, trials=4000,
                              args=lambda r: (r.randrange(1, 9), r.randrange(1, 8), r.choice([0, 1, 2, 3])),
                              ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 12]),
                                                 0x2000803D: r.randrange(0, 6), 0x20007FDF: r.choice([0, 1, 3])},
                              extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "dual_correction": dict(ret=None, trials=5000,
                            ram_set=lambda r: {0x2000803C: r.choice([13, 14, 15, 22, 23, 24, 13, 15, 24]),
                                               0x2000803D: r.randrange(0, 5), 0x20007FDF: r.choice([0, 1]),
                                               0x2000808A: r.choice([0, 0, 0, 1]), 0x200080CB: r.choice([0, 0, 0, 1]),
                                               0x200080CC: r.choice([0, 1, 2, 1, 2, 3]),
                                               **{a + i: (v >> (8 * i)) & 0xFF
                                                  for a in (0x20007D7C, 0x20007D80)
                                                  for v in [r.choice([r.randrange(-70000, 70000), r.randrange(0, 60001),
                                                                      0, 10, 11, 1000, 1001, 5000, 5001, 10000, 10001,
                                                                      30000, 30001, 50000, 50001, 60000, 60001])
                                                            & 0xFFFFFFFF]
                                                  for i in range(4)}},
                            extra=[(0x20007EB8 + 4 * i, 4) for i in range(4)]),
    "autorange_primary": dict(ret=None, trials=600,
                              args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3199, 3200,
                                                        -55001, r.getrandbits(32)]),),
                              ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                 0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                 0x200080C7: r.choice([0, 1]), 0x20008037: r.choice([10, 11])},
                              with_globals_of=["range_change", "sel_code"],
                              extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "autorange_secondary": dict(ret=None, trials=600,
                                args=lambda r: (r.choice([r.randrange(-70000, 70000), 55000, 55001, 3999, 4000,
                                                          11000, 11001, 399, 400, -55001, r.getrandbits(32)]),),
                                ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), 0x2000803C: r.choice([13, 14, 15, 22, 23, 24]),
                                                   0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 0, 1]),
                                                   0x2000804E: r.choice([0, 0, 1]), 0x200080C6: r.choice([0, 1]),
                                                   0x200080C7: r.choice([0, 1, 1, 2]), 0x20008037: r.choice([10, 11])},
                                with_globals_of=["range_change", "sel_code"],
                                extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "hy3131_on_peak_hold": dict(ret=None, trials=1500,
                                ram_set=lambda r: {**gains_at(0x20007E10, 0x20007E2C, 0x20007E30)(r),
                                                   0x2000803C: r.choice([2, 4, 2, 4, 13, 3]),
                                                   0x200080B7: r.choice([1, 1, 1, 0]), 0x200080B8: r.choice([0, 1]),
                                                   0x2000808C: r.choice([0, 0, 0, 1]), 0x2000808E: r.choice([0, 0, 0, 1]),
                                                   0x20000238: r.choice([0, 0, 0, 3]), 0x2000808A: r.choice([0, 0, 1]),
                                                   0x20008095: r.choice([16, 1, 2]),
                                                   **{0x200077D0 + i: r.getrandbits(8) for i in range(0x0E, 0x14)},
                                                   **{0x200077D0 + 0x13: r.choice([0, 1, 3, 4, r.getrandbits(8)])}},
                                with_globals_of=["subtract_zero_offset", "apply_gain", "average16",
                                                 "calibrate_zero_and_gain", "hy3131_write"],
                                extra=[(0x20007E8C, 4), (0x20007E90, 4)]),
    "counter_capture": dict(ret=None, trials=300, ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}, extra=[(0x2000001C, 4)]),
    "counter_rate_per_ms": dict(ret=None, trials=1000, ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}),
    "counter_process_func8": dict(ret=None, trials=300, ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}),
    "counter_calibrate_func6": dict(ret=None, trials=400, ram_set=lambda r: {**(lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}})(r), 0x2000803C: 6,
                                                                              0x2000803D: r.randrange(0, 4)}),
    "counter_value_func6": dict(ret=None, trials=1000, ram_set=lambda r: {**(lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}})(r), 0x2000808E: r.choice([0, 0, 1]),
                                                                          0x2000803C: r.choice([6, 6, 7]),
                                                                          0x2000803D: r.randrange(0, 4)}),
    "counter_value_func7": dict(ret=None, trials=1000, ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}),
    "counter_set_range_func7": dict(ret=None, trials=600, args=lambda r: (r.choice([0, 1, 2, 3, 9, 7]),), ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}),
    "average16_unsigned": dict(ret=32, trials=200, steps=20, args=lambda r: (r.getrandbits(32),),
                               ram_set=lambda r: {0x20008095: r.choice([16, 1, 2, 3])}, extra=[(0x20007C38, 4), (0x20007C3C, 4)]),
    "calibrate_counter": dict(ret=None, trials=150, steps=20,
                              args=lambda r: (r.randrange(0, 1 << 24), r.randrange(-1000, 1000)),
                              ram_set=lambda r: {**gains_at(0x20007E10)(r), 0x20008095: r.choice([16, 1, 2]),
                                                 0x2000803C: r.choice([12, 12, 6, 7, 2]), 0x2000803D: r.randrange(0, 6)},
                              with_globals_of=["cal_store_value", "gain_select_for_range"],
                              extra=[(0x20007C38, 4), (0x20007C3C, 4), (0x20007E68, 4)]),
    "freq_scale_store": dict(ret=None, trials=1000, args=lambda r: (r.choice([0, 1, 2]), r.randrange(0, 5)),
                             ram_set=lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}}),
    "freq_process": dict(ret=None, trials=3000,
                         args=lambda r: (r.choice([0, 0, 1]), r.randrange(0, 6)),
                         ram_set=lambda r: {**(lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}})(r), 0x2000808E: 0, 0x2000808A: r.choice([0, 0, 1]),
                                            0x20008042: r.choice([0, 1, 1]), 0x2000803C: r.choice([6, 0, 2]),
                                            **{0x20007C44 + i: 0 for i in range(4)},
                                            **{0x20007E18 + i: (v >> (8 * i)) & 0xFF
                                               for v in [r.randrange(40000000, 60000001)] for i in range(4)},
                                            **{0x20007E48 + i: (v >> (8 * i)) & 0xFF
                                               for v in [r.randrange(100, 1 << 25)] for i in range(4)},
                                            **{0x20007E40 + i: (v >> (8 * i)) & 0xFF
                                               for v in [r.randrange(1, 1 << 22)] for i in range(4)}}),
    "counter_process_func7": dict(ret=None, trials=2000,
                                  ram_set=lambda r: {**(lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}})(r), 0x2000803C: 7, 0x2000803D: r.randrange(0, 4),
                                                     0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1])}),
    "counter_process_func12": dict(ret=None, trials=1500, steps=6,
                                   ram_set=lambda r: {**(lambda r: {**{0x20007e40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(1, 1 << 24), 1, 100, r.getrandbits(32)])] for i in range(4)}, **{0x20007e44 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 24), 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e48 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 1 << 25), 999, 1000, 10, 9, 0, r.getrandbits(32)])] for i in range(4)}, **{0x20007e18 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([49152050, r.randrange(40000000, 60000001), r.getrandbits(32)])] for i in range(4)}, **{0x20007c40 + i: (v >> (8 * i)) & 0xFF for v in [r.choice([r.randrange(0, 2000000000), 99999, 100000, 79999, 80000, 7999999, 8000000, 999999999, 1000000000, r.getrandbits(64)])] for i in range(8)}, **{0x200077D0 + i: r.getrandbits(8) for i in range(0x15, 0x1E)}})(r), **gains_at(0x20007E10)(r), 0x2000803C: 12,
                                                      0x2000803D: r.randrange(0, 7), 0x2000808A: r.choice([0, 0, 1]),
                                                      0x20008042: r.choice([0, 1]), 0x2000808C: r.choice([0, 0, 0, 1]),
                                                      0x2000808E: r.choice([0, 0, 0, 1]), 0x20008095: r.choice([16, 1, 2]),
                                                      0x200080A4: r.choice([0, 1, 3]), 0x20008085: r.randrange(0, 11),
                                                      0x20007FDF: r.choice([0, 1])},
                                   with_globals_of=["average16_unsigned", "calibrate_counter", "cal_gain_stage",
                                                    "moving_average_b", "func12_set_interval"],
                                   extra=[(0x20007238 + 4 * i, 4) for i in range(10)] + [(0x20007E00, 4)]),
    "annunciator_temp_unit": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000808A: r.choice([0, 0, 0, 1]),
                       0x20008037: r.choice([100, 11, 12, 10, 10, 1, 2, 3, 0]), 0x200080A5: r.choice([0, 1]),
                       0x2000803E: r.randrange(0, 5), 0x200080AB: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]),
                       0x200080C7: r.choice([0, 1]), 0x200080BA: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "annunciator_dual": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000808A: r.choice([0, 0, 0, 1]),
                       0x20008037: r.choice([100, 11, 12, 10, 10, 1, 2, 3, 0]), 0x200080A5: r.choice([0, 1]),
                       0x2000803E: r.randrange(0, 5), 0x200080AB: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]),
                       0x200080C7: r.choice([0, 1]), 0x200080BA: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "annunciator_sub_clear": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000808A: r.choice([0, 0, 0, 1]),
                       0x20008037: r.choice([100, 11, 12, 10, 10, 1, 2, 3, 0]), 0x200080A5: r.choice([0, 1]),
                       0x2000803E: r.randrange(0, 5), 0x200080AB: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]),
                       0x200080C7: r.choice([0, 1]), 0x200080BA: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "annunciators_secondary": dict(ret=None, trials=3000, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000808A: r.choice([0, 0, 0, 1]),
                       0x20008037: r.choice([100, 11, 12, 10, 10, 1, 2, 3, 0]), 0x200080A5: r.choice([0, 1]),
                       0x2000803E: r.randrange(0, 5), 0x200080AB: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]),
                       0x200080C7: r.choice([0, 1]), 0x200080BA: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "hy3131_on_counter_ready": dict(ret=None, trials=600, steps=4,
        ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 25), 0x2000803D: r.randrange(0, 4),
                       0x200080C6: r.randrange(0, 4), 0x200080C7: r.randrange(0, 4),
                       0x200080B7: r.choice([0, 1]), 0x20008098: r.choice([0, 1]), 0x200080BA: r.choice([0, 1]),
                       0x200080BD: r.choice([0, 1]), 0x2000808A: r.choice([0, 1]), 0x20007FDF: r.choice([0, 1])})(r), **gains_at(0x20007E10)(r),
                           0x2000803C: r.choice([0, 2, 4, 16, 18, 20, 6, 6, 7, 7, 8, 12, 12, 13, 3]),
                           0x2000803D: r.randrange(0, 5), 0x2000808A: r.choice([0, 0, 1]),
                           0x20008042: r.choice([0, 1]), 0x2000808C: 0, 0x2000808E: 0,
                           0x200080A4: r.choice([0, 1]), 0x20008085: r.randrange(0, 11),
                           0x20008037: r.choice([10, 11, 12, 1]), 0x200080A5: r.choice([0, 1]),
                           0x2000803E: r.randrange(0, 5),
                           **{0x2000001C + i: (v >> (8 * i)) & 0xFF for v in [r.choice([0xE000, 0xF000, 0xFF00])] for i in range(4)},
                           **{0x200077D0 + 0x15 + i: r.getrandbits(8) for i in range(6)},
                           **{0x200077D0 + 0x18 + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(1, 100)] for i in range(3)},
                           **{0x200077D0 + 0x1B + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(0, 1 << 24)] for i in range(3)},
                           **{0x20007E18 + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(40000000, 60000001)] for i in range(4)}},
        with_globals_of=["freq_process", "freq_scale_store", "counter_process_func7", "counter_process_func12",
                         "annunciators_secondary", "func12_set_interval", "moving_average_b", "main_dp_for_range"],
        extra=[(0x20007790 + 4 * i, 4) for i in range(8)] + [(0x20007238 + 4 * i, 4) for i in range(10)] + [(0x20007E00, 4)]),
    "func12_timeout_poll": dict(ret=None, trials=800,
                                ram_set=lambda r: {0x200080A3: r.choice([0, 1, 1]), 0x2000803C: r.choice([12, 12, 3]),
                                                   0x2000803D: r.randrange(0, 7), 0x20008042: r.choice([0, 0, 1]),
                                                   0x2000808A: r.choice([0, 0, 1]), 0x200080A2: r.choice([0, 1]),
                                                   0x20007FDF: r.choice([0, 1])},
                                with_globals_of=["func12_set_interval"]),
    "char_to_segments": dict(ret=8, trials=2000, args=lambda r: (r.getrandbits(8),)),
    "dp_from_sel_c6": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}),
    "dp_for_dual": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}),
    "dp_for_range": dict(ret=8, args=lambda r: (r.getrandbits(8),)),
    "temp_for_display": dict(ret=32, trials=3000, args=lambda r: (r.choice([r.randrange(-32768, 32768), r.randrange(-30000, 30000), r.getrandbits(32)]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}),
    "sym_w1_b7_update": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w7_b7_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w0_b31_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_b9_for_range": dict(ret=None, args=lambda r: (r.getrandbits(8),), extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_b8_b11_row1": dict(ret=None, args=lambda r: (r.choice(list(range(8)) + [r.getrandbits(8)]),), extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_b8_b11_row2": dict(ret=None, args=lambda r: (r.choice(list(range(8)) + [r.getrandbits(8)]),), extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w0_b16_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "display_partial_mode": dict(ret=8, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}),
    "sym_w2_b31_update": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w5_b7_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w0_b0_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "autorange_shown": dict(ret=8, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}),
    "sym_state_804d": dict(ret=None, args=lambda r: (r.choice([0, 1, 2, 3, r.getrandbits(8)]),), extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_w6_b0_b16_update": dict(ret=None, args=lambda r: (r.choice([0, 1, 1, 2]),), ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "lcd_put_char": dict(ret=None, trials=3000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.getrandbits(8), r.choice(b"0123456789ALPbdn- ="), 0xFF]), r.choice(list(range(10)) * 3 + [10, 11, r.getrandbits(8)]))),
    "lcd_put_text5": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {0x20007C30 + i: r.choice([r.getrandbits(8), r.choice(b'0123456789AbCdEFLoPnr- ')]) for i in range(5)}, args=lambda r: (r.choice([0, 1, 1, 2]), 0x20007C30)),
    "lcd_put_char_at": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {0x20007C30 + i: r.choice([r.getrandbits(8), r.choice(b'0123456789AbCdEFLoPnr- ')]) for i in range(5)}, args=lambda r: (r.randrange(0, 12), 0x20007C30)),
    "lcd_put_text2": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {0x20007C30 + i: r.choice([r.getrandbits(8), r.choice(b'0123456789AbCdEFLoPnr- ')]) for i in range(5)}, args=lambda r: (r.randrange(0, 12), 0x20007C30)),
    "show_lead": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_dashes": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_blank": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_cal": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], trials=50),
    "show_lobat": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], trials=50),
    "show_sdend": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], trials=50),
    "show_burdn": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], trials=50),
    "show_sde_number": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(0, 30), r.getrandbits(8)]),)),
    "show_lcd_contrast": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(10, 18), r.getrandbits(8)]),)),
    "show_bat_number": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(0, 120), r.getrandbits(16)]),)),
    "show_apo_setting": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_beep_setting": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_bd_setting": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_3_or_15": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {0x200080B3: r.choice([0, 1, 2])}),
    "show_updn_setting": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 2, 3, 4, r.getrandbits(8)]),)),
    "main_show_end_text": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 2, 3, 4, r.getrandbits(8)]),)),
    "show_edit_value": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1]),), ram_set=lambda r: le32_at(0x20007D18, [r.randrange(0, 100000), r.getrandbits(32)])(r)),
    "show_year": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1]), r.getrandbits(8))),
    "show_secondary_number": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(-99999, 100000), r.randrange(-1000, 1000), r.getrandbits(32) - (1 << 31), -(1 << 31), 0]),), ram_set=lambda r: {0x20008040: r.choice([0, 1, 2, 3, 4, 5, r.getrandbits(8)])}),
    "show_secondary_number_dp": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(-99999, 100000), r.randrange(-1000, 1000), r.getrandbits(32) - (1 << 31), -(1 << 31), 0]), r.choice([0, 1, 2, 3, 4, 5]), r.choice([0, 1]))),
    "show_main_number": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.getrandbits(32), r.choice([0, 1, 2, 3, 4, 5]), r.choice([r.randrange(-99999, 100000), r.randrange(-1000, 1000), r.getrandbits(32) - (1 << 31), -(1 << 31), 0]), r.choice([0, 1])), ram_set=lambda r: {0x2000804F: r.choice([0, 0, 1])}),
    "sym_w7_b0_update": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "sym_w2_w4_b16_update": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "sym_b30_update": dict(ret=None, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "sym_w1_w3_w5_b0_update": dict(ret=None, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_range_marks_update": dict(ret=None, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "secondary_dp_for_freq": dict(ret=None, trials=600, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "sym_range_state_update": dict(ret=None, trials=1500, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1, 1, 0, 2]),)),
    "sym_function_units": dict(ret=None, trials=2000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice(list(range(27)) + [r.getrandbits(8)]), r.randrange(0, 8), r.choice([0, 1, 2]), r.choice([0, 1, 2]))),
    "sym_function_units_clear": dict(ret=None, ram_set=lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])}, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "bargraph_clear": dict(ret=None, trials=2000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice(list(range(27)) + [r.getrandbits(8)]), r.choice(list(range(27)) + [r.getrandbits(8)]))),
    "show_ofl": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}, args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_open": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}, args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "show_freq_secondary": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D84, [r.randrange(-10**7, 10**7)])(r)}),
    "show_in_number": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([r.randrange(0, 1000), r.randrange(0, 20), r.getrandbits(16)]),)),
    "main_show_avg_count": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}, args=lambda r: (r.choice([0, 1, 2, 3, 4, 5, r.getrandbits(8)]),)),
    "show_pair_dash": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], args=lambda r: (r.choice([0, 1]), r.getrandbits(8), r.getrandbits(8))),
    "show_pair_blink": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}, args=lambda r: (r.choice([0, 1]), r.getrandbits(8), r.getrandbits(8))),
    "bargraph_compute": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}, args=lambda r: (r.choice([0, 1, 1, 2]),)),
    "bargraph_compute_for_function": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}),
    "bargraph_draw": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}),
    "bargraph_update": dict(ret=None, trials=800, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}),
    "bargraph_off": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "bargraph_reset": dict(ret=None, trials=800, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])}),
    "show_temperature": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}, args=lambda r: (r.choice([r.randrange(-30000, 30000), r.randrange(-1000, 1000), r.getrandbits(32)]),)),
    "show_burden_value": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}, args=lambda r: (r.choice([r.randrange(-10**6, 10**6), r.randrange(-5000, 5000), -(1 << 31), r.getrandbits(32) - (1 << 31)]), r.choice([0, 0, 1, 1, 100, 100, r.getrandbits(8)]))),
    "show_edit_blink": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "show_dbm": dict(ret=None, trials=1500, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "sym_status_update_all": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "display_redraw_symbols": dict(ret=None, trials=600, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "main_display_update": dict(ret=None, trials=2000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "secondary_display_update": dict(ret=None, trials=3000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    "display_update": dict(ret=None, trials=2000, extra=[(0x20007790 + 4 * i, 4) for i in range(8)], ram_set=lambda r: {**(lambda r: {**(lambda r: {0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8), 0x200080C6: r.choice([0, 1]), 0x200080C7: r.choice([0, 1]), 0x200080AB: r.choice([0, 1]), 0x200080B1: r.choice([0, 1, 2]), 0x2000803B: r.choice([0, 1]), 0x2000802B: r.choice([0, 1]), 0x20007FE1: r.choice([0, 1, 3]), 0x20007FE3: r.choice([0, 1, 2]), 0x20007FE4: r.choice([0, 0, 1]), 0x20008050: r.choice([0, 1, 3]), 0x2000804F: r.choice([0, 0, 1]), 0x200080B9: r.choice([0, 1]), 0x20007FE7: r.choice([0, 1]), 0x200080E6: r.choice([0, 0, 1]), 0x20008039: r.choice([0, 1]), 0x2000804E: r.choice([0, 1, 2, 3, 4, 5]), 0x20008058: r.choice([0, 1]), 0x2000808A: r.choice([0, 0, 1]), 0x20008042: r.choice([0, 1]), 0x200080B7: r.choice([0, 1]), 0x20007FE8: r.choice([0, 1]), 0x200080B8: r.choice([0, 1]), 0x200080BA: r.choice([0, 0, 1]), 0x20007FE9: r.choice([0, 0, 1]), 0x20007FEB: r.randrange(0, 26), 0x2000803E: r.randrange(0, 8), 0x20008040: r.randrange(0, 6), 0x200080B3: r.choice([0, 1])})(r), **le32_at(0x20007D68, [r.randrange(-60000, 60000), r.randrange(-2000, 2000), -(1 << 31), 0, 400, 399, 2000, 1999])(r), **le32_at(0x20007D60, [0, 1000, 60000, 1 << 31])(r), 0x20008046: r.choice([0, 0, 1]), 0x20008041: r.choice(list(range(27)) + [r.getrandbits(8)]), 0x20008076: r.choice([0, 1]), 0x200080D5: r.choice([0, 1]), 0x20008095: r.choice([0, 5, 9, 10, 16, 19, 25])})(r), 0x20008037: r.choice(list(range(13)) + [100, 100, 13, r.getrandbits(8)]), 0x2000801D: r.choice([0, 0, 0, 0, 1]), 0x2000801B: r.choice([0, 0, 0, 0, 0, 0, 20, 12, 15]), 0x2000806E: r.choice([0, 0, 1]), 0x2000806F: r.choice([0, 1]), 0x20008097: r.choice([0, 1, 2]), 0x200080A8: r.choice([0, 1]), 0x200080A9: r.choice([0, 1]), 0x200080A6: r.choice([0, 1]), 0x200080A5: r.choice([0, 1]), 0x200080C2: r.choice([0, 1]), 0x200080C0: r.choice([0, 0, 1]), 0x200080C1: r.choice([0, 0, 1]), 0x200080CA: r.choice([0, 0, 1]), 0x200080CB: r.choice([0, 0, 1]), 0x200080AE: r.choice([0, 0, 1]), 0x200080AA: r.choice([0, 0, 1]), 0x200080A7: r.choice([0, 0, 1]), 0x200080D6: r.choice([0, 1]), 0x20008071: r.randrange(0, 6), 0x2000805D: r.choice([0, 1]), 0x2000805F: r.choice([0, 1]), 0x2000805E: r.choice([0, 1]), 0x20008060: r.choice([0, 1]), 0x20008061: r.choice([0, 1]), 0x2000804C: r.choice([0, 1]), 0x20008062: r.choice([0, 1, 1]), 0x20008063: r.choice([0, 1]), 0x20000236: r.choice([0, 10, 11, 12]), 0x2000805A: r.choice([0, 0, 1, 2]), 0x200080BF: r.choice([0, 0, 1]), 0x2000808C: r.choice([0] * 12 + [1]), 0x2000808D: r.choice([0] * 12 + [1]), 0x2000808E: r.choice([0] * 12 + [1]), 0x20008090: r.choice([0] * 12 + [1]), 0x2000808F: r.choice([0] * 12 + [1]), 0x20008091: r.choice([0] * 12 + [1]), 0x20008092: r.choice([0] * 12 + [1]), 0x20008093: r.choice([0] * 12 + [1]), 0x20008094: r.choice([0] * 12 + [1]), 0x20000238: r.choice([0] * 8 + [5]), 0x2000805C: r.choice([0] * 8 + [1]), 0x2000803F: r.randrange(0, 6), 0x200080B6: r.randrange(0, 5), 0x20008098: r.choice([0, 1]), 0x20008099: r.choice([0, 1]), 0x20000237: r.randrange(9, 19), 0x20008078: r.choice([0, 1]), 0x20007FE4: r.choice([0, 0, 0, 1]), **le32_at(0x20007D64, [r.randrange(-99999, 100000), r.randrange(-10**6, 10**6), r.choice([100000, 99999, -100000, -99999])])(r), **le32_at(0x20007D38, [r.randrange(0, 100000), r.randrange(0, 10**7)])(r), **le32_at(0x20007EB0, [r.randrange(-99999, 100000), r.randrange(-9999, 10000)])(r), **le32_at(0x20007EAC, [r.randrange(-99999, 100000)])(r), **le32_at(0x20007E6C, [r.randrange(-10**6, 10**6)])(r), **{0x20007FCC + i: (v >> (8 * i)) & 0xFF for v in [r.randrange(-30000, 30000)] for i in range(2)}, **{0x20007E94 + i: b for b4 in [__import__('struct').pack('<f', r.choice([r.uniform(-90, 60), r.uniform(-2000, 2000), 0.0, -0.0, 10.0, 100.0, 1000.0, 9.9999]))] for i, b in enumerate(b4)}}),
    # ---- SD card driver and disk I/O ----
    "sd_init": dict(ret=8, trials=3, model=dict(sd_image=SD_IMAGE)),
    "sd_init@hc": dict(ret=8, trials=3, model=dict(sd_image=SD_IMAGE, sd_high_capacity=True)),
    "sd_init@v1": dict(ret=8, trials=3, model=dict(sd_image=SD_IMAGE, sd_v2=False)),
    "sd_init@absent": dict(ret=8, trials=3, model=dict(sd_image=SD_IMAGE, sd_absent=True)),
    "disk_initialize": dict(ret=8, trials=3, args=lambda r: (0,), model=dict(sd_image=SD_IMAGE)),
    "disk_status": dict(ret=8, args=lambda r: (r.choice([0, 0, 1, r.getrandbits(8)]),)),
    "disk_ioctl": dict(ret=8, args=lambda r: (0, r.randrange(0, 6), SD_BUF)),
    "get_fattime": dict(ret=32, trials=5),
    "sd_detect": dict(ret=8, trials=5),
    "sd_power_off": dict(ret=None, trials=5),
    "sd_low_level_deinit": dict(ret=None, trials=5),
    "sd_get_transfer_status": dict(ret=8, trials=5, prep=SD_INIT, model=dict(sd_image=SD_IMAGE)),
    "disk_read": dict(ret=8, trials=12, prep=SD_INIT, model=dict(sd_image=SD_IMAGE),
                      args=lambda r: (0, SD_BUF, r.choice([0, 63, 64, 65, r.randrange(0, 8192)]), 1)),
    "disk_read@hc": dict(ret=8, trials=8, prep=SD_INIT, model=dict(sd_image=SD_IMAGE, sd_high_capacity=True),
                         args=lambda r: (0, SD_BUF, r.randrange(0, 8190), r.choice([1, 2]))),
    "disk_read@irq": dict(ret=8, trials=8, prep=SD_INIT, regs=SD_IRQS, model=dict(sd_image=SD_IMAGE),
                          args=lambda r: (0, SD_BUF, r.randrange(0, 8192), 1)),
    "disk_write": dict(ret=8, trials=10, prep=SD_INIT, regs=SD_IRQS, ram_set=SD_BUF_BYTES, model=dict(sd_image=SD_IMAGE),
                       args=lambda r: (0, SD_BUF, r.randrange(0, 8190), r.choice([1, 2]))),
    "disk_write@hc": dict(ret=8, trials=6, prep=SD_INIT, regs=SD_IRQS, ram_set=SD_BUF_BYTES,
                          model=dict(sd_image=SD_IMAGE, sd_high_capacity=True),
                          args=lambda r: (0, SD_BUF, r.randrange(0, 8190), 1)),
    "sd_read_block": dict(ret=8, trials=8, prep=SD_INIT, model=dict(sd_image=SD_IMAGE),
                          args=lambda r: (SD_BUF, 0, r.randrange(0, 8192) * 512, 0, 512)),
    "sd_write_multi_blocks": dict(ret=8, trials=6, prep=SD_INIT, ram_set=SD_BUF_BYTES, model=dict(sd_image=SD_IMAGE),
                                  args=lambda r: (SD_BUF, 0, r.randrange(0, 8190) * 512, 0, 512, r.choice([1, 2]))),
    "sd_wait_write_operation": dict(ret=8, trials=6, prep=SD_INIT + [("sd_write_multi_blocks", (SD_BUF, 0, 4096, 0, 512, 1))],
                                    regs=SD_IRQS, model=dict(sd_image=SD_IMAGE)),
    "sd_wait_write_operation@noirq": dict(ret=8, trials=4, prep=SD_INIT + [("sd_write_multi_blocks", (SD_BUF, 0, 4096, 0, 512, 1))],
                                          model=dict(sd_image=SD_IMAGE)),
    "sd_process_irq": dict(ret=8, trials=4, prep=SD_INIT + [("sd_read_block", (SD_BUF, 0, 4096, 0, 512))],
                           model=dict(sd_image=SD_IMAGE)),
    "sd_process_dma_irq": dict(ret=None, trials=4, prep=SD_INIT + [("sd_read_block", (SD_BUF, 0, 4096, 0, 512))],
                               model=dict(sd_image=SD_IMAGE)),
    "gpio_set_af": dict(ret=None, args=lambda r: (r.choice([0x40020000, 0x40020400, 0x40020800, 0x40020C00]),
                                                 r.randrange(0, 16), r.randrange(0, 16))),
    # ---- FatFs R0.08a (common/fatfs/ff.c, unmodified) ----
    "f_mount": dict(ret=8, trials=5, prep=SD_INIT, ram_set=fs_ram, model=FS_MODEL,
                    args=lambda r: (r.choice([0, 0, 1]), FS_OBJ)),
    "f_open": dict(ret=8, trials=30, prep=FS_MOUNT, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                   args=lambda r: (FIL_OBJ, r.choice(list(PATHS)), r.choice([0x01, 0x0A, 0x12, 0x13, 0x04, 0x03, 0x00]))),
    "f_open@unmounted": dict(ret=8, trials=5, prep=SD_INIT, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                             args=lambda r: (FIL_OBJ, P_TEST, 0x01)),
    "f_read": dict(ret=8, trials=20, steps=3, prep=FS_READ, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                   args=lambda r: (FIL_OBJ, SD_BUF, r.choice([0, 1, 100, 511, 512, 513, 1000, 1024]), BW)),
    "f_write": dict(ret=8, trials=15, steps=4, prep=FS_WRITE, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                    args=lambda r: (FIL_OBJ, SD_BUF, r.choice([0, 1, 100, 511, 512, 513, 1000, 1024]), BW)),
    "f_write@append": dict(ret=8, trials=10, steps=3, prep=FS_APPEND + [("f_lseek", (FIL_OBJ, 10))],
                           regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                           args=lambda r: (FIL_OBJ, SD_BUF, r.choice([1, 37, 512, 700]), BW)),
    "f_lseek": dict(ret=8, trials=20, steps=3, prep=FS_READ, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                    args=lambda r: (FIL_OBJ, r.choice([0, 1, 511, 512, 513, 2999, 3000, 3001, 5000, r.randrange(0, 4000)]))),
    "f_lseek@write": dict(ret=8, trials=10, steps=2, prep=FS_WRITE, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                          args=lambda r: (FIL_OBJ, r.choice([0, 100, 512, 1500, 2048]))),
    "f_sync": dict(ret=8, trials=8, args=lambda r: (FIL_OBJ,), prep=FS_WRITE + [("f_write", (FIL_OBJ, SD_BUF, 700, BW))],
                   regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL),
    "f_close": dict(ret=8, trials=8, args=lambda r: (FIL_OBJ,), prep=FS_WRITE + [("f_write", (FIL_OBJ, SD_BUF, 300, BW))],
                    regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL),
    "f_close@read": dict(ret=8, trials=4, args=lambda r: (FIL_OBJ,), prep=FS_READ, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL),
    "f_opendir": dict(ret=8, trials=6, ignore=[(DIR_OBJ + 0x18, 4)], prep=FS_MOUNT, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                      args=lambda r: (DIR_OBJ, r.choice([P_ROOT, P_TEST, 0x20004980]))),
    "f_readdir": dict(ret=8, trials=6, steps=4, ignore=[(DIR_OBJ + 0x18, 4)], prep=FS_MOUNT + [("f_opendir", (DIR_OBJ, P_ROOT))],
                      regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL, args=lambda r: (DIR_OBJ, FINFO)),
    "f_stat": dict(ret=8, trials=10, prep=FS_MOUNT, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                   args=lambda r: (r.choice(list(PATHS)), FINFO)),
    "f_utime": dict(ret=8, trials=10, prep=FS_MOUNT, regs=SD_IRQS, ram_set=fs_ram, model=FS_MODEL,
                    args=lambda r: (r.choice(list(PATHS)), FINFO)),
    # ---- interrupt handlers ----
    "TIM3_IRQHandler": dict(ret=None, trials=1500,
                            ram_set=lambda r: {0x20007FB4: r.choice([0, 1, 1, 2, 7]), 0x20007FB5: r.choice([0, 0, 0, 1]),
                                               0x2000803A: r.choice([0, 0, 1]), 0x2000807A: r.randrange(0, 6),
                                               0x20008078: r.choice([0, 1]), 0x2000022E: r.choice([0, 1, 5]),
                                               0x2000022F: r.choice([0, 0, 1])}),
    "SysTick_Handler": dict(ret=None, ram_set=lambda r: le32_at(0x20007D10, [0, 1, 2, 0xFFFFFFFF])(r)),
    "delay_tick": dict(ret=None, ram_set=lambda r: le32_at(0x20007D10, [0, 1, 2, 0xFFFFFFFF])(r)),
    "RTC_WKUP_IRQHandler": dict(ret=None, trials=5),
    "RTC_WKUP_IRQHandler@pending": dict(ret=None, trials=5, regs={0x40002808: 0x4000, 0x4000280C: 0x0487}),
    "RTC_WKUP_IRQHandler@masked": dict(ret=None, trials=5, regs={0x40002808: 0x0000, 0x4000280C: 0x0487}),
    "EXTI0_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "EXTI1_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "EXTI2_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "EXTI3_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "EXTI4_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "EXTI9_5_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0x0360}),
    "EXTI9_5_IRQHandler@none": dict(ret=None, trials=3),
    "EXTI15_10_IRQHandler": dict(ret=None, trials=3, regs={0x40010414: 0xA400}),
    "EXTI15_10_IRQHandler@all": dict(ret=None, trials=3, regs={0x40010414: 0xFFFF}),
    "ADC1_IRQHandler": dict(ret=None, trials=3, regs={0x40012400: 0x1, 0x40012404: 0x40}),
    "ADC1_IRQHandler@masked": dict(ret=None, trials=3, regs={0x40012400: 0x1, 0x40012404: 0x0}),
    "SDIO_IRQHandler": dict(ret=None, trials=3, prep=SD_INIT + [("sd_read_block", (SD_BUF, 0, 4096, 0, 512))],
                            model=dict(sd_image=SD_IMAGE)),
    "DMA2_CH4_IRQHandler": dict(ret=None, trials=3, prep=SD_INIT + [("sd_read_block", (SD_BUF, 0, 4096, 0, 512))],
                                model=dict(sd_image=SD_IMAGE)),
    "NMI_Handler": dict(ret=None, trials=2), "HardFault_Handler": dict(ret=None, trials=2),
    "MemManage_Handler": dict(ret=None, trials=2), "BusFault_Handler": dict(ret=None, trials=2),
    "UsageFault_Handler": dict(ret=None, trials=2), "SVC_Handler": dict(ret=None, trials=2),
    "DebugMon_Handler": dict(ret=None, trials=2), "PendSV_Handler": dict(ret=None, trials=2),
    # ---- EEPROM settings ----
    "settings_uncalibrated": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_value_7f9a": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_saved_value": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_a": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_b": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_c": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_d": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_e": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_f": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_function_g": dict(ret=8, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_log_interval": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_contrast": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_apo": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_temp_unit": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_flag_80b3": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_sel_80b6": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_flag_80a8": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_load_beep": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    "settings_save_value_7f9a": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, ram_set=lambda r: {a: r.getrandbits(8) for a in (0x20007F9A, 0x20007F9B, 0x20007D14, 0x20007D15, 0x20007D16, 0x20007D17)}),
    "settings_save_saved_value": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, ram_set=lambda r: {a: r.getrandbits(8) for a in (0x20007F9A, 0x20007F9B, 0x20007D14, 0x20007D15, 0x20007D16, 0x20007D17)}),
    "settings_save_log_interval": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_contrast": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_apo": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_temp_unit": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_flag_80b3": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_sel_80b6": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_flag_80a8": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_save_beep": dict(ret=None, trials=100, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.getrandbits(16),)),
    "settings_write_function": dict(ret=None, trials=300, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, ram_set=lambda r: {0x2000803C: r.randrange(0, 27), 0x200080BA: r.choice([0, 1])}, args=lambda r: (r.choice([8, 9, 0xC, r.randrange(0, 0x20)]), r.choice([1, 2, 3, r.getrandbits(8)]))),
    "settings_save_function": dict(ret=None, trials=400, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, ram_set=lambda r: {0x2000803C: r.randrange(0, 27), 0x200080BA: r.choice([0, 1])}),
    "acdc_mode_enter": dict(ret=None),
    "cal_extra_store": dict(ret=None, trials=300, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}, args=lambda r: (r.randrange(0, 26), r.choice([0, 0, 1]), r.getrandbits(32))),
    "cal_extra_clear_all": dict(ret=None, trials=20, eeprom=lambda r: {o: r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x82, 0xA5, 0xFF, r.getrandbits(8)]) for o in range(0x30)}),
    # ---- MEM records ----
    "mem_mark_counts": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_recall_init": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_save_function": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_restore_function": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_clock_snapshot_a": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_clock_restore_a": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_clock_snapshot_b": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_clock_restore_b": dict(ret=None, trials=200, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}),
    "mem_recall_index": dict(ret=16, trials=1000, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}, args=lambda r: (r.choice([0, 1, 2, 30, 60, 61, r.getrandbits(16)]),)),
    "mem_store_record": dict(ret=None, trials=300, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}, extra=[(0x20007810 + i, 1) for i in range(0, 0x2D0, 7)] + [(0x200073C8 + 4 * i, 4) for i in range(0, 180, 5)], args=lambda r: (r.randrange(0, 60),)),
    "mem_store": dict(ret=None, trials=300, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}, extra=[(0x20007810 + i, 1) for i in range(0, 0x2D0, 7)] + [(0x200073C8 + 4 * i, 4) for i in range(0, 180, 5)], args=lambda r: (r.randrange(0, 60),)),
    "mem_recall_record": dict(ret=None, trials=300, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}, extra=[(0x20007810 + i, 1) for i in range(0, 0x2D0, 7)] + [(0x200073C8 + 4 * i, 4) for i in range(0, 180, 5)], args=lambda r: (r.randrange(0, 60),)),
    "mem_recall": dict(ret=None, trials=300, ram_set=lambda r: {**{a: r.getrandbits(8) for a in list(range(0x20007FE5, 0x20008024)) + list(range(0x20007D1C, 0x20007D40)) + list(range(0x200080D7, 0x200080DD)) + list(range(0x20007F9C, 0x20007FAE))}, **le32_at(0x20007D38, [0, 1, 59, 60, 61, 1000])(r), 0x2000803C: r.randrange(0, 26), 0x2000803D: r.randrange(0, 8)}, extra=[(0x20007810 + i, 1) for i in range(0, 0x2D0, 7)] + [(0x200073C8 + 4 * i, 4) for i in range(0, 180, 5)], args=lambda r: (r.randrange(0, 60),)),
    # ---- LCD driver ----
    "lcd_update": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "lcd_update_partial": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(8)]),
    "lcd_clear_shadow": dict(ret=None, extra=[(0x20007790 + 4 * i, 4) for i in range(16)]),
    "number_to_digits": dict(ret=None, args=lambda r: (r.choice([0, 9, 10, 99999, 100000,
                                                                  r.getrandbits(17), r.getrandbits(32)]),)),
    "digits_to_text": dict(ret=None, args=lambda r: (r.randrange(0, 7),),
                           extra=[(0x20007DB0 + 4 * i, 4) for i in range(5)], trials=400),
    "lcd_set_secondary_dp": dict(ret=None, args=lambda r: (r.randrange(0, 7),), extra=[(0x20007790, 4)]),
    "lcd_set_main_dp": dict(ret=None, args=lambda r: (r.randrange(0, 7),),
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


def new_machine(flash, hybrid, ram, eeprom=EEPROM, **model):
    if model.get("sd_image") is not None:
        model = dict(model, sd_image=bytearray(model["sd_image"]))   # each run writes its own card
    m = Stm32l1(flash, extra_code=(HYBRID_BASE, hybrid), stop_on_app_entry=False,
                eeprom_image=eeprom, **model)
    m.uc.mem_write(RAM_BASE, bytes(ram))
    return m


def step(m, func, args, idr):
    """One call on an existing machine; RAM carries over between steps."""
    m.idr.update(idr)
    m.stop_reason = None
    m.blocks = 0
    r0 = m.call(func, args, sp=INITIAL_SP, max_blocks=200_000)
    from unicorn.arm_const import UC_ARM_REG_R1
    r0 |= m.uc.reg_read(UC_ARM_REG_R1) << 32      # for 64-bit (double) returns
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
    ap.add_argument("--shard", help="i/n: run every n-th spec starting at i (for parallel runs)")
    args = ap.parse_args()

    flash = open(os.path.join(ROOT, "dumps", "flash.bin"), "rb").read()
    hybrid = open(HYBRID_BIN, "rb").read()
    names = load_names()
    rebuilt = hybrid_functions()
    afl, sizes, _ = load_access()

    failures = 0
    tested = 0
    shard = tuple(int(x) for x in args.shard.split("/")) if args.shard else (0, 1)
    for index, (key, spec) in enumerate(SPECS.items()):
        name = key.split("@")[0]
        if args.only and args.only not in (key, name):
            continue
        if index % shard[1] != shard[0]:
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
            ee = bytearray(EEPROM)
            for off, val in (spec["eeprom"](rnd).items() if "eeprom" in spec else []):
                ee[off] = val
            mo = new_machine(flash, hybrid, ram, ee, **spec.get("model", {}))
            mn = new_machine(flash, hybrid, ram, ee, **spec.get("model", {}))
            idr = spec["idr"](rnd) if "idr" in spec else {}
            # prep: original functions run on both machines first (e.g. card initialisation)
            for pname, pargs in spec.get("prep", []):
                step(mo, names[pname], pargs, idr)
                step(mn, names[pname], pargs, idr)
            for m in (mo, mn):
                m.regs.update(spec.get("regs", {}))      # e.g. NVIC enables
                m.trace.clear()
            for s in range(steps):
                a = spec["args"](rnd) if "args" in spec else ()
                if "idr" in spec and rnd.random() < spec.get("idr_change", 1.0):
                    idr = spec["idr"](rnd)
                ro, ram_o = step(mo, orig, a, idr)
                rn, ram_n = step(mn, rebuilt[name], a, idr)
                problems = compare_step(spec, mo, mn, ro, rn, ram_o, ram_n)
                if problems:
                    problems.insert(0, "step %d of %d" % (s, steps))
                    # DUMP=0x20007C5C:0x30 prints that RAM range from both runs
                    if os.environ.get("DUMP"):
                        lo, n = (int(x, 0) for x in os.environ["DUMP"].split(":"))
                        for tag, img in (("orig", ram_o), ("new ", ram_n)):
                            words = struct.unpack_from("<%dI" % (n // 4), img, lo - RAM_BASE)
                            problems.append("%s %s" % (tag, " ".join("%08X" % w for w in words)))
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
    if spec.get("ignore"):     # RAM that legitimately differs (e.g. pointers into the stack)
        ram_o, ram_n = bytearray(ram_o), bytearray(ram_n)
        for a, n in spec["ignore"]:
            ram_o[a - RAM_BASE:a - RAM_BASE + n] = ram_n[a - RAM_BASE:a - RAM_BASE + n]
    d = ram_diff(ram_o, ram_n)
    if d is not None:
        problems.append("RAM differs at 0x%08X: 0x%02X vs 0x%02X"
                        % (d, ram_o[d - RAM_BASE], ram_n[d - RAM_BASE]))
    if mo.sd is not None and mo.sd.image != mn.sd.image:
        k = next(i for i, (x, y) in enumerate(zip(mo.sd.image, mn.sd.image)) if x != y)
        problems.append("SD card contents differ at byte 0x%X" % k)
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
