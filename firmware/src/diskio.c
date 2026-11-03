/*
 * FatFs disk I/O layer for the SD card (single drive 0).
 *
 * As the bootloader's (bootloader/src/diskio.c), except that reads and
 * writes give up after 20000 polls of the card state and then return 2
 * (RES_WRPRT, as the original does).
 *
 * Quirks preserved from the original image:
 *  - disk_read() ignores 'count' and always reads one 512-byte block.
 *  - Byte addresses are formed as sector * 512 in 32 bits.
 *  - disk_initialize() returns the SD driver's error code as the status.
 *
 * With APP_FIX_BUGS the byte address is formed in 64 bits (the 32-bit
 * product wraps above 4 GiB, and the SDHC block number derived from it then
 * points back at the start of the card), driver errors are returned as
 * RES_ERROR instead of being ignored, and a read waits for the data block
 * to end and checks its CRC/transfer status.
 */
#include "diskio.h"
#include "sd_card.h"

#define SECTOR_SIZE  512u
#define STATUS_POLLS 20000u

static DRESULT wait_transfer(void)
{
    for (uint32_t polls = STATUS_POLLS; polls != 0; polls--) {
        if (sd_get_transfer_status() == SD_TRANSFER_OK) {
            return RES_OK;
        }
    }
    return RES_WRPRT;
}

/* orig: 0x08023F5C */
DSTATUS disk_initialize(BYTE drv)
{
    (void)drv;
    return (DSTATUS)sd_init();
}

/* orig: 0x08023F6E */
DSTATUS disk_status(BYTE drv)
{
    return (drv != 0) ? STA_NOINIT : 0;
}

/* orig: 0x08023F7E */
DRESULT disk_read(BYTE drv, BYTE *buff, DWORD sector, BYTE count)
{
    (void)drv;
    (void)count;
#if APP_FIX_BUGS
    if (sd_read_block(buff, (uint64_t)sector * SECTOR_SIZE, SECTOR_SIZE) != SD_OK
        || sd_wait_read_operation() != SD_OK) {
        return RES_ERROR;
    }
#else
    (void)sd_read_block(buff, (uint32_t)(sector * SECTOR_SIZE), SECTOR_SIZE);
#endif
    return wait_transfer();
}

/* orig: 0x08023FB8 */
DRESULT disk_write(BYTE drv, const BYTE *buff, DWORD sector, BYTE count)
{
    (void)drv;
    for (uint32_t i = 0; i < count; i++) {
#if APP_FIX_BUGS
        if (sd_write_multi_blocks(buff + i * SECTOR_SIZE, ((uint64_t)sector + i) * SECTOR_SIZE,
                                  SECTOR_SIZE, 1) != SD_OK
            || sd_wait_write_operation() != SD_OK) {
            return RES_ERROR;
        }
#else
        (void)sd_write_multi_blocks(buff + i * SECTOR_SIZE,
                                    (uint32_t)(i * SECTOR_SIZE + sector * SECTOR_SIZE), SECTOR_SIZE, 1);
        (void)sd_wait_write_operation();
#endif
        if (wait_transfer() != RES_OK) {
            return RES_WRPRT;
        }
    }
    return RES_OK;
}

/* orig: 0x08024038 - fixed geometry, as in the original */
DRESULT disk_ioctl(BYTE drv, BYTE ctrl, void *buff)
{
    (void)drv;
    switch (ctrl) {
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = 0x20000;
        break;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECTOR_SIZE;
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 0x20;
        break;
    default:   /* CTRL_SYNC: nothing to do */
        break;
    }
    return RES_OK;
}

/* orig: 0x0802402E - fixed timestamp 2006-02-09 22:30:00 */
DWORD get_fattime(void)
{
    return 0x3449B3C0u;
}
