# EEVblog 121GW firmware — reverse-engineered C

Clean reverse-engineering of the EEVblog 121GW multimeter firmware (STM32L152xD + HY3131) into plain C that builds with GCC.

| Part | Location | Status |
|---|---|---|
| ST system bootloader (ROM) | `st-bootloader/` | Documentation only (ST code, not reproduced) |
| Custom SD-card bootloader | `bootloader/` | Reconstructed; equivalent to the original in emulation (see `docs/bootloader.md`); tested on the meter |
| Multimeter application (v2.05) | `firmware/` | Reconstructed (691 functions plus FatFs and the C runtime); equivalent to the original in emulation, function by function and as a whole image (see `docs/app-architecture.md`); tested on the meter |

## Building

Requires the GNU Arm Embedded toolchain (`arm-none-eabi-gcc`), CMake ≥ 3.16 and Ninja, all on the PATH (or add `-DCMAKE_MAKE_PROGRAM=<path to ninja>`). Run the commands from the repository root; the projects find `cmake/arm-none-eabi.cmake` by themselves. The released binaries were built with GCC 9.2.1 (GNU Arm Embedded 9-2019-q4).

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

Write only from 0x08006000: the bootloader below it and the data EEPROM at 0x08080000 (the meter's calibration) must not be touched. The same build also produces `hybrid.bin`, the test image used by the function-level equivalence tests.

## Verifying

```
python -P tools/emu/test_bootloader.py     # original vs rebuilt, on an emulated STM32L1
python -P tools/coverage.py bootloader     # every original function accounted for
python -P tools/emu/test_app_functions.py  # application, function by function (--shard i/n to split)
python -P tools/emu/compare_app.py 20000000 0 --points --ticks=40000   # whole application image
python -P tools/coverage.py app
```

The emulator test needs `pip install unicorn` and a private dump of the original flash in `dumps/` (not distributed).

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
