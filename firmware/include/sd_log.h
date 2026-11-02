/*
 * SD card logging (CSV files) and the calibration backup file.
 *
 * A log file "YYMMDDNN.csv" starts with a header (START date/time, ID
 * serial number, INTERVAL, column titles); then each record is written as
 *   No., date, time, main function/value/unit, sub-1, sub-2, remark
 * and the file ends with MAX and MIN lines. Records are collected in the
 * MEM arrays (60 at most) and flushed in blocks; every write is read back
 * and compared.
 *
 * The calibration backup copies EEPROM bytes 0..0x4AF to/from "cal.bin".
 */
#ifndef SD_LOG_H
#define SD_LOG_H

#include <stdint.h>
#include "ff.h"

#define SD_BUFFER_SIZE   0x4B0u

extern FIL g_log_file;                 /* 0x20006A60 */
extern FIL g_log_file2;                /* 0x20006C84 */
extern DIR g_log_dir;                  /* 0x20007B1C */
extern FILINFO g_log_info;             /* 0x20007B38 */
extern char *g_log_name;               /* 0x20007EC8  name of the open log file */
extern char *g_log_name2;              /* 0x20007ECC */
extern int32_t g_log_index;            /* 0x20007ED0  NN of today's next file */
extern UINT g_sd_br;                   /* 0x20007ED4 */
extern UINT g_sd_bw;                   /* 0x20007ED8 */
extern DWORD g_log_start_pos;          /* 0x20007EDC  file position before the last write */
extern DWORD g_log_end_pos;            /* 0x20007EE0  where the next write goes */
extern uint32_t g_log_bytes;           /* 0x20007EE4 */
extern uint16_t g_log_buf_len;         /* 0x20007FDA  bytes in g_sd_buffer since the last flush */
extern uint8_t g_sd_buffer[SD_BUFFER_SIZE];  /* 0x20001240  written text (for the read-back) or EEPROM image */
extern uint8_t g_sd_read_buf[0x200];   /* 0x20006EA8 */
extern uint8_t g_sd_result;            /* 0x200080F4  last FatFs result */
extern char g_sd_drive;                /* 0x200080F5  "0:" path: '0', then g_log_resync (':'), then g_sd_transfer_error (NUL) */
extern uint8_t g_log_resync;           /* 0x200080F6  also flags a failed write (rewrite from g_log_end_pos) */
extern char g_log_filename[12];        /* 0x20007BD0  "YYMMDDNN.csv" */
extern uint8_t g_log_file_yy;          /* 0x200080E2 */
extern uint8_t g_log_file_mm;          /* 0x200080E3 */
extern uint8_t g_log_file_dd;          /* 0x200080E4 */
extern uint8_t g_log_retries;          /* 0x200080E5 */
extern uint8_t g_log_summary_due;      /* 0x20008025  write the MAX/MIN lines at the next flush */
extern uint8_t g_log_header_due;       /* 0x20008026 */
extern int32_t g_log_blocks;           /* 0x20007D34  blocks of 60 records written */
extern int32_t g_log_record_saved;     /* 0x20007D48 */
extern char g_value_text[8];           /* 0x20007C4F  log number text, NUL at 0x20007C56 */
extern uint8_t g_flag_8043;            /* 0x20008043  g_manual_range before logging */
extern uint16_t g_log_tick_count;      /* 0x20007FA8  scheduler counter for the log interval */
extern uint8_t g_flag_7ffa;            /* 0x20007FFA */

uint8_t log_write(const char *text, uint16_t len);
uint8_t log_write_time(uint8_t hh, uint8_t mm, uint8_t ss);
uint8_t log_write_date(uint8_t yy, uint8_t mm, uint8_t dd);
char *value_to_text(uint32_t value, uint8_t dp);
uint8_t log_field(int16_t code, uint32_t value, uint8_t dp);
uint8_t log_number(uint32_t n);
uint8_t log_function_name(uint8_t function);
uint8_t log_value(uint8_t function, uint8_t range, int32_t value);
uint8_t log_unit(uint8_t function, uint8_t range);
uint8_t log_header_start(void);
uint8_t log_header_id(void);
uint8_t log_header_interval(void);
uint8_t log_header_groups(void);
uint8_t log_header_columns(void);
uint8_t log_write_header(void);
uint8_t log_write_record(uint16_t i);
uint8_t log_write_summary(void);
void log_track_max(void);
void log_track_min(void);
void log_track_minmax(void);
uint8_t log_write_pending(void);
uint8_t log_verify(void);
void log_name_from_date(int32_t index);
void log_name_from_count(void);
uint8_t log_find_index(const char *path);
uint8_t log_open(uint8_t second, uint8_t tries);
uint8_t log_create(uint8_t tries);
uint8_t log_reopen(void);
uint8_t log_set_timestamp(uint8_t cal_file);
uint8_t log_flush(void);
uint8_t log_flush_retry(void);
uint8_t sd_open(void);
uint8_t sd_open_retry(uint8_t tries);
uint8_t cal_file_create(uint8_t tries);
uint8_t cal_backup_save(void);
uint8_t cal_backup_restore(void);
void log_capture_value1(void);
void log_start(void);
void log_stop(uint8_t flush);
void log_toggle(uint8_t force);
void log_capture_values(void);

#endif /* SD_LOG_H */
