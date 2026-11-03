# Application (v2.05) — architecture map

Image: `dumps/app.bin` = flash 0x08006000–0x080294E7 (144,616 bytes), byte-identical to `EEVBlog2_05.bin`. Status: fully rebuilt in C, equivalent in emulation, and running on the meter.

## Build facts

- IAR EWARM, but **a different compiler version or optimisation level from the bootloader**: `GPIO_Init` is 164 bytes here and 168 in the bootloader. Only 70 of 794 functions match the bootloader exactly (`tools/match_functions.py`, `docs/app_matches.tsv`).
- 794 functions (119 KB of code). The rest is tables, including about 11 KB of 32-bit correction tables at 0x08026068–0x08028CDC.
- **RAM:** 0x23E bytes of initialised data at 0x20000000, stored **LZ77-compressed** in flash at 0x08029344 and unpacked by IAR's `__iar_lz77_init_single3` (0x080287C0). `tools/iar_lz77.py` reproduces it, and the unpacked image is `dumps/app_data_init.bin`. Next comes the stack (4 KB, initial SP 0x20001240), then 0x6EBC bytes of zeroed globals (0x20001240–0x200080FB).
- The initialised data is mostly the CSV log text: header fields (`ID,START,INTERVAL,sec,...`), function names (`LowZ,ACV,DCV,DCmV,...,Burden,`) and unit strings.
- **Libraries:** ST Standard Peripheral Library (RCC, GPIO, LCD, PWR, TIM, NVIC, DMA, SDIO, EXTI), **FatFs R0.08a** (same as the bootloader; several functions match byte-for-byte), the same SDIO SD-card driver and disk glue as the bootloader, IAR double-precision floating point plus `log` (errno-setting, core at 0x080204C4 with the entry stub at 0x0801BB5C), and the IAR runtime.

## Approximate address map

| Range | Contents |
|---|---|
| 0x08006124–0x0800BFFF | measurement processing; HY3131 service and per-mode configuration |
| 0x0800C03C–0x0800C2FF | HY3131 bit-banged SPI |
| 0x0800C300–0x0801007F | modes, analogue front-end switching (0x0800FC60–0x0800FE74), buzzer/backlight |
| 0x08010204–0x080103A7 | NJU6350 RTC bit-banged interface |
| 0x080103A8–0x0801B2FF | display formatting, menus, logging, calibration, SD/BLE features |
| 0x0801B304–0x0801B7FF | LCD segment driver (0x0801B304 has 29 callers) |
| 0x0801B874–0x0801BDEB | IAR floating point and `log` |
| 0x0801BDEC–0x0801CA9F | hardware init (`system_init` 0x0801BE76, GPIO/clock/peripheral set-up) |
| 0x0801CAA0–0x0801FD00 | more application code (SD logging, calibration file, etc.) |
| 0x0801FD00–0x08020B83 | ST library: RCC, LCD, PWR, misc/NVIC, TIM, RTC, EXTI |
| 0x080204C4–0x0802079B | IAR double-precision `log` core |
| 0x08020B84–0x0802225B | FatFs R0.08a |
| 0x0802225C–0x08024287 | SDIO SD-card driver, disk I/O glue, SDIO/DMA library |
| 0x0802428C–0x08024B3F | interrupt handlers |
| 0x08024B40–0x08025F6F | `main` and top-level control |
| 0x08025F70–0x08026067 | `SystemInit` |
| 0x08026068–0x08028CDB | correction tables (data) |
| 0x08028CDC–0x08029173 | IAR start-up and runtime (zero init, LZ77 init, `__cmain`, exit) |
| 0x08029174–0x08029343 | `Reset_Handler`, default IRQ handlers |
| 0x08029344–0x080294E7 | compressed initialised data |

## Hardware interfaces found so far

| Interface | Pins | Code |
|---|---|---|
| **HY3131** SPI (bit-banged) | PF1 = CS (low), PF0 = CK, PF2 = DI (MCU→HY), PF3 = DO (HY→MCU; IRQ output while CS is high) | write 0x0800C03C, read 0x0800C0C0. Frame: `reg<<1 \| R/W`, then data, auto-incrementing; reads have one turnaround clock |
| **NJU6350 RTC** (3-wire) | PC13 = CE (high), PB6 = CLK, PB7 = DATA (bidirectional) | 0x08010204–0x08010378: 4-bit address then 8 data bits, LSB first |
| **Rotary switch** | PG0–PG7 (low = selected) | `read_rotary_switch` 0x08014A14 → byte at 0x20008066 |
| **Keys** | PG8–PG15 (low = pressed) | still being identified: 0x08016BCA |
| **Unknown presence detect** | PF8 sampled 20× with a pull-up at boot | 0x0800C28C → flag at 0x20007FDF |
| **LCD** | STM32 LCD peripheral, 4 COM × 40 SEG | ST LCD library plus a driver around 0x0801B304 |
| **BLE** (BLE112) | USART2 (handler 0x080247D8) | not yet analysed |
| **SD card** | SDIO + DMA2 channel 4 | same driver as the bootloader |

## Control flow

- **Start-up:** `Reset_Handler` → `SystemInit` (0x08025F70) → `__cmain` (zero and LZ77 data init) → `main` (0x08024B40).
- **`main` set-up:** `NVIC_SetVectorTable(0x08000000, 0x6000)`, `system_init`, the PF8 detect, `0x08024DE0`, `0x0801B450(*(u8*)0x20000237, 1)`, then `0x0800EA3A`.
- **Main loop (cooperative, no RTOS):**
  1. `hy3131_poll`: if PF3 is high, `hy3131_service` reads registers 0x00–0x33 and dispatches the AD1-ready, RMS-ready, peak-hold and counter-ready handlers.
  2. Measurement post-processing when `0x2000805D` is set.
  3. Task flags set by TIM2:
     - `0x200080D0`: rotary, keys, input checks, display and front end
     - `0x200080D1`: display and range work
     - `0x200080D3`
     - `0x200080D4`, **1 s**: auto power-off countdown (`0x20000230`; 5 warning beeps at 60 s, `power_off` at 0) and the logging interval (`0x2000022C`/`0x20007FA8` → log record via `0x08010B64`)
  4. SD tasks, calibration-file tasks, and power-off when requested.
- **Interrupts:** TIM2 (the scheduler tick, 1,174 bytes), TIM3 (buzzer), USART2 (BLE), RTC wakeup, ADC1 (battery/temperature?), SDIO and DMA2 channel 4, SysTick. The EXTI0–15 handlers only clear their flags (wake-up sources).
- `0x2000803C` appears to be the current function/mode index (compared against 6, 7, 8, 0x0C).

## HY3131 register map (datasheet, refs/)

0x00 AD1 (24-bit), 0x03 AD2, 0x06 LPF, 0x09 RMS (40-bit), 0x0E PKHMIN, 0x11 PKHMAX, 0x14 CTSTA, 0x15 CTC, 0x18 CTB, 0x1B CTA, 0x1E INTF, 0x1F INTE, 0x20–0x37 configuration (R20 counter/comparator, R23–R28 ADC set-up, R29 RMS/LPF/peak enables, R2A–R2E input switch matrix, R2F–R33 references/op-amps/oscillator).

## Tools

| Tool | Purpose |
|---|---|
| `tools/ghidra_import.ps1`, `tools/ghidra_export.ps1` | import and re-export, with `ApplyNames.java` applying `ghidra/app_names.tsv` |
| `tools/fingerprint.py app` | per-function peripheral/RAM/fan-in summary (`docs/app_fingerprint.txt`) |
| `tools/match_functions.py` | functions byte-identical to identified bootloader code |
| `tools/iar_lz77.py` | IAR LZ77 initialiser decompressor |
| `tools/globals_access.py` | global memory accesses (address, width, read/write) of original functions |

## Reconstruction workflow (hybrid build)

The application is rebuilt module by module and verified before the whole firmware exists:

1. **Analyse:** decompile in Ghidra, check global access widths with `tools/globals_access.py`, and check subtle code against the disassembly. Signedness and stack out-parameters are the usual traps.
2. **Write C** in `firmware/src/`, with each function tagged `orig: 0x...`. Globals go in headers with their original addresses noted, and calls to code not yet rebuilt go through `firmware/include/orig_calls.h`.
3. **Hybrid build** (`cmake -S firmware -B build/firmware`): the rebuilt modules are linked at 0x08100000. Every global is bound to its original RAM address and every unreconstructed function to its original code (`tools/hybrid_syms.py`, using `ghidra/app_names.tsv`).
4. **Equivalence test** (`python -P tools/emu/test_app_functions.py`): each rebuilt function and its original run from identical randomised states, with boundary values taken from the original's compare constants, optionally as multi-step sequences. The test compares the return value, all RAM outside the stack, and every peripheral write. A planted off-by-one is caught.

5. **Standalone image** (`build/firmware/app.bin`, linked at 0x08006000 by `firmware/stm32l152xd_app.ld`): every function is rebuilt, so only RAM variables are bound to their original addresses (`tools/hybrid_syms.py --ram-only` fails if anything else is still needed). `firmware/src/startup.c` has the vector table and the start-up code; it clears 0x20001240..0x200080FB and copies the initialised variables (`firmware/src/ram_init.c`, generated by `tools/gen_ram_init.py`) to 0x20000000, as the IAR runtime does.
6. **Whole-firmware comparison** (`python -P tools/emu/compare_app.py <blocks> 0 --points --ticks=N`): the original image and the original bootloader plus `app.bin` boot from reset in the model. The timers tick at the loop heads of the start-up screen and the main loop (one TIM3 tick per visit, TIM2 every tenth), so both programs take interrupts at the same points. Both stop after N visits, and the comparison covers every peripheral write (main program and each interrupt separately) and all RAM outside the stack. With `--ticks=40000` (4000 TIM2 ticks, about 40 s of meter time: boot, start-up screen, measurement, display, keys, ADC, RTC) the runs are identical: 294,266 peripheral writes and the final RAM.
7. **On the meter:** `app.bin` was written to 0x08006000 over SWD (the bootloader, option bytes and EEPROM verified unchanged) and checked working on the hardware. The first flash found one timing problem the emulator can't see (see the `clock_init()` timing deviation below).

The globals keep their original addresses in the standalone image too; giving them ordinary C definitions is a later clean-up.

**Narrow parameters.** IAR callees extend their own `uint8_t`/`int16_t` arguments (`uxtb`/`sxth` on entry), while GCC relies on the caller having done it. Rebuilt functions whose original truncates an argument internally take the full register and truncate explicitly (for example `temp_for_display()`). In the final all-GCC build callers always extend, so this only matters while unrebuilt IAR code calls rebuilt functions in the hybrid image.

## Reconstruction status

`python -P tools/coverage.py app` → `docs/coverage_app.csv`.

| Module | Source | Functions | Status |
|---|---|---|---|
| Keys and rotary switch | `firmware/src/keys.c` | 9 | equivalent |
| Board I/O (delay, GPIO_Init) | `firmware/src/board_io.c` | 2 | equivalent (GPIO via callers) |
| HY3131 serial bus, PF8 detect | `firmware/src/hy3131_bus.c` | 6 | equivalent |
| NJU6350 RTC | `firmware/src/rtc.c` | 11 | equivalent |
| LCD driver | `firmware/src/lcd_driver.c` | 14 | equivalent |
| Start-up (clocks, power, GPIO, ADC, NVIC, timers) | `firmware/src/system_init.c` | 15 | equivalent, including BOR-set, wake-from-standby and no-HSE variants |
| Flash/option-byte helpers | `firmware/src/flash_ob.c` | 8 | equivalent (tested directly or via `system_init`) |
| HY3131 control (mode registers, IRQ enables, counter, service, poll) | `firmware/src/hy3131_ctrl.c`, generated `hy3131_tables.c` | 6 | equivalent for valid function/range indices |
| RMS results | `firmware/src/measure_rms.c` | 7 | equivalent |
| Measurement scaling (zero offset, gain, 16-sample mean, gain divisors) | `firmware/src/measure_scale.c`, `firmware/include/numeric.h` | 11 | equivalent for finite, realistic float values |
| Data EEPROM access (read, program with retries, BE reads/writes, markers) | `firmware/src/eeprom.c` | 14 | equivalent, including EEPROM write traces |
| Calibration storage and zero/gain calibration | `firmware/src/calibration.c` | 5 | equivalent |
| Calibration loading (records, trims, extra offsets, dual-gain pairs), trim capture | `firmware/src/calibration.c` | 10 | equivalent on randomised EEPROM contents |
| AD1 results (24-bit read, running mean, per-function dispatch) | `firmware/src/measure_ad1.c` | 4 | equivalent; the whole AD1 processing tree is rebuilt |
| Front-end switching per function | `firmware/src/front_end.c` | 8 | equivalent |
| Buzzer (PE7), beep presets, auto power-off reset | `firmware/src/buzzer.c` | 6 | equivalent |
| Measurement helpers, calibration stages | `firmware/src/measure_scale.c`, `firmware/src/calibration.c` | 7 | equivalent |
| Derived values (power/VA product, vector magnitude, thresholds, hold-off, dBm, AD1 staging, function 12 interval) | `firmware/src/measure_derived.c` | 10 | equivalent (dBm for ranges 0–3) |
| Reading filters (3-sample smoother, two moving-average instances) | `firmware/src/filters.c` | 3 | equivalent for in-range counters |
| Function/range change (HY3131 + calibration reload, decimal point, readings skipped after a change, VA borrow), dual-readout auto-range | `firmware/src/range_select.c` | 10 | equivalent for valid table rows |
| Per-function AD1 processing: functions 0–5, 9–11, 16–21 (incl. AC+DC and VA phase alternation) | `firmware/src/measure_funcs.c` | 22 | equivalent; also tested through the dispatcher over all functions |
| AC frequency response (functions 2/4) and function 2 range 0 frequency offset | `firmware/src/ac_response.c` | 2 | equivalent |
| Trim-based AC correction (functions 2, 4, 16, 18, 20) | `firmware/src/trim_correction.c` | 4 | equivalent |
| Dual-readout functions 13–15, 22–24 (phase alternation, in-cycle calibration, 2-D grid correction) | `firmware/src/dual_correction.c` | 4 | equivalent over 24-step sequences |
| Peak-hold results (max/min peak registers, held peak) | `firmware/src/measure_peak.c` | 1 | equivalent |
| Counter results (capture, frequency with auto/manual range, functions 6, 7, 8, 12, counter calibration) | `firmware/src/measure_counter.c` | 15 | equivalent below the 10^9-count frequency overload (see deviations) |
| Secondary-display unit annunciators | `firmware/src/annunciators.c` | 4 | equivalent |
| Display: 7-segment font, LCD symbol setters, unit symbols per function, small display selectors | `firmware/src/display_symbols.c` | 28 | equivalent; symbol setters named by shadow word/bit until the glass map is known |
| Display: characters on both readouts, numbers, fixed texts, setup screens, temperature, burden value, dBm | `firmware/src/display_text.c` | 38 | equivalent |
| Display: 26-segment bar graph | `firmware/src/display_bargraph.c` | 7 | equivalent |
| Display: periodic update of both readouts, bar graph and symbols (`display_update`) | `firmware/src/display_update.c` | 5 | equivalent; the whole display tree is rebuilt |
| Function 12 measurement timeout (polled) | `firmware/src/measure_counter.c` | 1 | equivalent |
| Type K thermocouple table, °C → °F | `firmware/src/temperature.c` | 2 | equivalent |
| SD card driver (SDIO + DMA2, SPL structures in RAM, bounded waits) | `firmware/src/sd_card.c` | 31 incl. inlined SPL helpers | equivalent with an emulated SD card (standard/high capacity, v1, absent), including multi-block writes |
| FatFs disk I/O glue | `firmware/src/diskio.c` | 6 | equivalent |
| FatFs R0.08a | `common/fatfs/ff.c` (upstream, unmodified) | 33 | library; every API function used by the app (`f_mount`, `f_open`, `f_read`, `f_write`, `f_lseek`, `f_sync`, `f_close`, `f_opendir`, `f_readdir`, `f_stat`, `f_utime`) checked against the original on a FAT16 image |
| Interrupt handlers (faults, SysTick, SDIO/DMA, RTC wake-up, EXTI, ADC, TIM3 buzzer tick) | `firmware/src/irq.c` | 23 | equivalent (TIM2 and USART2 pending) |
| User settings in data EEPROM (function per rotary position, contrast, APO, units, beep, log interval, VA extra offsets) | `firmware/src/settings.c` | 33 | equivalent on randomised EEPROM contents |
| MEM records (60 stored readings with clock) and clock snapshots | `firmware/src/mem_records.c` | 13 | equivalent |
| BLE reading packet (19 bytes, XOR checksum) and the 1000-entry reading histories | `firmware/src/ble_packet.c` | 20 | equivalent |
| BLE commands (set clock, inject key; echoed back over USART2), setting-edit time-out | `firmware/src/ble_command.c` | 5 | equivalent |
| Message screens (SdCAL/SdSAv/SdSEt/SAvEd/-Err-/CAL1, version, segment test, power-off), recall symbols, RTC helpers (BCD, days in month, 1 s poll) | `display_text.c`, `display_update.c`, `rtc.c` and others | 22 | equivalent; `rtc_days_in_month()` returns 0 for months outside 1-12, where the original reads past its stack copy of the table |
| Power-off: circuits, timers, BLE UART, ADC and oscillators off, 65 kHz MSI in range 3, pins analog, EXTI wake-up on the rotary/key lines, STOP mode; BLE UART on (USART2 19200 baud) | `firmware/src/power.c` | 22 incl. inlined SPL helpers (USART_Init, RCC_GetClocksFreq, EXTI_Init...) | equivalent; `power_off` itself (STOP entry, restart) checked against the disassembly |
| SETUP menu items (temperature unit, APO, beep, contrast, date/time, serial number, log interval, per-function options), start-up state, RTC clock load | `firmware/src/setup_menu.c` | 16 | equivalent (invalid months excluded: documented `rtc_days_in_month` deviation) |
| Key actions: HOLD/REL/MIN-MAX values, MODE key, bargraph limits, MEM store/recall display, calibration start | `firmware/src/modes.c` | 17 | equivalent |
| Rotary switch debounce and function selection, RANGE key, BLE button events | `firmware/src/keys.c`, `firmware/src/range_select.c` | 7 | equivalent |
| ADC task: battery (channel 5, 20-sample trimmed mean, low-battery debounce) and thermistor (channel 4, 10-sample mean, internal temperature with calibration offset, type K cold-junction voltage) | `firmware/src/battery.c`, `firmware/src/temperature.c` | 10 incl. inlined ADC SPL helpers | equivalent (the emulator models ADC EOC and DR) |
| SD logging (CSV files "YYMMDDNN.csv": header, records with up to three readings, MAX/MIN summary, read-back check, file stamps) and the calibration backup `cal.bin` | `firmware/src/sd_log.c` | 47 | equivalent on an emulated FAT16 card (FatFs DIR.fn, a stack pointer, excluded) |
| Interrupts: TIM2 scheduler (task flags, log/menu/hold-off timers, BLE packet start, blink phases), USART2 (packet transmit, command receive) | `firmware/src/irq.c`, `firmware/src/ble_command.c` | 2 incl. inlined SPL helpers | equivalent |
| Auto-range and overload, auto-hold capture, hazardous-voltage and range warnings, LowZ detection, function/range set-up, current-socket check, secondary display cycling, MEM reading capture | `firmware/src/range_select.c`, `firmware/src/modes.c`, `firmware/src/sd_log.c` | 12 | equivalent (ranges limited to those the function has: the HY3131 tables are only defined there) |
| Key handlers (short and long presses of the eight keys, start-up key actions) and BLE power | `firmware/src/key_handlers.c` | 3 | equivalent |
| Start-up screen, `main`, start-up code and vector table | `firmware/src/main.c`, `firmware/src/startup.c` | 4 incl. the IAR start-up routines | `startup_screen` and `main` checked against the disassembly and by the whole-firmware comparison |
| IAR `log10`, reproduced bit-for-bit | `firmware/src/iar_math.c` | 1 | equivalent on 4000 inputs incl. NaN/inf/zero/subnormal/negative and errno |

Known deviations, all for inputs that valid firmware state never produces:
- **NaN results:** IAR's soft-float runtime produces a *negative* default NaN (0xFFC00000) where GCC's libgcc produces a positive one, and that changes sign-dependent saturation later. NaN needs corrupt calibration data. A zero gain is handled explicitly; a NaN takes a gain below about 1e-44 (for example 0/0 in `apply_gain` with g_flag_80b9 set). The composite handler tests therefore use realistic gains, and the edge-case floats are covered by the `apply_gain` tests.
- **Float divide near FLT_MAX:** IAR's `fdiv` returns ±infinity for quotients just below FLT_MAX in magnitude (dividend within ~3% of FLT_MAX), where IEEE and libgcc give the finite value. Measurement values are many orders of magnitude smaller.
- `lcd_reapply_pulse_duration()` with a contrast outside 10–17 uses 0 where the original passes an uninitialised register.
- `hy3131_write_mode_registers()` / `hy3131_set_irq_enables()` with a function index above 0x18 use zeros / AD1 where the original uses an uninitialised stack buffer.
- `smooth3()` and the moving averages are tested with their counters in range (0–3 and 0–10), which is where the firmware keeps them. A corrupted counter could divide by zero: the Cortex-M3 returns 0 for that, while C leaves it undefined.
- Frequency overload (function 6 value of 10^9 counts or more, decade 9): the original stores an uninitialised register as the frequency or reading while flagging the overload. The rebuild leaves the previous value and stores only the decimal and the flag.
- Table rows beyond the end of the original's register-table blocks read code in the original. Every function has at most 4 rows in its smallest table, and real ranges stay inside the blocks.

Timing deviation found on the meter (the emulator has no clock):
- `clock_init()`: the original clears HSEON and then tests HSERDY once, hanging if it is still set. The flag takes a few HSE cycles to clear, which the original's two SPL calls cover. GCC reads RCC_CR on the next instruction, so the first flash of the rebuild hung there (the bootloader leaves the HSE running). The rebuild waits for HSERDY to clear instead: the same on any meter the original boots on, and it still hangs if the HSE never stops.

Original quirks that are reproduced deliberately:
- `adc_init()`: the external-trigger-edge field of the original's init struct is uninitialised. It is always 0 in the start-up sequence (checked in emulation with different RAM fills).
- `rotary_pins_config()`: for a non-one-hot code the pin mask is whatever the caller left in r2. In the only non-trivial call that is `RCC->AHBENR`, which the C caller passes explicitly.

## Runtime library (identified by behaviour)

`tools/emu/identify_math.py` calls helpers in the original with test operands and compares the results with reference IEEE/integer operations. The IAR routines found are float add/sub/mul/div, int/unsigned→float, float↔double, double add/mul/div, double/float→int and float→unsigned (all saturating; see `numeric.h`), unsigned→double, signed/unsigned 64-bit divide, int64→float, flag-setting float compares, double `sqrt` and double `log10`.

In the rebuild, the basic IEEE operations are left to GCC's libgcc, which gives bit-identical results for finite operands. Where a NaN can arise from realistic data, `iar_fdiv()` reproduces IAR's negative default NaN. Function 12 is one such case: an uncalibrated (all-zero) record makes 0/0. The trim corrections are another (`trim_correction.c`): a zero reading makes 0/0 at the level-interpolation knee when the knee is 0 (trim 2 of −5000, or a first offset of −5000/−600). `iar_f2u()` reproduces IAR's saturating float→unsigned conversion: negatives give 0, values ≥ 2^32 give 0xFFFFFFFF, and NaN goes by its sign. IAR's 64-bit divides return the dividend unchanged when dividing by zero, and the Cortex-M3 hardware divide returns 0; `iar_uldiv()`, `iar_ldiv()` and `m3_udiv()` reproduce both. All three behaviours were checked by calling the original routines in emulation. Truncating conversions use `iar_d2i()`/`iar_f2i()`, which reproduce IAR's saturation. `(int)sqrt((double)n)` on 32-bit integers is replaced by an exact integer square root, which gives identical results. `log10` isn't required to be correctly rounded, so `iar_math.c` reproduces IAR's routine operation by operation (bit-identical, including errno).

## Calibration (found so far)

- `calibrate_zero_and_gain` (0x0800F7B8) averages 16 samples. It derives the zero offset as `p2 − (mean − p2)/9` and the gain as span / divisor, where the divisor depends on function and range (50000, 30000, 10000, 6000, 1000, or 30000·√2 for AC peak). It stores both through `cal_store_value` (0x08011B40).
- Calibration data lives in data EEPROM: byte reads at 0x08080000 + offset; byte writes with up to 10 retries; big-endian 16/32-bit helpers; and 0xA5 "valid" markers at offsets 0x1F and 0x26.
- `cal_address_for_range` (0x0801159C) gives the address as `table[g_function] + g_range·16`, with the table at 0x08028EE8 (26 entries; entry 25 = 0x2D0 is the AC-peak record).
- `cal_load(function, range)` (0x0801167C) loads the offset (+0) and gain (+4) into the working values. The gain is ÷1e6; for function 12 it is ÷1e5, or ÷1e4 in range 4 when PF8 reads low. Special cases:
  - index 200 loads the 16-bit value at 0x2E0 (limit ±1000);
  - function 6 loads the gain at 0x104 (40e6–60e6, default 49152050);
  - function 9 range 6 also loads 0x178;
  - dual-gain functions (13–15, 22–24) load two offset/gain pairs;
  - function 0 uses `table[1 or 2] + 0x30`, and function 10 uses `table[9] + 0x10`.
- Each record holds four signed 16-bit trims at +8. Their limits are ±20000, ±20000, ±10000 and ±10000, and an out-of-range trim reads as 0. Functions 16 and 18 always use their range-1 record.
- Functions 16–21 have an extra offset at 0x4D0 + 8·(function − 16): a 0xA5 marker followed by a big-endian int32. It is used for range 0 only.
- Trim capture (calibration mode, functions 2, 4, 16, 18, 20): trim n = reading − nominal. The nominal is 5000 for trims 1 and 3 and 50000 for trims 2 and 4; 600 and 6000 for function 2 range 3.
- The type K table (0x08025CF8) holds EMF in µV from −210 °C to +1360 °C in 10 °C steps. Readings are interpolated in 0.01 °C. Valid inputs run from −5891 µV (−200 °C) up to 54138 µV (1350 °C).

## Measurement processing (found so far)

- **Function 2 with 0x200080BA set (AC+DC):** the meter alternates between two HY3131 set-ups (0x200080BD selects the phase and the function 1 or 2 calibration record). It captures the reading after 5 readings in one phase and 3 in the other. The displayed value is √(a² + b²) through the second moving average.
- **Function 5 (temperature):** the thermocouple voltage plus a cold-junction offset (0x20007E7C) is converted to 0.01 °C, or to °F when 0x200080AB is 0, and then to 0.1°. The original rounds up when the last digit of the already-divided value is 5 or more, not the discarded digit. This is reproduced.
- **Function 9 (and 10, which adds the threshold beeper):** negative readings are clipped except in range 0. Range 5 subtracts a correction (0 to 15 counts) that rises and falls with the reading. Range 6 subtracts `g_cal_e14 × curve / 100`, where the curve is a 13-point table of percentages. A reading of exactly 0 makes the original interpolate from the word before that table; the rebuild keeps that word, so the result is identical.
- **Function 11:** a reading within ±999 keeps the buzzer on. A change of more than 1000 gives one `beep_triple()` until 0x200080B4 is cleared. Despite its name `beep_triple()` sounds two beeps in the original.
- **Functions 2 and 4 with g_flag_80b9:** the reading is scaled by a gain (1000 = unity) for the measured frequency (20 Hz–6 kHz, 216 points). There are separate tables for low (< 100000) and high (> 500000) readings, blended linearly between them. Below 20 Hz the reading is unchanged; above 6 kHz it becomes 0. At exactly 20 Hz the original interpolates from the word before the frequency table; the rebuild keeps the tables contiguous, so the result is identical.
- **Trim correction** (functions 2, 4, 16, 18, 20, outside calibration and without g_flag_80b9): the four 16-bit trims give offsets at two levels. Percentage tables per frequency point scale them for the measured frequency (above 50 Hz), and the result is interpolated in level between the knee (low offset + 5000 counts, or + 600 for selection 5, which is function 2 range 3 or function 4 range 5) and 50000. Function 2 range 0 can add a further frequency-dependent offset (g_flag_8087).
- **Functions 16–21 (VA):** outside calibration with g_flag_80a5 set, the meter alternates between the main measurement and a borrowed function, function 2 for 16/18/20 and function 1 for 17/19/21. It switches on the 7th or 4th new reading respectively. The borrowed reading is smoothed into 0x20007E6C, and 0x200080A7 records whether it is above 5000.
- **Functions 22–24:** a fixed offset by range and by the signs of a reference value and the reading is added when |reading| > 2500 (PF8 high, g_flag_7fe0 set).
- **Functions 13–15 and 22–24 (two readings):** these alternate between a reference reading and a level reading (phase 0x200080C4). Functions 13–15 take them via functions 2 and 16, and functions 22–24 via functions 1 and 17. Each phase discards 3 or 6 readings after a switch. Each reading auto-ranges independently with its own range bit (0x200080C6/C7): up above 55000, down below 3200 or 4000 (11000/400 for functions 15/24 in the upper range). The range is then `sel_code()` of the two bits. The product of the two readings is computed after every pair. In calibration the two offsets or gains are averaged over 3 cycles inside the dispatcher.
- **Reference correction for those functions:** a bilinear interpolation in 9×8 grids (one per function, range and PF8 state), indexed by level band and reference band. Level band 8, used when 0x200080CC doesn't match the function, reads row 9, which is the first row of the next grid. Function 15 ranges 2/3 (PF8 high) use 7-word rows at offsets inside another grid. The rebuild keeps all grids as one block addressed by flash offset, so these reads are identical.
- The second moving average is used with up to 20 samples (function 9 range 6). Its buffer at 0x20007238 has 400 bytes free before the next variable, so the rebuild declares 20 entries.

## Counter functions (found so far)

- Each counter interrupt captures CTA (0x15), CTB (0x18) and CTC (0x1B) and restarts the counter. The reference count is CTC + (0x1000000 − preset·256), where the preset is at 0x2000001C.
- The function 6 value is gain·CTB·10/(reference/10). It is divided (rounded) by 10^decade and either shown (function 6) or stored as the secondary frequency (functions 0, 2, 4, 16, 18, 20). Auto-range moves up at each decade and down at 80 % of it. Manual range (0x20008042 outside calibration) flags an overload instead.
- Function 6 calibration sets gain = reference·150000000/(CTB·100).
- Function 7 = reference·100000/(gain/1000·CTB) · (CTA per ms of reference)/1000, over ranges 0–2.
- Function 8 = CTA per ms of reference.
- Function 12 = CTA/CTB (×100 in ranges 2–4), less the zero offset in ranges 0/1, divided by the gain. It is averaged with a window of 1/50 of the value (1/10 in range 5), at least 20. Readings below 21 in ranges 2–4 read 0. In range 4 with manual range, 0x200080A4 readings are held back.

## Data EEPROM settings (found so far)

Offsets are from 0x08080000. Out-of-range contents fall back to the defaults shown.

| Offset | Contents |
|---|---|
| 0x01–0x02 | serial number prefix YYMM (default 1708), sent over BLE |
| 0x03–0x06 | serial number (0–99999), sent over BLE; edited in the setup menu |
| 0x08 | function at rotary position A: 1 or 2, 0x82 = 2 with AC+DC |
| 0x09 / 0x0A / 0x0B | functions 3–5 / 6–8 / 9–12 |
| 0x0C | 15 or 24 |
| 0x0D | 13, 16, 17 or 22 |
| 0x0E | 18–21, read back as 18 (18/20) or 19 |
| 0x10 | temperature unit (1 = °C) |
| 0x12 | diode test voltage (0 = 3 V, 1 = 15 V) |
| 0x14 | 0x200080B6 selection (0–3; "DN 30"/"UP 30"/"DN300"/"UP300") |
| 0x16 | auto power-off disabled |
| 0x18–0x19 | log interval, seconds (1–999) |
| 0x1A | LCD contrast (12–17, default 14) |
| 0x1B | 0x200080A8 flag |
| 0x1E | beep enabled |
| 0x26 | 0xA5 once calibrated |

## BLE protocol (found so far)

Reading packet, sent after each reading (`ble_build_packet`, 0x0800C5DA):

| Byte | Contents |
|---|---|
| 0 | 0xF2 |
| 1-4 | serial number, packed BCD: YYMM prefix (EEPROM 0x01) and 5-digit number (EEPROM 0x03) |
| 5 | main function; +0x80 / +0x40 for bits 17 / 16 of the magnitude |
| 6 | main range; +0x80 overload, +0x40 negative, +0x10 (F) / +0x20 (C) for temperature; diode 15 V reports range 1 |
| 7-8 | main value magnitude, big-endian (low 16 bits) |
| 9 | secondary type: 0x64/0x69 temperature C/F, 0x6E battery, 0x78/0x7D APO on/off, 0xA0 contrast, 0x82 year, 0x87 date, 0x8C time, 0x06 frequency, 0x0B diode voltage, 0xB4 dBm, 0x96 burden, 0x9B/0x9C bd on/off, 0x01/0x02/0x10-0x15 dual readings, 0xAA-0xAD for 0x200080B6 |
| 10 | secondary flags: decimal point, +0x80 overload/open, +0x40 negative (frequency: +0x10, +0x20 above decade 1) |
| 11-12 | secondary magnitude, big-endian |
| 13 | bar graph state; +0x10 functions without bar graph, +0x04 negative, +0x08 hidden |
| 14 | bar graph length |
| 15-17 | symbol bits (AC/DC, auto range, APO, hold/peak, partial-update mode, logging) |
| 18 | XOR of bytes 0-17 |

Commands from the app (`ble_handle_command`, 0x0800C332), ASCII hex after a command byte:
- 0xF8 + six bytes (clock registers 2, 4, 6, 10, 12, 14) + their XOR: sets the clock, once per power-up.
- 0xF4 + key code twice: injects that key event.
Accepted commands are echoed back over USART2.

## Display (found so far)

- The LCD has ten 7-segment digits. Digits 0–4 are the **secondary** readout and digits 5–9 the **main** readout; the manual confirms this, since the internal temperature, burden voltage, diode test voltage and log count all appear on the secondary display. Each digit's segment code holds two segment columns per COM row (bits 7–6 COM0 … 1–0 COM3). In COM0 the bit-6 column is the decimal point to the left of the digit, which the font never sets. `lcd_set_secondary_dp()` (0x08017FB4) and `lcd_set_main_dp()` (0x0801717E) drive the decimal points. Earlier parts had these two names the other way round.
- Minus signs: shadow[4] bit 29 (secondary), shadow[3] bit 7 (main).
- The bar graph has 26 segments in SEG1–SEG7 (rows zig-zag). Its length is |value|/400 or /2000 depending on the range, and it is full on overload.
- `display_update()` (0x08016D34) runs from the main loop. It redraws whatever was flagged: the main readout on a new reading (`g_new_reading`), the secondary readout (`g_disp_flag_8062`), the bar graph (`g_scaled_ready`), the symbol groups (`g_flag_8060`, `g_flag_8061`, `g_flag_804c`) and the secondary annunciators when `g_mode_8037` changes. It then calls `lcd_update()` once. Nothing is drawn while the setup menu is active.
- `g_mode_8037` selects what the secondary readout shows: 0 temperature, 1 battery ("BATnn"), 2 APO setting, 3 beep setting, 4 LCD contrast, 5 year, 6 date pair, 7 time pair, 8 an edited value, 9 log interval ("Innn"), 10/11/12 per-function auxiliary values (burden value, dBm, frequency, dual readings, diode voltage, "bd" settings), and 100 the log count.
- Texts found: " LEAD", "-----", " OFL ", " OPEN", " CAL ", "LoBAt", "SdEnd", "SdEnn", "burdn", "Lcd-n", "APO.oN/oF", "b-On/b-OFF", "bd.On/bd.OFF", "DN 30/UP 30/DN300/UP300", "OFF-E", "G-END", "400-E", "5KEND", and "-x-nn" (averaging/calibration step counters).

## Test harness notes

- `tools/emu/test_app_functions.py --shard i/n` runs every n-th spec; the full suite is run as 16 parallel shards.
- Spec keys added for the SD/FatFs work: `prep` (original functions run on both machines first, for example card initialisation and `f_mount`), `regs` (peripheral registers preset in the model, for example NVIC enables and VTOR so the original SDIO/DMA interrupt handlers run), `ignore` (RAM that legitimately differs, such as a FatFs `DIR.fn` pointer into the caller's stack) and stack arguments beyond r0–r3.
- The emulator now models SD multi-block writes (ACMD23, CMD25, CMD12 and DMA to the card) and compares the card contents of both runs.
- FatFs is linked into the hybrid image unmodified; its two static variables (`FatFs[]`, `Fsid`) are placed at the original addresses 0x20007C58 and 0x20007F98 by `firmware/hybrid.ld`.

## Start-up summary

- `SystemInit` (before C start-up) briefly runs the 16 MHz HSE PLL, with a 0x5000-iteration HSE timeout.
- `system_init()`, called from `main`:
  1. Handles wake from standby (flag in 0x20007FDE).
  2. Forces the BOR level to 0.
  3. Sets voltage range 1, then switches to **MSI 4.194 MHz with HSE and LSE off and HSI on** (for the ADC).
  4. Clocks the RTC/LCD from LSI and enables debug in low-power modes.
  5. Sets up board GPIO: control outputs, HY3131/RTC buses, rotary/key pins with pull-ups, ADC inputs PA4/PA5/PF11/PF13/PF14.
  6. Initialises ADC1 (12-bit, single, software trigger), the NVIC (group 2: SDIO and DMA2 channel 4 at 0/0, TIM3 at 1/1, TIM2 at 2/2), **TIM2 ≈ 99.8 Hz** (scheduler) and **TIM3 ≈ 1 kHz**.
- `front_end_select(function)` drives PD0, PF4, PF5, PC11 and PD7 per measurement function (100 = idle).

## HY3131 configuration

- Each measurement function index (0–0x18) and range selects a 20-byte image of HY3131 registers 0x20–0x33 (R20 counter control … R33 oscillator) from tables in flash. The data is generated into `firmware/src/hy3131_tables.c` by `tools/gen_hy3131_tables.py`.
- Some functions select rows by range, others by 0x200080C6/C7 under a flag at 0x200080C4.
- INTE per function: AD1 only for most functions; AD1 + counter for functions 13–16, 18 and 20; counter only for 6–8 and 12; RMS + counter for 2/4 when 0x200080B7 = 1.
- The counter restart presets CTA from 0x2000001C (default 0xE000).
- `hy3131_service` reads registers 0x00–0x33 in one burst and dispatches to four result handlers still to be reconstructed: AD1 0x080061E4, RMS 0x08008208, peak 0x0800A7FC, counter 0x0800AA00.

## Deviations available by option

The default build reproduces the original, bugs included. Two CMake options (off by default, see the README) change behaviour. Each change sits under `#if APP_FIX_BUGS` or `#if APP_AUTORANGE_FAST`, so the default preprocessed code is the original.

`APP_FIX_BUGS`:

- **MEM recall re-processing** (`main.c`, `modes.c` `menu_exit`). While the MEM-recall menu is open, `display_update()` returns early and nothing clears `g_new_reading`. The main loop therefore runs `measure_postprocess()` (and `lowz_detect()` in function 0) on the same reading on every pass. Auto-range collapses to range 0 or hunts between 0 and 1, the hazardous-voltage beep never sounds, MIN/MAX counts one reading thousands of times, REL subtracts the reference until OL, and LowZ detection toggles. With the fix the main loop clears the reading after post-processing it while the menu is open. `menu_exit()` requests a redraw (`g_flag_805f`) instead of a new reading, so leaving the menu (or any other caller) does not apply REL twice or add a MIN/MAX sample. In HOLD and auto-hold it requests nothing, which matches what the original showed for a steady input.
- **Fake reading after every reconfiguration** (`display_state.c` `measure_display_reset`, `range_select.c` `function_change`). `function_change()` zeroes `g_derived_value`, and `measure_display_reset()` sets `g_new_reading`, so the zero is post-processed once as if it were a reading. It uses up one of the `g_setting_238` readings that `autorange()` skips while the HY3131 settles. The voltage warnings run on it, and `lowz_detect()` gets a quiet reading. If an AD1 sample arrives first, the dual and VA dispatchers (`dual_dispatch()`, `va_phase_step()`) count that raw sample as one of the readings they discard after a change. As a result only N−1 real readings are skipped. For function 3, function 9 ranges below 5 and function 12 ranges up to 5 (N = 1), none are. The first average after a change includes the settling, is displayed and used for range decisions, and is seeded into `moving_average_b`. With the fix the reset requests a redraw: `g_flag_805f` unless HOLD or auto-hold is on (as `function_change()` itself does), and `g_flag_8061` unless auto-hold is on. Of what the fake reading's post-processing did, only the overload state is kept. Outside auto-hold, `function_change()` clears `g_flag_8046` for the functions `autorange()` clears it for and updates `g_flag_8048`/`g_flag_8049`.
- **Stale bargraph limit after dual-function auto-range** (`range_select.c` `range_change`). `range_change()` did not call `bar_limit_update()`, so after functions 13–15/22–24 auto-range, `g_bar_limit` (used by auto-hold and the MIN/MAX bargraph) kept the old range's value. Functions 15/24 have different limits by range.
- **AC+DC magnitude wraps** (`measure_derived.c` `vector_magnitude`). a² + b² was summed in 32 bits, so magnitudes above 65535 wrapped (a = b = 47000 read 11092 instead of 66468, in range, no OL). The sum is now 64-bit, with an exact 64-bit integer square root above 2^32.
- **Calibration step survives a function/range change** (`range_select.c` `function_change`). The step flags (`g_avg_flag_808c`, `g_cal_flag_808e`, `g_flag_8090`) and the sample count/sums were only cleared on completion or by `cal_start()`, so a partial sum from the old range was stored in the new range's record. `function_change()` now abandons any step in progress (this includes the function 6–8 counter time-out, which reconfigures through it).
- **Non-positive gain stored** (`calibration.c` `cal_store_value`, `cal_store_dual`). A botched gain step (zero or negative span) wrote a zero or negative gain, and a negative one reloads as a huge positive gain (the record is read as unsigned). Such a gain (≤ 0, or saturated at INT32_MAX once scaled) is refused with `beep_triple()` and the old record kept; the offset of a combined zero-and-gain step is still stored, and loading is unchanged. `gain_select_for_range()` has already put the refused gain in the working gain (`g_gain`, or `g_gain_e2c`/`g_gain_e30`), so the refusal also reloads the stored record (`cal_load(g_function, g_range)`) and readings go on with the gain that is in EEPROM.
- **Function 12 division by a zero gain** (`measure_counter.c` `counter_process_func12`). Ranges 2–5 divided by `g_gain` without the zero check ranges 0/1 have, so an uncalibrated record read 0xFFFFFFFF; the same guard now applies.
- **REL off in HOLD** (`key_handlers.c` `key_rel`). REL on, then HOLD, then REL off showed the held relative value as if it were absolute. It also restored the pre-REL range mode while HOLD was on; leaving HOLD then restored the mode HOLD had saved, which was manual (set by REL), so the range stayed manual. REL off is now refused with `beep_triple()` while HOLD is on, as RANGE is; it still works in auto-hold. REL on in HOLD is unchanged.
- **No warning symbol on overload in the lowest voltage range** (`modes.c` `voltage_warnings`). The warning symbol (`g_flag_80b1`) on overload excluded range 0 of functions 1/2, so a range held at 5 V (manual, MIN/MAX, REL, HOLD) read OL on mains with no warning. It is now set on overload in every range; the hazardous-voltage beep (`g_flag_80b2`) is unchanged. This is deliberately conservative: in range 0 the symbol lights for any overload, including 5.5–30 V in a range held at 5 V, which is not hazardous.
- **Range kept past the new function's range count** (`range_select.c` `function_change`). With PEAK on, a dial change between functions 2 and 4 kept `g_range` (VAC range 2/3 → ACmV, which has 2 ranges), so the HY3131 was set from another function's table rows and the wrong calibration record was loaded. On a function change, a range beyond the function's count is now reset to 0.
- **Clock drawn from the timer interrupt** (`irq.c` TIM2). In secondary mode 7 the TIM2 interrupt called `show_time_pair(1)`, which shares `g_digits`/`g_digit_text` and the LCD shadow with the main loop and could corrupt a readout being drawn. It now only requests the secondary readout (`g_disp_flag_8062`); `display_update()` draws the same pair in the main loop.
- **Held key wraps into a new press** (`keys.c` `key_scan`). After a long press the uint8 hold counter kept counting from 0x33 and wrapped to 0 after 205 more scans, so a held key gave a second long press or a short press on release. It now stops at 0x33 until the key is released.
- **PEAK during logging or HOLD** (`key_handlers.c` `key_peak`, also the BLE PEAK event). PEAK sets the function up again (`g_flag_804a`), which ended logging and, in HOLD, blanked the readout with HOLD still lit. PEAK is now refused with `beep_triple()` while logging or in HOLD. The current-socket auto-switch (`modes.c` `input_jack_check`) still changes function, but now clears HOLD first, as the rotary switch does.
- **Auto range in PEAK** (`key_handlers.c` `key_long_press_handler`, case 0x10). PEAK (functions 2/4) reads through the HY3131 RMS path and sets the range manual (`key_peak`), but a long RANGE press cleared `g_manual_range` regardless, so `autorange()` acted on the PEAK readings. A long RANGE press is now refused with `beep_triple()` while PEAK is on (outside calibration mode).
- **1 kHz filter in HOLD** (`key_handlers.c` `key_long_press_handler`, case 0x40). A long REL press in functions 2/4 sets the function up again (`g_flag_804a`), which in HOLD blanked the readout with HOLD still lit, as PEAK did. It is now refused with `beep_triple()` in HOLD. While logging it was already refused in the original.
- **MIN/MAX with only overloads** (`modes.c` `minmax_value`). With only negative overloads the maximum stayed INT32_MIN and the overload flag was cleared, so INT32_MIN was drawn (and REL subtracted from it); with only positive overloads the minimum stayed INT32_MAX. These now read as −OL / OL.
- **MEM opens recall over a setting edit** (`key_handlers.c` `key_mem`). While a setting was being edited (`g_flag_806e`), MEM opened the recall menu and REL/PEAK kept stepping the setting. MEM is now refused with `beep_triple()` then.
- **`beep_triple()` sounds twice** (`buzzer.c`). Its count of 3 is on/off phases after the first beep (TIM3 tick in `irq.c`), which gives two beeps; the count is now 5.
- **SD addresses above 4 GiB** (`diskio.c` `disk_read`, `disk_write`). The byte address `sector * 512` is formed in 32 bits. On an SDHC card the driver divides it by 512 to get the block number, so every sector from 4 GiB up maps back to the start of the card, and a log write there overwrites the MBR, FAT or root directory. With the fix the address is formed in 64 bits. Standard-capacity cards (2 GB at most) were not affected.
- **SD errors ignored** (`diskio.c`, `sd_card.c` `sd_wait_read_operation`). `disk_read()` and `disk_write()` ignore what `sd_read_block()`, `sd_write_multi_blocks()` and `sd_wait_write_operation()` return. Only a card that never returns to transfer state is reported, so a data CRC error, a DMA underrun or a failed command reads as success. With the fix those errors return `RES_ERROR`, and a read waits for the SDIO data end and the DMA to finish (polled, at most 0x100000 times) and returns the transfer status. The read wait does not touch `g_sd_dma_end`: `sd_wait_write_operation()` depends on the state the original leaves it in. A write error that persists now ends logging: `log_flush()` retries 20 times, 20 more with a fall-back file, then logging stops with `SdE 7` (write) or `SdE 8` (read-back). Errors the original ignored now count, so run a long logging session on the meter with `APP_FIX_BUGS` before relying on it.
- **Log read-back mismatches never reported** (`sd_log.c` `log_verify`, `log_flush`). On a mismatch `log_verify()` carries on without advancing, ends on a zero-length read and returns 0, so only short reads were ever reported. With the fix a mismatch returns 8. `log_flush()` then moves the write position back, as the original intends. The original also takes 60 off the record count, but the records are no longer pending (`g_value_7d44` is 0), so the retry writes nothing, the block is lost and the next block overwrites it. With the fix the record count, the pending count and `g_log_blocks` are put back, so the retry writes the same records again (they stay in the MEM arrays during `log_flush_retry()`). A new file also starts its read-back at its own start. The original keeps the previous file's `g_log_start_pos` when no header is written (the fall-back file after 20 failures), so that read-back always came back short. A mismatch that persists ends like any other flush failure: 20 retries, 20 more with a fall-back file, then logging stops with `SdE 8`.
- **Calibration restore takes any `cal.bin`** (`sd_log.c` `cal_backup_restore`). "SdSEt" copies the first 0x4B0 bytes of any file of at least that size over EEPROM 0x000–0x4AF, which holds the calibration, with no check and no way back. With the fix the file must be exactly 0x4B0 bytes (what "SdSAv" writes), have the calibrated marker (byte 0x26 = 0xA5), and carry this meter's serial number (bytes 0x01–0x06) unless this meter's own EEPROM has no calibrated marker. Otherwise it returns 10, nothing is written, and the caller shows the usual error. If the written copy does not read back, the previous contents (read before writing) are written back where they differ, and it returns 9.
- **Log file 99 wraps to 00** (`sd_log.c` `log_find_index`, `log_create`, `log_flush`, `log_flush_retry`). When today's highest log file is `YYMMDD99.csv`, the next index wraps to 0 and `log_open()` creates `YYMMDD00.csv` with `FA_CREATE_ALWAYS`, emptying it, and the same happens for every later session that day. Names whose `NN` is not two digits give arbitrary indices (often over 99, so also 00). With the fix only names that are eight digits are counted. When 99 is taken, logging does not start: `log_flush()` returns 6 at once (no retries, and no fall-back name, which would be built from the previous file's name), the main loop stops logging and the secondary display shows `SdE 6`.
- **BLE clock set not range-checked** (`ble_command.c` `ble_handle_command`). The 0xF8 command writes any six values to the RTC (as BCD). With the fix the command is applied only for year 0–99, month 1–12, day 1 to the length of the month (`rtc_days_in_month()`), hour 0–23 and minute/second 0–59. An invalid command is treated as one with a bad checksum: it is not echoed or applied, and a later valid command can still set the clock.
- **Sticky flash error flags** (`eeprom.c` `eeprom_program_byte`). The FLASH_SR error flags are cleared only at start-up. After one failed EEPROM write, `flash_wait_for_last_operation()` reports the old error before every later write, so all later writes (settings, calibration, markers) fail silently. With the fix all flags are cleared, as at start-up, before each attempt, so the 10 retries of `eeprom_write_byte_retry()` mean something.
- **Flash left unlocked** (`flash_ob.c` `flash_lock_all`, `system_init.c` `system_init`). The original bootloader unlocks program memory and the data EEPROM and starts the application that way. The application only sets PELOCK after its first EEPROM write and never sets PRGLOCK, so until then a stray store can program the data EEPROM. With the fix `system_init()` sets PRGLOCK and OPTLOCK, then PELOCK, right after the brown-out option check. EEPROM writes unlock PELOCK as before. Nothing in the application programs program memory or option bytes after that point. The bootloader's own fix is `BL_FIX_BUGS` (see `docs/bootloader.md`).

Not an option: `g_sd_buffer` (0x20001240) is now declared with the 0x2710 bytes of free RAM it runs into (up to `g_ble_history_main` at 0x20003950) instead of 0x4B0, because `log_write()` fills it with a whole flush (header or up to 60 records, several KB). Only the declared type changes, and both default images stay bit-equivalent. `SD_BUFFER_SIZE` (0x4B0, the EEPROM image) is unchanged.

`APP_AUTORANGE_FAST` (`range_select.c` `autorange`), only for functions 1, 2, 3, 4, 9 and 16–21, whose ranges are all a decade apart in counts. Other functions, including 12, keep single steps. It turns `APP_FIX_BUGS` on (`firmware/CMakeLists.txt`, with a status message): without it the fake reading after a range change uses up the only skipped reading of function 3, function 9 ranges below 5 and function 2 AC+DC (`g_setting_238` = 1), so a multi-range step could act on an unsettled first average and drop about three decades. A build with only `APP_AUTORANGE_FAST` is therefore the same image as one with both options.

- **Down:** to range r−k, for the largest k ≥ 1 with (|reading| + |reading|/8 + `AUTORANGE_DOWN_NOISE`) × 10^k ≤ 90 % of the bargraph limit of range r−k. `AUTORANGE_DOWN_NOISE` is 5 counts, which covers the offset and noise of the coarse higher-range reading.
- **Up:** to the top range when the reading comes from AD1 of the current set-up and |`g_ad1_average`| ≥ `AUTORANGE_AD1_RAIL` (0x7F0000, a placeholder until the clip level is measured on the meter). That is functions 1, 3 and 9, and functions 2 and 4 unless PEAK (`g_flag_80b7`: RMS and counter, AD1 not updated) or, for function 2, AC+DC (`g_flag_80ba`: alternating phases) is on; not the VA functions 16–21, which borrow the function 1/2 phases. Otherwise to range r+k for the smallest k ≥ 1 with |reading| / 10^k ≤ the limit of range r+k (100 %, not 90 %: a reading at 90–100 % of that limit fits, and one range further it would read below 10 % and step back down), capped at the top range. A clipped reading that is not caught underestimates the input, which only costs another step later.
- All existing guards are kept: MIN/MAX, manual range, the range count, HOLD and auto-hold, the function 9 step blocking (`range_step_blocked()`) and the function 9 range 6 auto-hold return. `function_change()` sets up the final range in one go, so a jump costs one HY3131 reconfiguration and one set of skipped readings.

Not changed: the calibration steps (`calibration.c`) set `g_new_reading` to show their end texts, which post-processes the last value again. `autorange()` does nothing in calibration mode, so this was left alone. In `dual_dispatch()`, the reading that changes the range is still post-processed with the single phase's value in `g_derived_value` (the display shows dashes until the next product).

## Findings from the reconstruction so far

- **Keys:** PG8–PG15 are the keys (`g_keys_raw` bit n = PG(8+n)). PG0–PG7 are the rotary switch (`g_rotary_raw` bit n = PG(7−n)).
  - Debounce is two equal samples.
  - A long press fires after 50 scan ticks. It applies to single keys and the PG11+PG13 combination (the bootloader's update keys).
  - PG14/PG15 auto-repeat in the menu: after 3 ticks in the menu (50 elsewhere), then 5 repeats at 30 ticks, then every 10 ticks.
- **Settings in initialised data:** 0x2000022C log interval (1 s), 0x2000022E the 1 kHz countdown used by the start-up screen (500), 0x20000230 auto power-off (1800 s), 0x20000237 LCD contrast (14).
- **LCD:** the application drives three more segment pins than the bootloader (PA15, PB3, PB4). The display shadow is 16 words at 0x20007790. Numbers go through `number_to_digits` → `digits_to_text` (signed digits, leading-zero blanking) → glyph rendering (still to be reconstructed).
- **RTC:** writes send address + 1, values are BCD, and the date/time registers are 2, 4, 6, 10, 12, 14.
- **Keys:** 0x01 MODE, 0x02 SETUP, 0x04 MIN/MAX, 0x08 MEM, 0x10 RANGE, 0x20 HOLD, 0x40 REL, 0x80 PEAK. Long presses: MODE backlight (PE8), SETUP edit/store the setting shown, MIN/MAX off, MEM logging on/off, RANGE back to auto, MEM+HOLD logging in auto-hold mode, REL 1 kHz filter (functions 2/4) or VA zero offset, PEAK BLE on/off. The BLE app's button events 1..8 (0x81..0x88 long) map to RANGE, HOLD, REL, PEAK, MODE, MIN/MAX, MEM, SETUP.
- **Start-up keys** (version screen, ~2 s): MODE then REL enters calibration mode; MEM shows "SdCAL", then REL (save) or PEAK (restore) and MODE to confirm copy EEPROM bytes 0..0x4AF to/from `cal.bin`; SETUP makes the serial prefix follow the clock; RANGE lights all segments.
- **HOLD pressed twice** enters auto-hold (0x20008050): after the input drops below 1000 counts, the next reading that stays within 20 counts on two updates (and differs by more than 400 counts from the held one) is captured with a short beep and logged when logging.
- **Quirks kept as in the original:**
  - In calibration mode SETUP starts calibration step 0 and then falls into the MEM-key code, so a successful start is answered with `beep_triple()` (two beeps in the original).
  - The log read-back (`log_verify`) keeps reading after a mismatching block without advancing its buffer position and ends with success unless a read fails.
  - `rtc_days_in_month` reads past its table for months outside 1..12 (the rebuild returns 0 there).
- **IRQ-shared variable:** the start-up countdown 0x2000022E must be `volatile` in C (the original re-reads it while polling; GCC would otherwise keep it in a register).
