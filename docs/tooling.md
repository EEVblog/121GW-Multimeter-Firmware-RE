# Tooling

| Tool | Version | Location / source | Use |
|---|---|---|---|
| Ghidra | 12.1.4 (2026-09-21) | `C:\Users\Dave\Tools\ghidra_12.1.4_PUBLIC`, from NSA GitHub releases, SHA256 `ddac49f9…d2d4db` | Decompilation |
| Temurin JDK | 21.0.13+9 | `C:\Users\Dave\Tools\jdk-21.0.13+9`, from Adoptium, SHA256 `a4dbaa05…2004f9` | Runs Ghidra |
| arm-none-eabi-gcc | 9 2019-q4-major (GCC 9.2.1) | `C:\Users\steve\tools\gcc-arm-none-eabi-9-2019-q4-major` (put its `bin` on PATH) | Rebuild |
| CMake | already installed | | Build system |
| STM32CubeProgrammer CLI | 2.23.0 | already installed | SWD dump/flash |
| STM32L152 SVD | from CubeProgrammer | `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\SVD\STM32L152.svd` | Peripheral register names |
| unicorn / capstone / pypdf | 2.1.4 / 5.0.x / 6.21.4 | `pip install --user` | Equivalence harness, disassembly, manual text |

Python scripts here use `python -P` rather than `-I`, because `-I` hides the user site-packages where these were installed.

## Headless Ghidra
`JAVA_HOME` must point at the JDK 21 folder (the system Java 8 is too old):

```powershell
$env:JAVA_HOME = 'C:\Users\Dave\Tools\jdk-21.0.13+9'
& 'C:\Users\Dave\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat' `
    X:\121GW-Firmware-RE\ghidra <project> `
    -import X:\121GW-Firmware-RE\dumps\bootloader.bin `
    -processor ARM:LE:32:Cortex -loader BinaryLoader -loader-baseAddr 0x08000000 `
    -scriptPath X:\121GW-Firmware-RE\tools\ghidra_scripts -postScript Summary.java
```

Smoke test (bootloader region, no seeding, no SVD yet): 163 functions found.

## Producing the dumps
The emulator tests read `dumps/` (ignored by version control, not distributed). Read each region from the meter, read-only (sizes and hashes in `docs/memory-map.md`):

```
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -u 0x08000000 0x60000 dumps/flash.bin
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -u 0x08080000 0x3000 dumps/eeprom.bin
```
`bootloader.bin` is the bootloader region of `flash.bin` (from 0x08000000) and `app_data_init.bin` is the unpacked application RAM initialiser (see `tools/iar_lz77.py`). Back up `eeprom.bin` before any other SWD work: a mass erase or an RDP level change wipes the calibration.

## Model limitations (tools/emu/stm32l1_model.py)
The emulator is a behavioural model, not a silicon-accurate one; equivalence results hold only within these limits:
- `FLASH_SR` never reports busy or an error, so flash error paths are not exercised.
- No double-programming check and no half-page alignment model for flash writes.
- The SD card is always in the TRAN state and DMA completes instantly.
- `tick_blocks` defaults to 0, so timer interrupts are off unless a test enables them.
- Interrupts ignore PRIMASK and priorities.

## Reference text
`docs/manual.txt` is extracted from `EEVblog-121GW-Manual.pdf` (73 pages) and is used for feature behaviour and the test matrix.
