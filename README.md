# EEVblog 121GW firmware — reverse-engineered C

Clean reverse-engineering of the EEVblog 121GW multimeter firmware (STM32L152xD + HY3131) into plain C that builds with GCC.

| Part | Location | Status |
|---|---|---|
| ST system bootloader (ROM) | `st-bootloader/` | Documentation only (ST code, not reproduced) |
| Custom SD-card bootloader | `bootloader/` | Reconstructed; equivalent to the original in emulation (see `docs/bootloader.md`); tested on the meter |
| Multimeter application (v2.05) | `firmware/` | Reconstructed (691 functions plus FatFs and the C runtime); equivalent to the original in emulation, function by function and as a whole image (see `docs/app-architecture.md`); tested on the meter |

## Building

Requires the GNU Arm Embedded toolchain (`arm-none-eabi-gcc`), CMake ≥ 3.16, Ninja and Python ≥ 3.11 (the firmware build runs `python -P`), all on the PATH (or add `-DCMAKE_MAKE_PROGRAM=<path to ninja>`). Run the commands from the repository root; the projects find `cmake/arm-none-eabi.cmake` by themselves. The released binaries were built with GCC 9.2.1 (GNU Arm Embedded 9-2019-q4).

```
cmake -G Ninja -S bootloader -B build/bootloader
cmake --build build/bootloader
```

Output: `build/bootloader/bootloader.bin`, linked at 0x08000000.

```
cmake -G Ninja -S firmware -B build/firmware
cmake --build build/firmware
```

Output: `build/firmware/app.bin`, the application linked at 0x08006000 (the address the SD bootloader starts). It is a plain image like the released `EEVBlog*.bin` files, so it installs the same way (copied to the root of the SD card as `EEVBlog.bin`, then the update procedure in the manual), or over SWD with the chip held in reset:

```
STM32_Programmer_CLI -c port=SWD mode=UR -w build/firmware/app.bin 0x08006000 -v -hardRst
```

Write only from 0x08006000: the bootloader below it and the data EEPROM at 0x08080000 (the meter's calibration) must not be touched.

**Warning:** before any SWD work, back up the data EEPROM (`STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -u 0x08080000 0x3000 eeprom.bin`). A mass erase or a read-out protection (RDP) level change wipes the calibration stored there, and it cannot be recovered without that backup.

The same build also produces `hybrid.bin`, the test image used by the function-level equivalence tests.

### Build options

Two CMake options change the application's behaviour. Both are off by default, and the default build stays bit-equivalent to the original v2.05 image:

- `APP_FIX_BUGS`: fixes confirmed bugs in the reading and auto-range path (readings post-processed repeatedly while MEM recall is open, a fake zero reading after every function/range change, a stale bargraph limit after dual-function auto-range) and in storage and communication (SD addresses wrapping above 4 GiB, ignored SD errors and log read-back mismatches, an unchecked calibration restore, log file 99 wrapping to 00, an unchecked BLE clock set, sticky flash error flags, flash left unlocked at start-up).
- `APP_AUTORANGE_FAST`: auto-range jumps several ranges at once instead of one per reading, for the functions whose ranges are all a decade apart. It turns `APP_FIX_BUGS` on (CMake prints a status line): without the fixes, the readings skipped after a range change can be used up by a fake one, and a fast step could act on an unsettled reading. `-DAPP_AUTORANGE_FAST=ON` alone builds the same image as both options.

```
cmake -G Ninja -S firmware -B build/firmware -DAPP_FIX_BUGS=ON -DAPP_AUTORANGE_FAST=ON
cmake --build build/firmware
```

Use a separate build directory, or turn an option off again with `=OFF`: CMake keeps option values in the cache. An image built with either option is no longer equivalent to the original, so the emulator equivalence tests are expected to fail for the changed functions. `docs/app-architecture.md` (Deviations available by option) lists each change.

With `APP_FIX_BUGS`, SD errors that the original ignored now count: a write or read-back error that persists stops logging after 20 retries and 20 more with a fall-back file (`SdE 7` / `SdE 8`). Run a long logging session on the meter with this build before relying on it.

The bootloader has one option, `BL_FIX_BUGS`, also off by default (the default build stays bit-equivalent to the original). It fixes the SD update path: a failed or short read is no longer programmed, SD addresses no longer wrap above 4 GiB, the flash is locked again before the application starts, a failed update no longer starts the half-programmed image, and a file larger than the application area (or empty) is refused before anything is erased (FAIL for about 2 s, then the intact application starts). `docs/bootloader.md` (Deviations available by option) lists each change.

```
cmake -G Ninja -S bootloader -B build/bootloader -DBL_FIX_BUGS=ON
cmake --build build/bootloader
```

The constants of `APP_AUTORANGE_FAST` (in `firmware/src/range_select.c`) are not yet validated on the meter. Before relying on it, check each function with a calibrator at 1, 9, 30 and 90 % of each range, starting from the top range. Then log `g_ad1_average` at 2×, 10× and 100× full scale and with the input open, and set `AUTORANGE_AD1_RAIL` just below the level the ADC actually clips at.

## Verifying

```
python -P tools/emu/test_bootloader.py     # original vs rebuilt, on an emulated STM32L1
python -P tools/coverage.py bootloader     # every original function accounted for
python -P tools/emu/test_app_functions.py  # application, function by function (--shard i/n to split)
python -P tools/emu/compare_app.py 20000000 0 --points --ticks=40000   # whole application image
python -P tools/coverage.py app
```

Requirements: Python >= 3.11 and `pip install unicorn capstone`.

- The emulator tests (`tools/emu/`) need a private `dumps/` directory (not distributed) with `flash.bin`, `eeprom.bin`, `app_data_init.bin` and `bootloader.bin`, read from a meter with STM32CubeProgrammer; see "Producing the dumps" in `docs/tooling.md`. Without it they cannot run.
- `tools/coverage.py` needs `ghidra/export/<name>/functions.tsv` (git-ignored; produced by `tools/ghidra_export.ps1`). It exits non-zero if any original function is unaccounted for.

## Layout

| Path | Contents |
|---|---|
| `bootloader/` | Bootloader sources, linker script, CMake project |
| `firmware/` | Application sources, linker script, CMake project |
| `common/cmsis/` | ARM CMSIS core and ST STM32L1 device headers (Apache-2.0) |
| `common/fatfs/` | ChaN FatFs R0.08a, as used by the original (BSD-style licence in `ff.c`) |
| `docs/` | Memory map, bootloader notes, coverage tables, tooling, manual text |
| `tools/` | Ghidra scripts, emulator harness, coverage checker |

See `docs/memory-map.md` for the device layout and reference image hashes.

## Licence

MIT (see `LICENSE`), except the third-party code in `common/`, which keeps its own licence: CMSIS and the ST device headers are Apache-2.0 (`common/cmsis/core/LICENSE.txt`, `common/cmsis/device/License.md`), and FatFs is under ChaN's licence (in its source files).
