# Custom SD-card bootloader

Source: `bootloader/` · Original: `dumps/flash.bin` 0x08000000–0x0800514B (20,812 bytes, IAR EWARM build)

## What it does

1. **Start-up.** `SystemInit` resets the RCC and tries the 4 MHz HSE crystal through the PLL (×12 ÷3 = 16 MHz, voltage range 1, one wait state). `main()` then switches SYSCLK to the HSE directly, so the bootloader runs at **4 MHz**. Without a crystal it stays on MSI.
2. **Option bytes.** If the brown-out reset level (BOR_LEV) is anything but 0, it writes BOR_LEV = 0 and relaunches the option bytes. That resets the MCU, and the next boot continues normally. This is why every meter reads BOR_LEV 0.
3. **Hardware set-up.** GPIO (all of port G as key inputs with pull-ups, plus fixed initial levels on ports D/E/F), NVIC, TIM2 (~24.8 Hz tick), and the LCD (1/4 duty, 1/3 bias, clocked from LSI via the RTC clock). All segments are lit.
4. **Boot decision.** It reads the keys once (one IDR read per key):
   - **Normal:** if the update combination isn't held, it jumps to the application. If the word at 0x08006000 looks like a RAM stack pointer (`& 0x2FFE0000 == 0x20000000`), it loads MSP from it and calls the reset vector at 0x08006004. Otherwise it spins.
   - **Update mode:** entered when **PG11 and PG13 are held** and PG8/9/10/12/14/15 are released. The display shows `-IAP-` on both rows, and it waits for **PG9** for up to 200 TIM2 ticks (**about 8 s**). On timeout it boots the application.
5. **Update.** When PG9 is pressed:
   - TIM2 stops, the main row shows `douun`, the bargraph resets, and PF15 is driven low.
   - The SD card is initialised (SDIO, 1-bit bus, DMA2 channel 4), FAT is mounted, and `EEVBlog.bin` is opened from the root directory.
   - It erases every 256-byte page from 0x08006000 to 0x0805FF00, then reads the file in 512-byte chunks and programs it in 128-byte half pages (zero-padded) from a routine running in RAM, verifying each half page.
   - The bargraph advances in 25 steps.
   - Afterwards it shows ` Good` or ` FAIL`, closes the file, powers the card off and **immediately boots the application**.
   - If the card or file can't be opened, it powers the card off and waits for PG9 again with no timeout, because TIM2 is stopped.

The manual's procedure (hold two buttons at power-on, "IAP" appears, press a third) matches this. The keys are shown as icons in the manual, so the names of PG9/PG11/PG13 still need confirming.

## Update file format

- Name `EEVBlog.bin`, matched case-insensitively as an 8.3 short name in the root directory of the first FAT partition, or of an unpartitioned volume. FAT12/16/32 all work; long file names aren't needed.
- Contents: a plain application image linked at 0x08006000, starting with its vector table, with no header or checksum. The released `EEVBlog*.bin` files are exactly this.
- Maximum size is 0x5A000 bytes (360 KB). Only whole words are written: a size that isn't a multiple of 4 loses its last 1–3 bytes.

## RAM layout of the original

| Address | Size | Contents |
|---|---|---|
| 0x20000000 | 0x104 | RAM code: `flash_program_half_page` and its two helpers |
| 0x20000104 | 0x230 | FatFs `FATFS` |
| 0x20000334 | 0x224 | FatFs `FIL` (update file) |
| 0x20000558 | 0x200 | read buffer |
| 0x20000758 | — | SDIO command/init/data structures (ST driver style) |
| 0x20000770 | 0x58 | SD card info (CSD/CID decoded) |
| 0x200007C8 | 0x40 | LCD RAM shadow (16 words; 8 used) |
| 0x20000838 | 0x20 | raw CSD and CID |
| 0x2000085C | — | update state, SD driver flags, tick flag |
| 0x200008A8 | 0x1000 | stack (initial SP 0x200018A8) |

The rebuild keeps the RAM code at 0x20000000. Everything else is placed by the GCC linker. The addresses are not entirely private to the bootloader: while the application starts, `SystemInit` sets `VTOR = 0x08000000` (`firmware/src/system_init.c`) until `main` moves it to 0x08006000, so a TIM2 tick in that window runs the bootloader's handler, which writes `g_tick_flag` (0x200007EE rebuilt, 0x200008A2 original). Both addresses fall inside the application's stack region, so the stray write is harmless in practice, but it differs between the two builds.

## Libraries in the original, and what replaced them

| In the binary | Identified by | In the rebuild |
|---|---|---|
| FatFs **R0.08a** (ChaN), default `ffconf.h`: non-tiny, writable, CP932, no LFN, 512-byte sectors | `FATFS`/`FIL` sizes (560/548), `f_mount(vol, fs)` API, `create_chain` error handling (≥R0.08a), `f_read` write-back placement (<R0.08b) | Upstream R0.08a in `common/fatfs/`, unmodified |
| ST Standard Peripheral Library (RCC, GPIO, FLASH, PWR, TIM, NVIC, LCD, SDIO, DMA) | init-struct layouts and register sequences | Plain register code with CMSIS headers; each helper is listed in an `inlines:` tag |
| ST-style SDIO SD-card driver | command flow (CMD0/8/55/41/2/3/9/7, ACMD51/6) | `bootloader/src/sd_card.c` |
| IAR C start-up (`__cmain`, `__iar_data_init3`, zero/copy init, semihosting exit) | init-table format | `bootloader/src/startup.c` |

## Behaviour kept exactly, including quirks

- `disk_read()` ignores its sector count and reads one block. It builds the byte address as `sector * 512` in 32 bits, and it doesn't wait for the DMA to finish, only for the card to return to transfer state.
- `disk_initialize()` returns the SD driver's error code as the FatFs status. `disk_ioctl()` reports a fixed 0x20000 sectors. `get_fattime()` returns 2006-02-09 22:30:00.
- `sd_init()`'s result is ignored and the 0x300-try retry loop always exits on the first pass. Its failure code (1) can never be returned because the counter runs past zero.
- The flash erase result is ignored, and an empty file programs nothing but still reports ` Good`.
- Progress uses `done / (size / 25) + 1`. Files under 25 bytes divide by zero, which gives 0 on the Cortex-M3.
- SD card info: high-capacity capacity is computed in 32 bits (wraps at 4 GB), `cid.reserved1` is OR-ed rather than set, and unknown card types reuse a leftover CSD byte. None of this affects operation.
- 7-segment writes clear only the upper of the two COM0 bits for each digit.
- Response waits in the SD driver have no timeout, apart from CMD0 and CMD8.
- At the application jump, TIM2 is still running with its interrupt enabled whenever the update wasn't started (normal boot or timeout).

## Deviations available by option (`BL_FIX_BUGS`)

The default build reproduces the original. The CMake option `BL_FIX_BUGS` (off by default, see the README) fixes the following. Each change sits under `#if BL_FIX_BUGS`, so the default preprocessed code is the original. The image grows by 232 bytes, to 10,388 bytes of the 24 KB region.

- **Read errors ignored** (`main.c` `program_application`, `diskio.c` `disk_read`, `sd_card.c` `sd_wait_read_operation`). The original ignores the result of `f_read()` and the byte count, so a failed or short read programs whatever is in the buffer. `disk_read()` ignores the result of `sd_read_block()` and never checks the data CRC. It waits only for the card to return to transfer state (forever if the card reports an error) and always returns `RES_OK`. Its byte address `sector * 512` is formed in 32 bits, so on an SDHC card every sector from 4 GiB up maps back to the start of the card. With the fix the address is formed in 64 bits. A failed read command, a data error (CRC, time-out, overrun) or a card error state returns `RES_ERROR`, and the read waits for the SDIO data end and for the DMA to finish (polled, at most 0x1000000 times). `program_application()` stops with FAIL when `f_read()` fails or returns fewer bytes than asked for.
- **Flash left unlocked** (`main.c` `main`, `flash_if.c` `flash_lock`). `main()` unlocks program memory and the data EEPROM (PELOCK and PRGLOCK) at every start and never locks them again, so the application runs with both writable. With the fix PRGLOCK and OPTLOCK, then PELOCK, are set just before the jump to the application, on every path (normal boot, time-out, after an update). The application's EEPROM writes unlock PELOCK themselves.
- **Half-programmed image started after FAIL** (`main.c` `run_update_mode`). After FAIL the original closes the file and starts the application. The stack pointer word at 0x08006000 is usually written by then, so the check passes and a partly programmed image runs. With the fix FAIL stays on the display. Once the start key (PG9) has read released on 0x10000 consecutive polls (a debounce of a few milliseconds at least), the bootloader waits for it again (with no time-out, as TIM2 is stopped) and retries the update, so a good card can be inserted and the update restarted. Turning the meter off and on boots whatever is in flash. Not fixed: a power loss during an update also leaves a partly programmed image that passes the stack pointer check. That needs a completion marker, written after the last half page and checked before the jump, which the application images do not have.
- **No size check** (`main.c` `program_application`, `board.h` `APP_FLASH_SIZE`). The original erases 0x08006000–0x0805FFFF (0x5A000 bytes) and then programs the whole file, so a larger file runs past the erased area until a program error. An empty file erases the application and reports Good. With the fix a file of 0 bytes, or larger than `APP_FLASH_SIZE` (0x5A000, derived from the same `APP_FLASH_LAST_PAGE` as the erase loop), gives FAIL before anything is erased. As the application is then intact, FAIL stays up for 50 TIM2 ticks (about 2 s; the original shows its result text for no time at all before the jump), TIM2 is left running as on a normal start, and the application is started as usual, after the stack pointer check.

Not changed: a file whose size is not a multiple of 4 still loses its last 1–3 bytes.

## Verification

**Emulator equivalence** (`python -P tools/emu/test_bootloader.py`) runs the original and the rebuilt image on a register-level STM32L1 model. It compares every peripheral write in order (main and interrupt contexts separately), every LCD frame, option-byte writes, final flash contents and the application jump:

| Scenario | Checks |
|---|---|
| normal_boot, hse_fail_boot | power-on to application jump, with and without the crystal |
| bor_forced_off | BOR level forced to 0 (USER = 0xFF0F00F0), then reset |
| iap_timeout | `-IAP-` prompt, 200 TIM2 interrupts, then boot |
| update_sdsc_v2, update_sdhc, update_sd_v1 | full v2.05 update; flash byte-identical to `EEVBlog2_05.bin`; SP 0x20001240 at the jump |
| update_no_file, update_no_card | open failures and the retry loop |
| update_verify_fail | a half page that reads back wrong, giving ` FAIL` and then boot |

All 10 pass, with identical traces from both images.

**Coverage** (`python -P tools/coverage.py bootloader` → `docs/coverage_bootloader.csv`): all 234 functions are accounted for, with 148 reconstructed, 32 library and 54 default handlers.

**Not covered by emulation:** the SD write path (`disk_write`, `sd_write_multi_blocks`, `sd_wait_write_operation`). FatFs never writes when files are only opened for reading, so the original never runs this code. It was checked against the disassembly only. SD CRC and data-error interrupts aren't exercised either.

**Tested on hardware.** The rebuilt bootloader has been flashed to the meter and tested by hand.

## Open questions

- Key names for PG9 (start) and PG11 + PG13 (update combination).
- What PF15 does. It's driven low when an update starts.
- Roles of the initial outputs on PE4–9/12/13, PF0–2/6–10 and PD7.
- Which annunciator COM2 SEG29 is. It's cleared before each message.
