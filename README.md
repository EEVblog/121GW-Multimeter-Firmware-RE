# EEVblog 121GW firmware — reverse-engineered C

Clean reverse-engineering of the EEVblog 121GW multimeter firmware (STM32L152xD + HY3131) into plain C that builds with GCC.

| Part | Location | Status |
|---|---|---|
| ST system bootloader (ROM) | `st-bootloader/` | Documentation only (ST code, not reproduced) |
| Custom SD-card bootloader | `bootloader/` | Not started |
| Multimeter application (v2.05) | `firmware/` | Not started |

See `docs/memory-map.md` for the device layout and reference image hashes.
