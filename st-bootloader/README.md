# ST system bootloader (documentation only)

The STM32L15xxD has ST's factory bootloader in system-memory ROM at 0x1FF00000 (8 KB). On this meter it reports version **0x45 (v4.5)**. It is ST's copyrighted code. It can't be erased or replaced, so this project doesn't convert it to C or publish it. A private dump is kept in `dumps/sysmem.bin` for reference only.

## Why it matters
It is the recovery path. If both the custom SD bootloader and the application are broken, the meter can still be reflashed through it or directly over SWD.

## Entry
The MCU boots from system memory when BOOT0 (pin 138) is high at reset. On the 121GW, BOOT0 is not brought out to a button. Details are to be confirmed against the board.

## Recovery options
1. **SWD (preferred):** ST-LINK on CN3 (SWDIO, SWCLK, RESET#, VDD, GND). This works regardless of what is in flash, as long as RDP stays at Level 0.
2. **ST USART bootloader (AN2606 / AN3155):** needs BOOT0 high and access to the USART pins listed in AN2606 for this device.

More detail will be added as the board is examined.
