/*
 * 121GW board definitions used by the SD-card bootloader.
 *
 * Pin roles are given by port/bit as seen in the binary. Where the schematic
 * net is not yet confirmed, the role is described by behaviour only.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l1xx.h"

/* Flash layout */
#define BL_FLASH_BASE           0x08000000u
#define APP_FLASH_BASE          0x08006000u   /* application vector table */
#define APP_FLASH_LAST_PAGE     0x0805FF00u   /* last 256-byte page erased by an update */
#define FLASH_PAGE_SIZE         0x100u
/* Bytes from APP_FLASH_BASE to the end of the last erased page (0x5A000, 360 KB) */
#define APP_FLASH_SIZE          (APP_FLASH_LAST_PAGE + FLASH_PAGE_SIZE - APP_FLASH_BASE)
#define FLASH_HALF_PAGE_WORDS   32u           /* 128-byte half page */

/* Update image on the SD card (FatFs short name, matched case-insensitively) */
#define UPDATE_FILE_NAME        "EEVBlog.bin"

/*
 * Front-panel keys: GPIOG is all inputs with pull-ups, so a pressed key reads 0.
 * Update mode is entered when PG11 and PG13 are held at power-on with
 * PG8/9/10/12/14/15 released. PG9 then starts the update.
 * (The manual shows the keys as icons; key names are to be confirmed.)
 */
#define KEYS_PORT               GPIOG
#define KEY_START_PIN           9u
#define KEYS_MUST_BE_RELEASED   ((1u << 8) | (1u << 9) | (1u << 10) | (1u << 12) | (1u << 14) | (1u << 15))
#define KEYS_MUST_BE_HELD       ((1u << 11) | (1u << 13))

/* PF15 is driven low when the update starts (role to be confirmed). */
#define UPDATE_ACTIVE_PORT      GPIOF
#define UPDATE_ACTIVE_PIN       15u

/* TIM2 ticks (~24.8 Hz at 4 MHz) to wait at the "-IAP-" prompt before booting. */
#define IAP_PROMPT_TICKS        200u

#endif /* BOARD_H */
