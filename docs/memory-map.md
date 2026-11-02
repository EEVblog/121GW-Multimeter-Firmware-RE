# 121GW memory map and reference images

## Target device (as read 2026-11-02)

| Item | Value |
|---|---|
| MCU | STM32L15xxD (DEV_ID 0x436, rev Y), Cortex-M3 |
| Flash | 384 KB, 0x08000000–0x0805FFFF (erased value 0x00) |
| RAM | 48 KB, 0x20000000–0x2000BFFF |
| Data EEPROM | 12 KB, 0x08080000–0x08082FFF |
| ST system bootloader | ROM 0x1FF00000, 8 KB, version 0x45 (v4.5) |
| Read-out protection | Level 0 (0xAA), no WRP, IWDG software, BOR off, nBFB2=1 |
| Debug probe | ST-LINK V2, FW V2J48S7, SWD at 4 MHz, target 3.28 V |

## Flash layout

| Range | Size | Contents |
|---|---|---|
| 0x08000000–0x08005FFF | 24 KB | Custom SD-card bootloader ("IAP"), 0x514C bytes used |
| 0x08006000–0x0805FFFF | 360 KB | Application, 0x234E8 bytes used (v2.05) |

### Bootloader (0x08000000)
- Initial SP 0x200018A8, Reset 0x08004EED, NMI 0x08004DD9, HardFault 0x08004DDB.
- **Does not match** the archived `EEVBlog__IAP.bin` (Feb 2017, 20,792 B, Reset 0x08004ED9). The device copy is 20 bytes longer, with the same structure and a shifted layout. It's a later build, and **the device copy is the RE target**.

### Application (0x08006000)
- **Byte-identical to the released `EEVBlog2_05.bin`** (144,616 B, SHA256 below). Everything past the image is erased.
- Initial SP 0x20001240, Reset 0x08029175, NMI 0x0802427D, HardFault 0x0802427F. 68 of the 73 vector slots are populated.
- Release images are plain, unencrypted application binaries linked at 0x08006000, with no header.

## Data EEPROM (0x08080000)
Non-zero data only in 0x008–0x2E2, 0x302–0x480 and 0x4D0–0x4F9 (576 bytes). It holds this unit's calibration and settings. The layout is to be recovered from the code. **Never written by this project.** The dump stays out of git.

## Option bytes (0x1FF80000)
`aa0055ff f0000fff 0000ffff 0000ffff …` → RDP Level 0, USER 0xF0 (IWDG_SW, nRST_STOP, nRST_STDBY, nBFB2 set; BOR off), no write protection.

## Dumps (`dumps/`, git-ignored)
Each region was read twice in hot-plug mode (no reset), and both reads were identical.

| File | Address | Size | SHA256 |
|---|---|---|---|
| flash.bin | 0x08000000 | 0x60000 | F7A415DA8FDFCA288A92F32BDE59A85BFE00B5D753E60BFFA26171AB9D604285 |
| eeprom.bin | 0x08080000 | 0x3000 | 222E0B6E160EFF4209C762DF7A3451A0D4867820FF43A8D84E4A66E177A43858 |
| optbytes.bin | 0x1FF80000 | 0x20 | FD0E54460779EFE8FE35B830521063D701EF5DDA2A67CF7C5E0D710C18FAE478 |
| sysmem.bin | 0x1FF00000 | 0x2000 | 7878B22A86BB0BF96E2931C1292E9B897E743B1B908905234BB56EAA02CE31B4 |

Command used (per region, read-only):
```
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -u <addr> <size> <file>
```
