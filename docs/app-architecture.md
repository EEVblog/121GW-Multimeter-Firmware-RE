# Application (v2.05) — architecture map

Image: `dumps/app.bin` = flash 0x08006000–0x080294E7 (144,616 bytes), byte-identical to `EEVBlog2_05.bin`. Status: analysis in progress (M3).

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

When every function is reconstructed, the globals are defined normally and the hybrid bindings disappear.

## Reconstruction status

`python -P tools/coverage.py app` → `docs/coverage_app.csv`.

| Module | Source | Functions | Status |
|---|---|---|---|
| Keys and rotary switch | `firmware/src/keys.c` | 9 | equivalent |
| Board I/O (delay, GPIO_Init) | `firmware/src/board_io.c` | 2 | equivalent (GPIO via callers) |
| HY3131 serial bus, PF8 detect | `firmware/src/hy3131_bus.c` | 6 | equivalent |
| NJU6350 RTC | `firmware/src/rtc.c` | 11 | equivalent |
| LCD driver | `firmware/src/lcd_driver.c` | 12 | equivalent |
| Start-up (clocks, power, GPIO, ADC, NVIC, timers) | `firmware/src/system_init.c` | 15 | equivalent, including BOR-set, wake-from-standby and no-HSE variants |
| Flash/option-byte helpers | `firmware/src/flash_ob.c` | 8 | equivalent (tested directly or via `system_init`) |
| HY3131 control (mode registers, IRQ enables, counter, service, poll) | `firmware/src/hy3131_ctrl.c`, generated `hy3131_tables.c` | 6 | equivalent for valid function/range indices |

| RMS results | `firmware/src/measure_rms.c` | 7 | equivalent |
| Measurement scaling (zero offset, gain, 16-sample mean, gain divisors) | `firmware/src/measure_scale.c`, `firmware/include/numeric.h` | 11 | equivalent for finite, realistic float values |
| Data EEPROM access (read, program with retries, BE writes, markers) | `firmware/src/eeprom.c` | 11 | equivalent, including EEPROM write traces |
| Calibration storage and zero/gain calibration | `firmware/src/calibration.c` | 5 | equivalent |
| AD1 results (24-bit read, running mean, per-function dispatch) | `firmware/src/measure_ad1.c` | 4 | equivalent; per-function processing still calls the original |
| Front-end switching per function | `firmware/src/front_end.c` | 8 | equivalent |
| Buzzer (PE7), beep presets, auto power-off reset | `firmware/src/buzzer.c` | 6 | equivalent |
| Measurement helpers, calibration stages | `firmware/src/measure_scale.c`, `firmware/src/calibration.c` | 7 | equivalent |
| Derived values (power/VA product, vector magnitude, thresholds, hold-off, dBm) | `firmware/src/measure_derived.c` | 6 | equivalent (dBm for ranges 0–3) |
| IAR `log10`, reproduced bit-for-bit | `firmware/src/iar_math.c` | 1 | equivalent on 4000 inputs incl. NaN/inf/zero/subnormal/negative and errno |

Known deviations, all for inputs that valid firmware state never produces:
- **NaN results:** IAR's soft-float runtime produces a *negative* default NaN (0xFFC00000) where GCC's libgcc produces a positive one, and that changes sign-dependent saturation later. NaN needs corrupt calibration data; an erased STM32L1 EEPROM reads as zeros.
- **Float divide near FLT_MAX:** IAR's `fdiv` returns ±infinity for quotients just below FLT_MAX in magnitude (dividend within ~3% of FLT_MAX), where IEEE and libgcc give the finite value. Measurement values are many orders of magnitude smaller.
- `lcd_reapply_pulse_duration()` with a contrast outside 10–17 uses 0 where the original passes an uninitialised register.
- `hy3131_write_mode_registers()` / `hy3131_set_irq_enables()` with a function index above 0x18 use zeros / AD1 where the original uses an uninitialised stack buffer.
- Table rows beyond the end of the original's register-table blocks read code in the original. Every function has at most 4 rows in its smallest table, and real ranges stay inside the blocks.

Original quirks that are reproduced deliberately:
- `adc_init()`: the external-trigger-edge field of the original's init struct is uninitialised. It is always 0 in the start-up sequence (checked in emulation with different RAM fills).
- `rotary_pins_config()`: for a non-one-hot code the pin mask is whatever the caller left in r2. In the only non-trivial call that is `RCC->AHBENR`, which the C caller passes explicitly.

## Runtime library (identified by behaviour)

`tools/emu/identify_math.py` calls helpers in the original with test operands and compares the results with reference IEEE/integer operations. The IAR routines found are float add/sub/mul/div, int/unsigned→float, float↔double, double add/mul/div, double/float→int and float→unsigned (all saturating; see `numeric.h`), unsigned→double, signed/unsigned 64-bit divide, int64→float, flag-setting float compares, double `sqrt` and double `log10`.

In the rebuild, the basic IEEE operations are left to GCC's libgcc, which gives bit-identical results for finite operands. Truncating conversions use `iar_d2i()`/`iar_f2i()`, which reproduce IAR's saturation. `(int)sqrt((double)n)` on 32-bit integers is replaced by an exact integer square root, which gives identical results. `log10` isn't required to be correctly rounded, so IAR's routine will be reconstructed itself when its users are.

## Calibration (found so far)

- `calibrate_zero_and_gain` (0x0800F7B8, not yet rebuilt) averages 16 samples. It derives the zero offset as `p2 − (mean − p2)/9` and the gain as span / divisor, where the divisor depends on function and range (50000, 30000, 10000, 6000, 1000, or 30000·√2 for AC peak). It stores both through `cal_store_value` (0x08011B40).
- Calibration data lives in data EEPROM: byte reads at 0x08080000 + offset; byte writes with up to 10 retries; big-endian 16/32-bit helpers; and 0xA5 "valid" markers at offsets 0x1F and 0x26.
- `cal_address_for_range` (0x0801159C) gives the address as `table[g_function] + g_range·16`, with the table at 0x08028EE8.

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

## Findings from the reconstruction so far

- **Keys:** PG8–PG15 are the keys (`g_keys_raw` bit n = PG(8+n)). PG0–PG7 are the rotary switch (`g_rotary_raw` bit n = PG(7−n)).
  - Debounce is two equal samples.
  - A long press fires after 50 scan ticks. It applies to single keys and the PG11+PG13 combination (the bootloader's update keys).
  - PG14/PG15 auto-repeat in the menu: after 3 ticks in the menu (50 elsewhere), then 5 repeats at 30 ticks, then every 10 ticks.
- **Settings in initialised data:** 0x2000022C log interval (1 s), 0x2000022E an unidentified key-timing setting (500), 0x20000230 auto power-off (1800 s), 0x20000237 LCD contrast (14).
- **LCD:** the application drives three more segment pins than the bootloader (PA15, PB3, PB4). The display shadow is 16 words at 0x20007790. Numbers go through `number_to_digits` → `digits_to_text` (signed digits, leading-zero blanking) → glyph rendering (still to be reconstructed).
- **RTC:** writes send address + 1, values are BCD, and the date/time registers are 2, 4, 6, 10, 12, 14.
