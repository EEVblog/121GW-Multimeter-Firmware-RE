/*
 * EEVblog 121GW SD-card bootloader.
 *
 * At power-on this checks the keys. Normally it jumps straight to the
 * application at 0x08006000. If the update key combination is held, it shows
 * "-IAP-" and waits (about 8 s) for the start key, then copies EEVBlog.bin from
 * the SD card into application flash, verifying every 128-byte half page.
 */
#include <stdint.h>
#include "board.h"
#include "flash_if.h"
#include "lcd.h"
#include "sd_card.h"
#include "sys_init.h"
#include "ff.h"
#include "stm32l1xx.h"

#define READ_CHUNK      512u
#define PROGRESS_STEPS  25u

static FATFS fs;                              /* orig: 0x20000104 */
static FIL update_file;                       /* orig: 0x20000334 */
static uint32_t read_buffer[READ_CHUNK / 4];  /* orig: 0x20000558 */
static uint32_t update_file_size;             /* orig: 0x2000085C */

typedef void (*app_entry_t)(void);

/* orig: 0x08004B84 - empty in the original image (a progress/debug hook) */
static void boot_stage(uint8_t stage)
{
    (void)stage;
}

/* orig: 0x08004B04 - the SD init result is ignored; f_mount() cannot fail for drive 0 */
static int sd_init_and_mount(void)
{
    (void)sd_init();
    return f_mount(0, &fs) != FR_OK;
}

/*
 * orig: 0x08004AC8
 * Returns 0 if the card is ready, 1 if it never initialised, 2 if no card.
 * As in the original, the retry counter runs past zero, so 1 is never
 * returned, and sd_init_and_mount() always succeeds anyway.
 */
static uint8_t sd_open(void)
{
    uint8_t result = 0;
    int32_t tries = 0x300;
    int32_t left;
    int more;

    do {
        sd_low_level_deinit();
        int failed = sd_init_and_mount();
        left = tries;
        if (!failed) {
            break;
        }
        left = tries - 1;
        more = tries != 0;
        tries = left;
    } while (more);

    if (left == 0) {
        result = 1;
    } else if (sd_detect() == 0) {
        result = 2;
    }
    return result;
}

/* orig: 0x08004A9A - returns 0 if the update file was opened */
static int open_update_file(void)
{
    (void)f_mount(0, &fs);
    FRESULT res = f_open(&update_file, UPDATE_FILE_NAME, FA_READ);
    if (res == FR_OK) {
        update_file_size = update_file.fsize;
    }
    return res != FR_OK;
}

/*
 * orig: 0x080049D2
 * Erases the application area and programs the open file into it.
 * Returns 0 on success, 1 on a program or verify failure.
 */
static uint8_t program_application(void)
{
    uint32_t flash_address = APP_FLASH_BASE;
    UINT bytes_read;

    (void)flash_erase_application(APP_FLASH_BASE);   /* result ignored, as the original */

    uint32_t remaining = update_file_size;
    uint32_t step = update_file_size / PROGRESS_STEPS;

    for (;;) {
        uint32_t done = update_file_size - remaining;
        /* Cortex-M3 UDIV returns 0 for a zero divisor (files under 25 bytes). */
        uint32_t progress = (step != 0 ? done / step : 0) + 1;
        lcd_show_progress((uint8_t)progress);

        uint32_t chunk = (remaining < READ_CHUNK) ? remaining : READ_CHUNK;
        (void)f_read(&update_file, read_buffer, chunk, &bytes_read);

        if (flash_write_words(&flash_address, read_buffer, (uint16_t)((chunk & 0xFFFF) / 4)) != 0) {
            return 1;
        }
        remaining -= chunk;
        if (remaining == 0) {
            return 0;
        }
    }
}

static int update_keys_held(void)
{
    /* One IDR read per key, in the original order. */
    static const uint8_t released[] = { 8, 9, 10, 12, 14, 15 };

    for (unsigned i = 0; i < sizeof released; i++) {
        if ((KEYS_PORT->IDR & (1u << released[i])) == 0) {
            return 0;
        }
    }
    return (KEYS_PORT->IDR & KEYS_MUST_BE_HELD) == 0;
}

/* orig: main loop body at 0x080048BA. Returns when the application should start. */
static void run_update_mode(void)
{
    uint16_t ticks;

    lcd_clear();
    lcd_show_secondary_message(LCD_MSG_IAP);
    lcd_show_main_message(LCD_MSG_IAP);
    ticks = 0;

    for (;;) {
        /* Wait for the start key; give up after IAP_PROMPT_TICKS. */
        do {
            if (g_tick_flag) {
                g_tick_flag = 0;
                ticks++;
                if (ticks > IAP_PROMPT_TICKS) {
                    return;
                }
            }
        } while (KEYS_PORT->IDR & (1u << KEY_START_PIN));

        tim2_enable(0);
        lcd_show_main_message(LCD_MSG_DOWNLOAD);
        lcd_show_progress(0);
        ticks = 0;
        UPDATE_ACTIVE_PORT->BSRR = 1u << (UPDATE_ACTIVE_PIN + 16);

        if (sd_open() == 0) {
            boot_stage(1);
            if (open_update_file() == 0) {
                boot_stage(2);
                if (program_application() == 0) {
                    boot_stage(3);
                    lcd_show_main_message(LCD_MSG_GOOD);
                } else {
                    boot_stage(4);
                    lcd_show_main_message(LCD_MSG_FAIL);
                }
                (void)f_close(&update_file);
                sd_power_off();
                return;
            }
            (void)f_close(&update_file);
        }
        sd_power_off();
        /* Retry: TIM2 is now stopped, so this waits for the start key indefinitely. */
    }
}

/* orig: 0x0800499E */
static void jump_to_application(void)
{
    uint32_t app_stack = *(volatile uint32_t *)APP_FLASH_BASE;

    if ((app_stack & 0x2FFE0000u) == 0x20000000u) {
        app_entry_t entry = (app_entry_t)(*(volatile uint32_t *)(APP_FLASH_BASE + 4));
        __set_MSP(app_stack);
        entry();
    }
    for (;;) {
    }
}

/* orig: 0x08004850
 * inlines: 0x08004B90 (flash unlock + clear flags) */
int main(void)
{
    flash_unlock();
    flash_clear_flags(FLASH_FLAGS_ALL);
    boot_stage(0);

    g_use_hse = 1;
    g_set_voltage_range = 0;
    system_init();

    lcd_all_segments_on();

    if (update_keys_held()) {
        run_update_mode();
    }

    boot_stage(5);
    jump_to_application();
    return 0;
}
