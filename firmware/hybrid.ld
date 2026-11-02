/*
 * Hybrid test image: rebuilt modules linked against the original v2.05 image.
 * Code lives at 0x08100000 (outside real flash; emulator only). All globals
 * resolve to their original RAM addresses, so the image must not define any
 * RAM data of its own. The only exception is FatFs (common/fatfs/ff.c,
 * unmodified), whose two static variables are placed at the original
 * image's addresses.
 */
MEMORY
{
    TEST (rx) : ORIGIN = 0x08100000, LENGTH = 256K
}

SECTIONS
{
    .text :
    {
        KEEP(*(.text*))
        *(.rodata*)
    } > TEST

    .fatfs_volumes 0x20007C58 (NOLOAD) : { *(.bss.FatFs) }   /* FATFS *FatFs[1] */
    .fatfs_mount_id 0x20007F98 (NOLOAD) : { *(.bss.Fsid) }   /* WORD Fsid */

    .data : { *(.data*) } > TEST
    .bss  : { *(.bss*) *(COMMON) } > TEST

    ASSERT(SIZEOF(.data) == 0, "hybrid image must not define initialised RAM data")
    ASSERT(SIZEOF(.bss) == 0, "hybrid image must not define RAM variables")
}
