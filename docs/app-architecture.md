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
