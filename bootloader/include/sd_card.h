/*
 * SD card driver over the STM32L1 SDIO peripheral (1-bit bus, DMA2 channel 4).
 */
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>

/* Driver status codes (values as in the original image). */
typedef enum {
    SD_OK = 0,
    SD_CMD_CRC_FAIL = 1,
    SD_DATA_CRC_FAIL = 2,
    SD_CMD_RSP_TIMEOUT = 3,
    SD_DATA_TIMEOUT = 4,
    SD_TX_UNDERRUN = 5,
    SD_RX_OVERRUN = 6,
    SD_START_BIT_ERR = 7,
    SD_ADDR_MISALIGNED = 9,
    SD_BLOCK_LEN_ERR = 10,
    SD_ERASE_SEQ_ERR = 11,
    SD_BAD_ERASE_PARAM = 12,
    SD_WRITE_PROT_VIOLATION = 13,
    SD_LOCK_UNLOCK_FAILED = 14,
    SD_COM_CRC_FAILED = 15,
    SD_ILLEGAL_CMD = 16,
    SD_CARD_ECC_FAILED = 17,
    SD_CC_ERROR = 18,
    SD_GENERAL_UNKNOWN_ERROR = 19,
    SD_STREAM_READ_UNDERRUN = 20,
    SD_STREAM_WRITE_OVERRUN = 21,
    SD_CID_CSD_OVERWRITE = 22,
    SD_WP_ERASE_SKIP = 23,
    SD_CARD_ECC_DISABLED = 24,
    SD_ERASE_RESET = 25,
    SD_AKE_SEQ_ERROR = 26,
    SD_INVALID_VOLTRANGE = 27,
    SD_ADDR_OUT_OF_RANGE = 28,
    SD_REQUEST_NOT_APPLICABLE = 37,
    SD_INVALID_PARAMETER = 38,
    SD_UNSUPPORTED_FEATURE = 39
} sd_error_t;

/* Card types */
enum {
    SD_TYPE_STD_CAPACITY_V1_1 = 0,
    SD_TYPE_STD_CAPACITY_V2_0 = 1,
    SD_TYPE_HIGH_CAPACITY = 2,
    SD_TYPE_MULTIMEDIA = 3,
    SD_TYPE_SDIO = 4,
    SD_TYPE_HIGH_SPEED_MULTIMEDIA = 5,
    SD_TYPE_SDIO_COMBO = 6,
    SD_TYPE_HIGH_CAPACITY_MMC = 7
};

/* Result of sd_get_transfer_status() */
enum {
    SD_TRANSFER_OK = 0,
    SD_TRANSFER_BUSY = 1,
    SD_TRANSFER_ERROR = 2
};

#define SD_BUS_WIDE_1B  0x0000u
#define SD_BUS_WIDE_4B  0x0800u
#define SD_BUS_WIDE_8B  0x1000u

/* Decoded CSD register (SD Physical Layer spec field names). */
typedef struct {
    uint8_t  csd_structure;
    uint8_t  spec_version;
    uint8_t  reserved1;
    uint8_t  taac;
    uint8_t  nsac;
    uint8_t  tran_speed;
    uint16_t ccc;
    uint8_t  read_bl_len;
    uint8_t  read_bl_partial;
    uint8_t  write_blk_misalign;
    uint8_t  read_blk_misalign;
    uint8_t  dsr_imp;
    uint8_t  reserved2;
    uint32_t c_size;
    uint8_t  vdd_r_curr_min;
    uint8_t  vdd_r_curr_max;
    uint8_t  vdd_w_curr_min;
    uint8_t  vdd_w_curr_max;
    uint8_t  c_size_mult;
    uint8_t  erase_blk_en;
    uint8_t  sector_size;
    uint8_t  wp_grp_size;
    uint8_t  wp_grp_enable;
    uint8_t  default_ecc;
    uint8_t  r2w_factor;
    uint8_t  write_bl_len;
    uint8_t  write_bl_partial;
    uint8_t  reserved3;
    uint8_t  content_prot_app;
    uint8_t  file_format_grp;
    uint8_t  copy;
    uint8_t  perm_write_protect;
    uint8_t  tmp_write_protect;
    uint8_t  file_format;
    uint8_t  ecc;
    uint8_t  crc;
    uint8_t  reserved4;
} sd_csd_t;

/* Decoded CID register. */
typedef struct {
    uint8_t  manufacturer_id;
    uint16_t oem_id;
    uint32_t product_name1;
    uint8_t  product_name2;
    uint8_t  product_rev;
    uint32_t product_sn;
    uint8_t  reserved1;
    uint16_t manufacture_date;
    uint8_t  crc;
    uint8_t  reserved2;
} sd_cid_t;

typedef struct {
    sd_csd_t csd;
    sd_cid_t cid;
    uint64_t capacity;     /* bytes */
    uint32_t block_size;   /* bytes */
    uint16_t rca;
    uint8_t  card_type;
} sd_card_info_t;

sd_error_t sd_init(void);
void sd_low_level_deinit(void);
void sd_power_off(void);
uint8_t sd_detect(void);
uint8_t sd_get_transfer_status(void);
sd_error_t sd_read_block(uint8_t *buffer, uint64_t address, uint16_t block_size);
sd_error_t sd_write_multi_blocks(const uint8_t *buffer, uint64_t address,
                                 uint16_t block_size, uint32_t block_count);
sd_error_t sd_wait_write_operation(void);
#if BL_FIX_BUGS
sd_error_t sd_wait_read_operation(void);
#endif

/* Interrupt hooks */
sd_error_t sd_process_irq(void);
void sd_process_dma_irq(void);

#endif /* SD_CARD_H */
