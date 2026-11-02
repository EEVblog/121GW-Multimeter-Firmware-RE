/*
 * FatFs disk I/O layer for the SD card (single drive 0).
 *
 * Quirks preserved from the original image:
 *  - disk_read() ignores 'count' and always reads one 512-byte block. FatFs
 *    asks for more only when f_read() spans whole sectors into the caller's
 *    buffer; the bootloader always reads 512 bytes, so this is never hit.
 *  - Byte addresses are formed as sector * 512 in 32 bits (wraps above 4 GB
 *    for standard-capacity addressing).
 *  - disk_initialize() returns the SD driver's error code as the status.
 */
#include "diskio.h"
#include "sd_card.h"

#define SECTOR_SIZE  512u

/* orig: 0x08003238 */
DSTATUS disk_initialize(BYTE drv)
{
    (void)drv;
    return (DSTATUS)sd_init();
}

/* orig: 0x0800324A */
DSTATUS disk_status(BYTE drv)
{
    return (drv != 0) ? STA_NOINIT : 0;
}

/* orig: 0x0800325A */
DRESULT disk_read(BYTE drv, BYTE *buff, DWORD sector, BYTE count)
{
    (void)drv;
    (void)count;
    (void)sd_read_block(buff, (uint32_t)(sector * SECTOR_SIZE), SECTOR_SIZE);
    while (sd_get_transfer_status() != SD_TRANSFER_OK) {
    }
    return RES_OK;
}

#if _READONLY == 0
/* orig: 0x08003280 */
DRESULT disk_write(BYTE drv, const BYTE *buff, DWORD sector, BYTE count)
{
    (void)drv;
    for (uint32_t i = 0; i < count; i++) {
        (void)sd_write_multi_blocks(buff + i * SECTOR_SIZE,
                                    (uint32_t)((sector + i) * SECTOR_SIZE), SECTOR_SIZE, 1);
        (void)sd_wait_write_operation();
        while (sd_get_transfer_status() != SD_TRANSFER_OK) {
        }
    }
    return RES_OK;
}
#endif

/* orig: 0x080032EC - fixed geometry, as in the original */
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

/* orig: 0x080032E2 - fixed timestamp 2006-02-09 22:30:00 */
DWORD get_fattime(void)
{
    return 0x3449B3C0u;
}
