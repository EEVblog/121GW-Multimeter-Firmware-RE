# EEVblog 121GW firmware — reverse-engineered C

Clean reverse-engineering of the EEVblog 121GW multimeter firmware (STM32L152xD + HY3131) into plain C that builds with GCC.

| Part | Location | Status |
|---|---|---|
| ST system bootloader (ROM) | `st-bootloader/` | Documentation only (ST code, not reproduced) |
| Custom SD-card bootloader | `bootloader/` | Reconstructed; equivalent to the original in emulation (see `docs/bootloader.md`); not yet run on hardware |
| Multimeter application (v2.05) | `firmware/` | Not started |

## Building

Requires the GNU Arm Embedded toolchain (`arm-none-eabi-gcc`), CMake ≥ 3.16 and Ninja.

```
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -S bootloader -B build/bootloader
cmake --build build/bootloader
```

Output: `build/bootloader/bootloader.bin`, linked at 0x08000000.

## Verifying

```
python -P tools/emu/test_bootloader.py     # original vs rebuilt, on an emulated STM32L1
python -P tools/coverage.py bootloader     # every original function accounted for
```

The emulator test needs `pip install unicorn` and a private dump of the original flash in `dumps/` (not distributed).

## Layout

| Path | Contents |
|---|---|
| `bootloader/` | Bootloader sources, linker script, CMake project |
| `common/cmsis/` | ARM CMSIS core and ST STM32L1 device headers (Apache-2.0) |
| `common/fatfs/` | ChaN FatFs R0.08a, as used by the original (BSD-style licence in `ff.c`) |
| `docs/` | Memory map, bootloader notes, coverage tables, tooling, manual text |
| `tools/` | Ghidra scripts, emulator harness, coverage checker |

See `docs/memory-map.md` for the device layout and reference image hashes.
