/*
 * Hybrid test image: rebuilt modules linked against the original v2.05 image.
 * Code lives at 0x08100000 (outside real flash; emulator only). All globals
 * resolve to their original RAM addresses, so the image must not define any
 * RAM data of its own.
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

    .data : { *(.data*) } > TEST
    .bss  : { *(.bss*) *(COMMON) } > TEST

    ASSERT(SIZEOF(.data) == 0, "hybrid image must not define initialised RAM data")
    ASSERT(SIZEOF(.bss) == 0, "hybrid image must not define RAM variables")
}
