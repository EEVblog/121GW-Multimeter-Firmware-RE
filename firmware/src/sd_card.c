/*
 * SD card driver over the STM32L1 SDIO peripheral (1-bit bus, DMA2 channel 4).
 *
 * This is the application's build of the bootloader's driver (see
 * bootloader/src/sd_card.c). Differences in the application:
 *  - the ST library init structures for SDIO set-up, commands and data
 *    transfers live in RAM (g_sdio_init, g_sdio_cmd, g_sdio_data);
 *  - every wait for a command response gives up after 20000 polls, and the
 *    write wait after 1000 polls per phase;
 *  - only D0, CK and CMD are configured as SDIO pins (the board uses the
 *    1-bit bus).
 */
#include <stddef.h>
#include "sd_card.h"
#include "board_io.h"
#include "stm32l1xx.h"

_Static_assert(offsetof(sd_card_info_t, cid) == 0x2C, "SD_CardInfo layout");
_Static_assert(offsetof(sd_card_info_t, capacity) == 0x48, "SD_CardInfo layout");
_Static_assert(offsetof(sd_card_info_t, rca) == 0x54, "SD_CardInfo layout");
_Static_assert(offsetof(sd_card_info_t, card_type) == 0x56, "SD_CardInfo layout");
_Static_assert(offsetof(sdio_init_t, clock_div) == 0x14, "SDIO_InitTypeDef layout");

/* SDIO_STA / SDIO_ICR / SDIO_MASK bits */
#define STA_CCRCFAIL   (1u << 0)
#define STA_DCRCFAIL   (1u << 1)
#define STA_CTIMEOUT   (1u << 2)
#define STA_DTIMEOUT   (1u << 3)
#define STA_TXUNDERR   (1u << 4)
#define STA_RXOVERR    (1u << 5)
#define STA_CMDREND    (1u << 6)
#define STA_CMDSENT    (1u << 7)
#define STA_DATAEND    (1u << 8)
#define STA_STBITERR   (1u << 9)
#define STA_DBCKEND    (1u << 10)
#define STA_TXACT      (1u << 12)
#define STA_RXDAVL     (1u << 21)
#define STATIC_FLAGS   0x5FFu

#define CMD_RESPONSE_DONE     (STA_CCRCFAIL | STA_CTIMEOUT | STA_CMDREND)
#define DATA_IT_MASK          (STA_DCRCFAIL | STA_DTIMEOUT | STA_DATAEND | STA_RXOVERR | STA_STBITERR)
#define DATA_IT_ALL           0xC33Au
#define SCR_READ_DONE         (STA_DCRCFAIL | STA_DTIMEOUT | STA_RXOVERR | STA_STBITERR | STA_DBCKEND)

/* SDIO_CMD fields */
#define RESP_NONE   0x000u
#define RESP_SHORT  0x040u
#define RESP_LONG   0x0C0u
#define CPSM_ENABLE 0x400u

/* SDIO_CLKCR / SDIO_POWER / SDIO_DCTRL */
#define CLKCR_CLKEN          (1u << 8)
#define CLKDIV_INIT          0x76u   /* identification clock */
#define CLKDIV_TRANSFER      0x01u
#define POWER_ON             0x3u
#define POWER_OFF            0x0u
#define DCTRL_DMAEN          (1u << 3)
#define DCTRL_DIR_TO_HOST    2u
#define DCTRL_BLOCK_8B       0x30u
#define DCTRL_BLOCK_512B     0x90u
#define DPSM_ENABLE          1u
#define DATA_TIMEOUT_MAX     0xFFFFFFFFu

/* SD commands */
#define CMD0_GO_IDLE_STATE        0
#define CMD2_ALL_SEND_CID         2
#define CMD3_SEND_RELATIVE_ADDR   3
#define ACMD6_SET_BUS_WIDTH       6
#define CMD7_SELECT_CARD          7
#define CMD8_SEND_IF_COND         8
#define CMD9_SEND_CSD             9
#define CMD12_STOP_TRANSMISSION   12
#define CMD13_SEND_STATUS         13
#define CMD16_SET_BLOCKLEN        16
#define CMD17_READ_SINGLE_BLOCK   17
#define ACMD23_SET_WR_BLK_ERASE   23
#define CMD25_WRITE_MULTI_BLOCK   25
#define ACMD41_SD_SEND_OP_COND    41
#define ACMD51_SEND_SCR           51
#define CMD55_APP_CMD             55

#define CMD8_CHECK_PATTERN        0x1AAu
#define ACMD41_VOLTAGE_WINDOW     0x80100000u
#define OCR_HCS                   0x40000000u
#define MAX_VOLT_TRIAL            0xFFFFu
#define CMDSENT_TIMEOUT           0x10000u
#define RESPONSE_POLLS            20000u
#define WRITE_WAIT_POLLS          1000u

#define R1_ERRORBITS              0xFDFFE008u
#define R1_CARD_LOCKED            0x02000000u
#define R6_ERRORBITS              0x0000E000u
#define SCR_WIDE_BUS_SUPPORT      0x00040000u
#define SCR_SINGLE_BUS_SUPPORT    0x00010000u
#define CARD_STATE_TRANSFER       4u
#define CARD_STATE_ERROR          0xFFu

/* DMA2 channel 4 */
#define DMA2_FLAGS_CH4            0x1000F000u   /* as passed to DMA_ClearFlag() */
#define DMA2_FLAG_TC4             0x10002000u
#define DMA_CCR_EN_BIT            (1u << 0)
#define DMA_CCR_TCIE_BIT          (1u << 1)
#define DMA_DIR_FROM_MEMORY       (1u << 4)
#define DMA_MINC                  (1u << 7)
#define DMA_PSIZE_32              (2u << 8)
#define DMA_MSIZE_32              (2u << 10)
#define DMA_PRIORITY_HIGH         (2u << 12)

#define RCC_AHBENR_GPIOC_D        0x0000000Cu
#define RCC_AHBENR_DMA2           0x02000000u
#define RCC_APB2_SDIO             0x00000800u
#define GPIO_AF_SDIO              12u

/* ---- SDIO library helpers (ST SPL, operating on the RAM structures) ------- */

/* orig: 0x0802408C (SDIO_Init) */
static void sdio_apply_init(const sdio_init_t *init)
{
    SDIO->CLKCR = (SDIO->CLKCR & 0xFFFF8100u) | init->clock_div | init->clock_power_save
                  | init->clock_bypass | init->bus_wide | init->clock_edge | init->hw_flow_control;
}

/* Fill and apply g_sdio_init with the given divider and bus width */
static void sdio_set_clock(uint8_t clock_div, uint32_t bus_wide)
{
    g_sdio_init.clock_div = clock_div;
    g_sdio_init.clock_edge = 0;
    g_sdio_init.clock_bypass = 0;
    g_sdio_init.clock_power_save = 0;
    g_sdio_init.bus_wide = bus_wide;
    g_sdio_init.hw_flow_control = 0;
    sdio_apply_init(&g_sdio_init);
}

/*
 * orig: 0x080240D6 (SDIO_SendCommand)
 * inlines: 0x080240B6 SDIO_ClockCmd, 0x080240BE SDIO_SetPowerState,
 *          0x080240C4 SDIO_GetPowerState, 0x080240CE SDIO_DMACmd,
 *          0x080240FE SDIO_GetCommandResponse, 0x08024106 SDIO_GetResponse,
 *          0x0802414A SDIO_ReadData, 0x0802416E SDIO_GetFlagStatus,
 *          0x08024188 SDIO_ClearFlag, 0x0802418E SDIO_GetITStatus,
 *          0x080241A8 SDIO_ClearITPendingBit (direct register accesses below)
 */
static void sdio_apply_command(const sdio_cmd_t *cmd)
{
    SDIO->ARG = cmd->argument;
    SDIO->CMD = (SDIO->CMD & 0xFFFFF800u) | cmd->cmd_index | cmd->response | cmd->wait | cmd->cpsm;
}

/* Fill and send g_sdio_cmd */
static void sdio_send_command(uint32_t argument, uint32_t index, uint32_t response)
{
    g_sdio_cmd.argument = argument;
    g_sdio_cmd.cmd_index = index;
    g_sdio_cmd.response = response;
    g_sdio_cmd.wait = 0;
    g_sdio_cmd.cpsm = CPSM_ENABLE;
    sdio_apply_command(&g_sdio_cmd);
}

/* orig: 0x0802411C (SDIO_DataConfig) */
static void sdio_apply_data_config(const sdio_data_t *data)
{
    SDIO->DTIMER = data->timeout;
    SDIO->DLEN = data->length;
    SDIO->DCTRL = (SDIO->DCTRL & 0xFFFFFF08u) | data->block_size | data->direction
                  | data->mode | data->dpsm;
}

/* Fill and apply g_sdio_data */
static void sdio_data_config(uint32_t length, uint32_t block_size, uint32_t direction)
{
    g_sdio_data.timeout = DATA_TIMEOUT_MAX;
    g_sdio_data.length = length;
    g_sdio_data.block_size = block_size;
    g_sdio_data.direction = direction;
    g_sdio_data.mode = 0;
    g_sdio_data.dpsm = DPSM_ENABLE;
    sdio_apply_data_config(&g_sdio_data);
}

/* orig: 0x08024150 (SDIO_ITConfig) */
static void sdio_it_config(uint32_t mask, int enable)
{
    if (enable) {
        SDIO->MASK |= mask;
    } else {
        SDIO->MASK &= ~mask;
    }
}

/* orig: 0x08024074 (SDIO_DeInit); inlines: 0x0801FFDA RCC_APB2PeriphResetCmd */
static void sdio_deinit(void)
{
    RCC->APB2RSTR |= RCC_APB2_SDIO;
    RCC->APB2RSTR &= ~RCC_APB2_SDIO;
}

/* orig: 0x08024260 (DMA_ClearFlag) */
static void dma_clear_flags(uint32_t flags)
{
    if (flags & 0x10000000u) {
        DMA2->IFCR = flags;
    } else {
        DMA1->IFCR = flags;
    }
}

/*
 * orig: 0x08023DE8 (to card) / 0x08023E58 (to host)
 * inlines: 0x080241F0 DMA_Init, 0x0802422E DMA_Cmd, 0x0802424A DMA_ITConfig
 */
static void sd_dma_config(const void *buffer, uint32_t size, uint32_t direction)
{
    dma_clear_flags(DMA2_FLAGS_CH4);
    DMA2_Channel4->CCR &= 0xFFFEu;
    DMA2_Channel4->CCR = (DMA2_Channel4->CCR & 0xFFFF800Fu) | direction | DMA_MINC
                         | DMA_PSIZE_32 | DMA_MSIZE_32 | DMA_PRIORITY_HIGH;
    DMA2_Channel4->CNDTR = size >> 2;
    DMA2_Channel4->CPAR = (uint32_t)&SDIO->FIFO;
    DMA2_Channel4->CMAR = (uint32_t)buffer;
    DMA2_Channel4->CCR |= DMA_CCR_TCIE_BIT;
    DMA2_Channel4->CCR |= DMA_CCR_EN_BIT;
}

/* ---- Low-level pin/clock setup ------------------------------------------- */

/*
 * orig: 0x08023D56 - PC8 = D0, PC12 = CK, PD2 = CMD
 * (RCC_AHBPeriphClockCmd 0x0801FF80 and RCC_APB2PeriphClockCmd 0x0801FF9E inlined)
 */
static void sd_low_level_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOC_D;
    gpio_set_af(GPIOC, 8, GPIO_AF_SDIO);
    gpio_set_af(GPIOC, 12, GPIO_AF_SDIO);
    gpio_set_af(GPIOD, 2, GPIO_AF_SDIO);

    gpio_init_pins(GPIOC, 0x0100, GPIO_MODE_AF, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
    gpio_init_pins(GPIOD, 0x0004, GPIO_MODE_AF, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
    gpio_init_pins(GPIOC, 0x1000, GPIO_MODE_AF, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);

    RCC->APB2ENR |= RCC_APB2_SDIO;
    RCC->AHBENR |= RCC_AHBENR_DMA2;
}

/* orig: 0x08023CD8 */
void sd_low_level_deinit(void)
{
    SDIO->CLKCR &= ~CLKCR_CLKEN;
    SDIO->POWER = POWER_OFF;
    sdio_deinit();
    RCC->APB2ENR &= ~RCC_APB2_SDIO;
    gpio_set_af(GPIOC, 8, 0);
    gpio_set_af(GPIOC, 12, 0);
    gpio_set_af(GPIOD, 2, 0);

    gpio_init_pins(GPIOC, 0x0100, GPIO_MODE_IN, 0, 0, GPIO_NOPULL);
    gpio_init_pins(GPIOD, 0x0004, GPIO_MODE_IN, 0, 0, GPIO_NOPULL);
    gpio_init_pins(GPIOC, 0x1000, GPIO_MODE_IN, 0, 0, GPIO_NOPULL);
}

/* orig: 0x08022932 */
void sd_power_off(void)
{
    SDIO->POWER = POWER_OFF;
}

/* orig: 0x080226D6 - no card-detect switch is wired, so a card is always assumed */
uint8_t sd_detect(void)
{
    return 1;
}

/* ---- Command response checks --------------------------------------------- */

/* orig: 0x08023558 */
static sd_error_t cmd_error(void)
{
    uint32_t timeout = CMDSENT_TIMEOUT;
    uint32_t polls = RESPONSE_POLLS;

    while (timeout != 0 && (SDIO->STA & STA_CMDSENT) == 0) {
        timeout--;
        if (--polls == 0) {
            return SD_CMD_RSP_TIMEOUT;
        }
    }
    if (timeout == 0) {
        return SD_CMD_RSP_TIMEOUT;
    }
    SDIO->ICR = STATIC_FLAGS;
    return SD_OK;
}

/* orig: 0x080235A4 */
static sd_error_t cmd_resp7_error(void)
{
    uint32_t timeout = CMDSENT_TIMEOUT;
    uint32_t status = SDIO->STA;
    uint32_t polls = RESPONSE_POLLS;

    while ((status & CMD_RESPONSE_DONE) == 0 && timeout != 0) {
        timeout--;
        status = SDIO->STA;
        if (--polls == 0) {
            SDIO->ICR = STA_CTIMEOUT;
            return SD_CMD_RSP_TIMEOUT;
        }
    }
    if (timeout == 0 || (status & STA_CTIMEOUT)) {
        SDIO->ICR = STA_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }
    if (status & STA_CMDREND) {
        SDIO->ICR = STA_CMDREND;
    }
    return SD_OK;
}

/* Wait for a command response; 0 after RESPONSE_POLLS polls without one */
static uint32_t wait_command_response(void)
{
    uint32_t status = SDIO->STA;
    uint32_t polls = RESPONSE_POLLS;

    while ((status & CMD_RESPONSE_DONE) == 0) {
        status = SDIO->STA;
        if (--polls == 0) {
            return 0;
        }
    }
    return status;
}

/* orig: 0x0802361C */
static sd_error_t cmd_resp1_error(uint8_t command)
{
    static const struct {
        uint32_t bit;
        sd_error_t error;
    } r1_errors[] = {
        { 1u << 31, SD_ADDR_OUT_OF_RANGE },   { 1u << 30, SD_ADDR_MISALIGNED },
        { 1u << 29, SD_BLOCK_LEN_ERR },       { 1u << 28, SD_ERASE_SEQ_ERR },
        { 1u << 27, SD_BAD_ERASE_PARAM },     { 1u << 26, SD_WRITE_PROT_VIOLATION },
        { 1u << 24, SD_LOCK_UNLOCK_FAILED },  { 1u << 23, SD_COM_CRC_FAILED },
        { 1u << 22, SD_ILLEGAL_CMD },         { 1u << 21, SD_CARD_ECC_FAILED },
        { 1u << 20, SD_CC_ERROR },            { 1u << 19, SD_GENERAL_UNKNOWN_ERROR },
        { 1u << 18, SD_STREAM_READ_UNDERRUN },{ 1u << 17, SD_STREAM_WRITE_OVERRUN },
        { 1u << 16, SD_CID_CSD_OVERWRITE },   { 1u << 15, SD_WP_ERASE_SKIP },
        { 1u << 14, SD_CARD_ECC_DISABLED },   { 1u << 13, SD_ERASE_RESET },
        { 1u << 3,  SD_AKE_SEQ_ERROR },
    };
    uint32_t status = wait_command_response();

    if (status == 0 || (status & STA_CTIMEOUT)) {
        SDIO->ICR = STA_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }
    if (status & STA_CCRCFAIL) {
        SDIO->ICR = STA_CCRCFAIL;
        return SD_CMD_CRC_FAIL;
    }
    if ((SDIO->RESPCMD & 0xFFu) != command) {
        return SD_ILLEGAL_CMD;
    }
    SDIO->ICR = STATIC_FLAGS;

    uint32_t r1 = SDIO->RESP1;
    if ((r1 & R1_ERRORBITS) == 0) {
        return SD_OK;
    }
    for (unsigned i = 0; i < sizeof r1_errors / sizeof r1_errors[0]; i++) {
        if (r1 & r1_errors[i].bit) {
            return r1_errors[i].error;
        }
    }
    return SD_OK;
}

/* orig: 0x08023752 */
static sd_error_t cmd_resp3_error(void)
{
    uint32_t status = wait_command_response();

    if (status == 0 || (status & STA_CTIMEOUT)) {
        SDIO->ICR = STA_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }
    SDIO->ICR = STATIC_FLAGS;
    return SD_OK;
}

/* orig: 0x080237AA */
static sd_error_t cmd_resp2_error(void)
{
    uint32_t status = wait_command_response();

    if (status == 0 || (status & STA_CTIMEOUT)) {
        SDIO->ICR = STA_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }
    if (status & STA_CCRCFAIL) {
        SDIO->ICR = STA_CCRCFAIL;
        return SD_CMD_CRC_FAIL;
    }
    SDIO->ICR = STATIC_FLAGS;
    return SD_OK;
}

/* orig: 0x0802381C */
static sd_error_t cmd_resp6_error(uint8_t command, uint16_t *new_rca)
{
    uint32_t status = wait_command_response();

    if (status == 0 || (status & STA_CTIMEOUT)) {
        SDIO->ICR = STA_CTIMEOUT;
        return SD_CMD_RSP_TIMEOUT;
    }
    if (status & STA_CCRCFAIL) {
        SDIO->ICR = STA_CCRCFAIL;
        return SD_CMD_CRC_FAIL;
    }
    if ((SDIO->RESPCMD & 0xFFu) != command) {
        return SD_ILLEGAL_CMD;
    }
    SDIO->ICR = STATIC_FLAGS;

    uint32_t r6 = SDIO->RESP1;
    if ((r6 & R6_ERRORBITS) == 0) {
        *new_rca = (uint16_t)(r6 >> 16);
        return SD_OK;
    }
    if (r6 & (1u << 13)) {
        return SD_GENERAL_UNKNOWN_ERROR;
    }
    if (r6 & (1u << 14)) {
        return SD_ILLEGAL_CMD;
    }
    if (r6 & (1u << 15)) {
        return SD_COM_CRC_FAILED;
    }
    return SD_OK;
}

/* ---- Card identification -------------------------------------------------- */

/* orig: 0x080226E6 */
static sd_error_t sd_power_on(void)
{
    sd_error_t err;
    uint32_t response = 0;
    uint32_t count = 0;
    uint32_t valid_voltage = 0;
    uint32_t sd_type = 0;

    sdio_set_clock(CLKDIV_INIT, SD_BUS_WIDE_1B);
    SDIO->POWER = POWER_ON;
    SDIO->CLKCR |= CLKCR_CLKEN;

    sdio_send_command(0, CMD0_GO_IDLE_STATE, RESP_NONE);
    err = cmd_error();
    if (err != SD_OK) {
        return err;
    }

    /* CMD8 only answers on v2.0+ cards. */
    sdio_send_command(CMD8_CHECK_PATTERN, CMD8_SEND_IF_COND, RESP_SHORT);
    if (cmd_resp7_error() == SD_OK) {
        g_sd_card_type = SD_TYPE_STD_CAPACITY_V2_0;
        sd_type = OCR_HCS;
    } else {
        sdio_send_command(0, CMD55_APP_CMD, RESP_SHORT);
        (void)cmd_resp1_error(CMD55_APP_CMD);
    }

    sdio_send_command(0, CMD55_APP_CMD, RESP_SHORT);
    err = cmd_resp1_error(CMD55_APP_CMD);
    if (err != SD_OK) {
        return err;   /* not an SD card (MMC is not supported) */
    }

    while (valid_voltage == 0 && count < MAX_VOLT_TRIAL) {
        sdio_send_command(0, CMD55_APP_CMD, RESP_SHORT);
        err = cmd_resp1_error(CMD55_APP_CMD);
        if (err != SD_OK) {
            return err;
        }
        sdio_send_command(ACMD41_VOLTAGE_WINDOW | sd_type, ACMD41_SD_SEND_OP_COND, RESP_SHORT);
        err = cmd_resp3_error();
        if (err != SD_OK) {
            return err;
        }
        response = SDIO->RESP1;
        valid_voltage = response >> 31;
        count++;
    }
    if (count >= MAX_VOLT_TRIAL) {
        return SD_INVALID_VOLTRANGE;
    }
    if (response & OCR_HCS) {
        g_sd_card_type = SD_TYPE_HIGH_CAPACITY;
    }
    return err;
}

/* orig: 0x08022942 */
static sd_error_t sd_initialize_cards(void)
{
    sd_error_t err;
    uint16_t new_rca = 1;

    if ((SDIO->POWER & 3u) == POWER_OFF) {
        return SD_REQUEST_NOT_APPLICABLE;
    }

    if (g_sd_card_type != SD_TYPE_SDIO) {
        sdio_send_command(0, CMD2_ALL_SEND_CID, RESP_LONG);
        err = cmd_resp2_error();
        if (err != SD_OK) {
            return err;
        }
        g_sd_cid[0] = SDIO->RESP1;
        g_sd_cid[1] = SDIO->RESP2;
        g_sd_cid[2] = SDIO->RESP3;
        g_sd_cid[3] = SDIO->RESP4;
    }

    if (g_sd_card_type == SD_TYPE_STD_CAPACITY_V1_1 || g_sd_card_type == SD_TYPE_STD_CAPACITY_V2_0
        || g_sd_card_type == SD_TYPE_SDIO_COMBO || g_sd_card_type == SD_TYPE_HIGH_CAPACITY) {
        sdio_send_command(0, CMD3_SEND_RELATIVE_ADDR, RESP_SHORT);
        err = cmd_resp6_error(CMD3_SEND_RELATIVE_ADDR, &new_rca);
        if (err != SD_OK) {
            return err;
        }
    }

    if (g_sd_card_type != SD_TYPE_SDIO) {
        g_sd_rca = new_rca;
        sdio_send_command((uint32_t)new_rca << 16, CMD9_SEND_CSD, RESP_LONG);
        err = cmd_resp2_error();
        if (err != SD_OK) {
            return err;
        }
        g_sd_csd[0] = SDIO->RESP1;
        g_sd_csd[1] = SDIO->RESP2;
        g_sd_csd[2] = SDIO->RESP3;
        g_sd_csd[3] = SDIO->RESP4;
    }
    return SD_OK;
}

/* orig: 0x08022AE4 */
static sd_error_t sd_get_card_info(sd_card_info_t *info)
{
    const uint32_t *csd = g_sd_csd;
    const uint32_t *cid = g_sd_cid;
    uint32_t tmp;

    info->card_type = (uint8_t)g_sd_card_type;
    info->rca = (uint16_t)g_sd_rca;

    tmp = csd[0] >> 24;
    info->csd.csd_structure = (uint8_t)(tmp >> 6);
    info->csd.spec_version = (uint8_t)((tmp & 0x3C) >> 2);
    info->csd.reserved1 = (uint8_t)(tmp & 0x03);
    info->csd.taac = (uint8_t)(csd[0] >> 16);
    info->csd.nsac = (uint8_t)(csd[0] >> 8);
    info->csd.tran_speed = (uint8_t)csd[0];

    info->csd.ccc = (uint16_t)((csd[1] >> 24) << 4);
    info->csd.ccc |= (uint16_t)(((csd[1] >> 16) & 0xFF) >> 4);
    info->csd.read_bl_len = (uint8_t)((csd[1] >> 16) & 0x0F);

    tmp = (csd[1] >> 8) & 0xFF;
    info->csd.read_bl_partial = (uint8_t)(tmp >> 7);
    info->csd.write_blk_misalign = (uint8_t)((tmp & 0x40) >> 6);
    info->csd.read_blk_misalign = (uint8_t)((tmp & 0x20) >> 5);
    info->csd.dsr_imp = (uint8_t)((tmp & 0x10) >> 4);
    info->csd.reserved2 = 0;

    if (g_sd_card_type == SD_TYPE_STD_CAPACITY_V1_1 || g_sd_card_type == SD_TYPE_STD_CAPACITY_V2_0) {
        info->csd.c_size = (tmp & 0x03) << 10;
        info->csd.c_size |= (csd[1] & 0xFF) << 2;
        tmp = csd[2] >> 24;
        info->csd.c_size |= tmp >> 6;
        info->csd.vdd_r_curr_min = (uint8_t)((tmp & 0x38) >> 3);
        info->csd.vdd_r_curr_max = (uint8_t)(tmp & 0x07);
        tmp = (csd[2] >> 16) & 0xFF;
        info->csd.vdd_w_curr_min = (uint8_t)(tmp >> 5);
        info->csd.vdd_w_curr_max = (uint8_t)((tmp & 0x1C) >> 2);
        info->csd.c_size_mult = (uint8_t)((tmp & 0x03) << 1);
        tmp = (csd[2] >> 8) & 0xFF;
        info->csd.c_size_mult |= (uint8_t)(tmp >> 7);

        info->capacity = info->csd.c_size + 1u;
        info->capacity *= 1u << (info->csd.c_size_mult + 2);
        info->block_size = 1u << info->csd.read_bl_len;
        info->capacity *= info->block_size;
    } else if (g_sd_card_type == SD_TYPE_HIGH_CAPACITY) {
        info->csd.c_size = (csd[1] & 0x3F) << 16;
        info->csd.c_size |= (csd[2] >> 24) << 8;
        info->csd.c_size |= (csd[2] >> 16) & 0xFF;
        tmp = csd[2] >> 8;   /* not masked to a byte in the original */
        /* 32-bit product, as in the original: wraps for cards of 4 GB and up */
        info->capacity = (uint32_t)((info->csd.c_size + 1u) * 512u * 1024u);
        info->block_size = 512;
    }
    /* Other card types keep 'tmp' from CSD[1] above, as the original does. */

    info->csd.erase_blk_en = (uint8_t)((tmp & 0x40) >> 6);
    info->csd.sector_size = (uint8_t)((tmp & 0x3F) << 1);
    tmp = csd[2] & 0xFF;
    info->csd.sector_size |= (uint8_t)(tmp >> 7);
    info->csd.wp_grp_size = (uint8_t)(tmp & 0x7F);

    tmp = csd[3] >> 24;
    info->csd.wp_grp_enable = (uint8_t)(tmp >> 7);
    info->csd.default_ecc = (uint8_t)((tmp & 0x60) >> 5);
    info->csd.r2w_factor = (uint8_t)((tmp & 0x1C) >> 2);
    info->csd.write_bl_len = (uint8_t)((tmp & 0x03) << 2);
    tmp = (csd[3] >> 16) & 0xFF;
    info->csd.write_bl_len |= (uint8_t)(tmp >> 6);
    info->csd.write_bl_partial = (uint8_t)((tmp & 0x20) >> 5);
    info->csd.reserved3 = 0;
    info->csd.content_prot_app = (uint8_t)(tmp & 0x01);
    tmp = (csd[3] >> 8) & 0xFF;
    info->csd.file_format_grp = (uint8_t)(tmp >> 7);
    info->csd.copy = (uint8_t)((tmp & 0x40) >> 6);
    info->csd.perm_write_protect = (uint8_t)((tmp & 0x20) >> 5);
    info->csd.tmp_write_protect = (uint8_t)((tmp & 0x10) >> 4);
    info->csd.file_format = (uint8_t)((tmp & 0x0C) >> 2);
    info->csd.ecc = (uint8_t)(tmp & 0x03);
    info->csd.crc = (uint8_t)((csd[3] & 0xFF) >> 1);
    info->csd.reserved4 = 1;

    info->cid.manufacturer_id = (uint8_t)(cid[0] >> 24);
    info->cid.oem_id = (uint16_t)(((cid[0] >> 16) & 0xFF) << 8);
    info->cid.oem_id |= (uint16_t)((cid[0] >> 8) & 0xFF);
    info->cid.product_name1 = cid[0] << 24;
    info->cid.product_name1 |= (cid[1] >> 24) << 16;
    info->cid.product_name1 |= ((cid[1] >> 16) & 0xFF) << 8;
    info->cid.product_name1 |= (cid[1] >> 8) & 0xFF;
    info->cid.product_name2 = (uint8_t)cid[1];
    info->cid.product_rev = (uint8_t)(cid[2] >> 24);
    info->cid.product_sn = ((cid[2] >> 16) & 0xFF) << 24;
    info->cid.product_sn |= ((cid[2] >> 8) & 0xFF) << 16;
    info->cid.product_sn |= (cid[2] & 0xFF) << 8;
    info->cid.product_sn |= cid[3] >> 24;
    info->cid.reserved1 |= (uint8_t)(((cid[3] >> 16) & 0xFF) >> 4);   /* OR, as the original */
    info->cid.manufacture_date = (uint16_t)(((cid[3] >> 16) & 0x0F) << 8);
    info->cid.manufacture_date |= (uint16_t)((cid[3] >> 8) & 0xFF);
    info->cid.crc = (uint8_t)((cid[3] & 0xFF) >> 1);
    info->cid.reserved2 = 1;

    return SD_OK;
}

/* orig: 0x08022F96 */
static sd_error_t sd_select_deselect(uint32_t address)
{
    sdio_send_command(address, CMD7_SELECT_CARD, RESP_SHORT);
    return cmd_resp1_error(CMD7_SELECT_CARD);
}

/* orig: 0x08023A8C - the rca argument is unused (g_sd_rca is read instead) */
static sd_error_t sd_find_scr(uint16_t rca, uint32_t scr[2])
{
    sd_error_t err;
    uint32_t index = 0;
    uint32_t raw[2] = { 0, 0 };
    uint32_t polls;

    (void)rca;
    sdio_send_command(8, CMD16_SET_BLOCKLEN, RESP_SHORT);
    err = cmd_resp1_error(CMD16_SET_BLOCKLEN);
    if (err != SD_OK) {
        return err;
    }
    sdio_send_command(g_sd_rca << 16, CMD55_APP_CMD, RESP_SHORT);
    err = cmd_resp1_error(CMD55_APP_CMD);
    if (err != SD_OK) {
        return err;
    }

    sdio_data_config(8, DCTRL_BLOCK_8B, DCTRL_DIR_TO_HOST);
    sdio_send_command(0, ACMD51_SEND_SCR, RESP_SHORT);
    err = cmd_resp1_error(ACMD51_SEND_SCR);
    if (err != SD_OK) {
        return err;
    }

    for (polls = RESPONSE_POLLS; polls != 0; polls--) {
        if (SDIO->STA & SCR_READ_DONE) {
            if (SDIO->STA & STA_DTIMEOUT) {
                SDIO->ICR = STA_DTIMEOUT;
                return SD_DATA_TIMEOUT;
            }
            if (SDIO->STA & STA_DCRCFAIL) {
                SDIO->ICR = STA_DCRCFAIL;
                return SD_DATA_CRC_FAIL;
            }
            if (SDIO->STA & STA_RXOVERR) {
                SDIO->ICR = STA_RXOVERR;
                return SD_RX_OVERRUN;
            }
            if (SDIO->STA & STA_STBITERR) {
                SDIO->ICR = STA_STBITERR;
                return SD_START_BIT_ERR;
            }
            SDIO->ICR = STATIC_FLAGS;
            scr[1] = __builtin_bswap32(raw[0]);
            scr[0] = __builtin_bswap32(raw[1]);
            return SD_OK;
        }
        if (SDIO->STA & STA_RXDAVL) {
            raw[index++] = SDIO->FIFO;
        }
    }
    SDIO->ICR = STA_DTIMEOUT;
    return SD_DATA_TIMEOUT;
}

/* orig: 0x080238F0 */
static sd_error_t sd_set_wide_bus(int enable)
{
    sd_error_t err;
    uint32_t scr[2] = { 0, 0 };

    if (SDIO->RESP1 & R1_CARD_LOCKED) {
        return SD_LOCK_UNLOCK_FAILED;
    }
    err = sd_find_scr((uint16_t)g_sd_rca, scr);
    if (err != SD_OK) {
        return err;
    }

    uint32_t supported = (enable == 1) ? SCR_WIDE_BUS_SUPPORT : SCR_SINGLE_BUS_SUPPORT;
    if ((scr[1] & supported) == 0) {
        return SD_REQUEST_NOT_APPLICABLE;
    }
    sdio_send_command(g_sd_rca << 16, CMD55_APP_CMD, RESP_SHORT);
    err = cmd_resp1_error(CMD55_APP_CMD);
    if (err != SD_OK) {
        return err;
    }
    sdio_send_command((enable == 1) ? 2u : 0u, ACMD6_SET_BUS_WIDTH, RESP_SHORT);
    return cmd_resp1_error(ACMD6_SET_BUS_WIDTH);
}

/* orig: 0x08022ECE */
static sd_error_t sd_enable_wide_bus_operation(uint32_t mode)
{
    sd_error_t err = SD_OK;

    if (g_sd_card_type == SD_TYPE_MULTIMEDIA) {
        return SD_UNSUPPORTED_FEATURE;
    }
    if (g_sd_card_type == SD_TYPE_STD_CAPACITY_V1_1 || g_sd_card_type == SD_TYPE_STD_CAPACITY_V2_0
        || g_sd_card_type == SD_TYPE_HIGH_CAPACITY) {
        if (mode == SD_BUS_WIDE_8B) {
            return SD_UNSUPPORTED_FEATURE;
        }
        if (mode == SD_BUS_WIDE_4B) {
            err = sd_set_wide_bus(1);
            if (err == SD_OK) {
                sdio_set_clock(CLKDIV_TRANSFER, SD_BUS_WIDE_4B);
            }
        } else {
            err = sd_set_wide_bus(0);
            if (err == SD_OK) {
                sdio_set_clock(CLKDIV_TRANSFER, SD_BUS_WIDE_1B);
            }
        }
    }
    return err;
}

/* orig: 0x080225D8 */
sd_error_t sd_init(void)
{
    sd_error_t err;

    sd_low_level_init();
    sdio_deinit();

    err = sd_power_on();
    if (err != SD_OK) {
        return err;
    }
    err = sd_initialize_cards();
    if (err != SD_OK) {
        return err;
    }

    sdio_set_clock(CLKDIV_TRANSFER, SD_BUS_WIDE_1B);

    err = sd_get_card_info(&g_sd_card_info);
    if (err == SD_OK) {
        err = sd_select_deselect((uint32_t)g_sd_card_info.rca << 16);
    }
    if (err == SD_OK) {
        err = sd_enable_wide_bus_operation(SD_BUS_WIDE_1B);   /* board wires D0 only */
    }
    return err;
}

/* ---- Status --------------------------------------------------------------- */

/* orig: 0x08023408 */
static sd_error_t sd_send_status(uint32_t *card_status)
{
    sd_error_t err;

    if (card_status == NULL) {
        return SD_INVALID_PARAMETER;
    }
    sdio_send_command(g_sd_rca << 16, CMD13_SEND_STATUS, RESP_SHORT);
    err = cmd_resp1_error(CMD13_SEND_STATUS);
    if (err == SD_OK) {
        *card_status = SDIO->RESP1;
    }
    return err;
}

/* orig: 0x080226AC - returns the CURRENT_STATE field of the card status */
static uint8_t sd_get_state(void)
{
    uint32_t card_status = 0;

    if (sd_detect() == 1) {
        if (sd_send_status(&card_status) != SD_OK) {
            return CARD_STATE_ERROR;
        }
        return (uint8_t)((card_status >> 9) & 0x0F);
    }
    return CARD_STATE_ERROR;
}

/* orig: 0x0802268A */
uint8_t sd_get_transfer_status(void)
{
    uint8_t state = sd_get_state();

    if (state == CARD_STATE_TRANSFER) {
        return SD_TRANSFER_OK;
    }
    if (state == CARD_STATE_ERROR) {
        return SD_TRANSFER_ERROR;
    }
    return SD_TRANSFER_BUSY;
}

/* ---- Data transfer -------------------------------------------------------- */

/* orig: 0x08022FCE - starts a DMA read; completion is not awaited here */
sd_error_t sd_read_block(uint8_t *buffer, uint64_t address, uint16_t block_size)
{
    sd_error_t err;

    g_sd_transfer_error = SD_OK;
    g_sd_transfer_end = 0;
    g_sd_stop_condition = 0;
    SDIO->DCTRL = 0;

    if (g_sd_card_type == SD_TYPE_HIGH_CAPACITY) {
        block_size = 512;
        address /= 512;
    }

    sdio_send_command(block_size, CMD16_SET_BLOCKLEN, RESP_SHORT);
    err = cmd_resp1_error(CMD16_SET_BLOCKLEN);
    if (err != SD_OK) {
        return err;
    }

    sdio_data_config(block_size, DCTRL_BLOCK_512B, DCTRL_DIR_TO_HOST);
    sdio_send_command((uint32_t)address, CMD17_READ_SINGLE_BLOCK, RESP_SHORT);
    err = cmd_resp1_error(CMD17_READ_SINGLE_BLOCK);
    if (err == SD_OK) {
        sdio_it_config(DATA_IT_MASK, 1);
        SDIO->DCTRL |= DCTRL_DMAEN;
        sd_dma_config(buffer, block_size, 0);
    }
    return err;
}

/* orig: 0x08023100 */
sd_error_t sd_write_multi_blocks(const uint8_t *buffer, uint64_t address,
                                 uint16_t block_size, uint32_t block_count)
{
    sd_error_t err;

    g_sd_transfer_error = SD_OK;
    g_sd_transfer_end = 0;
    g_sd_stop_condition = 1;
    SDIO->DCTRL = 0;

    if (g_sd_card_type == SD_TYPE_HIGH_CAPACITY) {
        block_size = 512;
        address /= 512;
    }

    sdio_send_command(block_size, CMD16_SET_BLOCKLEN, RESP_SHORT);
    err = cmd_resp1_error(CMD16_SET_BLOCKLEN);
    if (err != SD_OK) {
        return err;
    }
    sdio_send_command(g_sd_rca << 16, CMD55_APP_CMD, RESP_SHORT);
    err = cmd_resp1_error(CMD55_APP_CMD);
    if (err != SD_OK) {
        return err;
    }
    sdio_send_command(block_count, ACMD23_SET_WR_BLK_ERASE, RESP_SHORT);
    err = cmd_resp1_error(ACMD23_SET_WR_BLK_ERASE);
    if (err != SD_OK) {
        return err;
    }
    sdio_send_command((uint32_t)address, CMD25_WRITE_MULTI_BLOCK, RESP_SHORT);
    err = cmd_resp1_error(CMD25_WRITE_MULTI_BLOCK);
    if (err != SD_OK) {
        return err;
    }

    sdio_data_config(block_size * block_count, DCTRL_BLOCK_512B, 0);
    sdio_it_config(DATA_IT_MASK, 1);
    SDIO->DCTRL |= DCTRL_DMAEN;
    sd_dma_config(buffer, block_size * block_count, DMA_DIR_FROM_MEMORY);
    return SD_OK;
}

/* orig: 0x080233BE */
static sd_error_t sd_stop_transfer(void)
{
    sdio_send_command(0, CMD12_STOP_TRANSMISSION, RESP_SHORT);
    return cmd_resp1_error(CMD12_STOP_TRANSMISSION);
}

/*
 * orig: 0x080232E0 - wait (at most 1000 polls each) for the DMA/transfer to
 * end and for the transmitter to go idle; a timeout returns SD_DATA_TIMEOUT
 * at once, without the stop command.
 */
sd_error_t sd_wait_write_operation(void)
{
    sd_error_t err = SD_OK;

    g_sd_timeout = WRITE_WAIT_POLLS;
    while (g_sd_dma_end == 0 && g_sd_transfer_end == 0 && g_sd_transfer_error == SD_OK) {
        if (--g_sd_timeout == 0) {
            return SD_DATA_TIMEOUT;
        }
    }
    g_sd_dma_end = 0;

    g_sd_timeout = WRITE_WAIT_POLLS;
    while (SDIO->STA & STA_TXACT) {
        if (--g_sd_timeout == 0) {
            return SD_DATA_TIMEOUT;
        }
    }
    if (g_sd_stop_condition == 1) {
        err = sd_stop_transfer();
    }
    SDIO->ICR = STATIC_FLAGS;
    if (g_sd_transfer_error != SD_OK) {
        return (sd_error_t)g_sd_transfer_error;
    }
    return err;
}

/* ---- Interrupts ----------------------------------------------------------- */

/* orig: 0x08023476 */
sd_error_t sd_process_irq(void)
{
    if (SDIO->STA & STA_DATAEND) {
        g_sd_transfer_error = SD_OK;
        SDIO->ICR = STA_DATAEND;
        g_sd_transfer_end = 1;
    } else if (SDIO->STA & STA_DCRCFAIL) {
        SDIO->ICR = STA_DCRCFAIL;
        g_sd_transfer_error = SD_DATA_CRC_FAIL;
    } else if (SDIO->STA & STA_DTIMEOUT) {
        SDIO->ICR = STA_DTIMEOUT;
        g_sd_transfer_error = SD_DATA_TIMEOUT;
    } else if (SDIO->STA & STA_RXOVERR) {
        SDIO->ICR = STA_RXOVERR;
        g_sd_transfer_error = SD_RX_OVERRUN;
    } else if (SDIO->STA & STA_TXUNDERR) {
        SDIO->ICR = STA_TXUNDERR;
        g_sd_transfer_error = SD_TX_UNDERRUN;
    } else if (SDIO->STA & STA_STBITERR) {
        SDIO->ICR = STA_STBITERR;
        g_sd_transfer_error = SD_START_BIT_ERR;
    }
    sdio_it_config(DATA_IT_ALL, 0);
    return (sd_error_t)g_sd_transfer_error;
}

/* orig: 0x08023524 */
void sd_process_dma_irq(void)
{
    if (DMA2->ISR & DMA2_FLAG_TC4) {
        g_sd_dma_end = 1;
        dma_clear_flags(DMA2_FLAGS_CH4);
    }
}
