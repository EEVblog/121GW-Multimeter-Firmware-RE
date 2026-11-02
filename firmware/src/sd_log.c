/*
 * SD card logging (CSV files) and the calibration backup file.
 *
 * Every text field goes through log_write(), which appends it to the file
 * and to g_sd_buffer; log_verify() later reads the written block back and
 * compares it with the buffer.
 */
#include <string.h>
#include "sd_log.h"
#include "app_globals.h"
#include "buzzer.h"
#include "measure_funcs.h"
#include "display_state.h"
#include "ac_response.h"
#include "ble_packet.h"
#include "measure_scale.h"
#include "measure_funcs.h"
#include "display_symbols.h"
#include "display_text.h"
#include "display_update.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_peak.h"
#include "mem_records.h"
#include "modes.h"
#include "range_select.h"
#include "rtc.h"
#include "sd_card.h"
#include "settings.h"
#include "setup_menu.h"
#include "system_init.h"

#define CAL_FILE_NAME   "cal.bin"
#define LOG_RECORD_MAX  600000u      /* logging stops after this many records */

/* Log field codes (log_field()) */
enum {
    LOG_COMMA = 0, LOG_CRLF = 1, LOG_NEG_OVERFLOW = 2, LOG_OVERFLOW = 3, LOG_MINUS = 4,
    LOG_START = 5, LOG_ID = 6, LOG_INTERVAL = 7, LOG_SEC = 8,
    LOG_GROUPS_MAIN = 11, LOG_GROUPS_SUB1 = 12, LOG_GROUPS_SUB2 = 13, LOG_REMARK = 14,
    LOG_COL_NO = 15, LOG_COL_FUNC = 16, LOG_COL_VALUE = 17, LOG_COL_UNIT = 18,
    LOG_COL_DATE = 19, LOG_COL_TIME = 20,
    LOG_FUNC_NAMES = 21,             /* 21..46: function names */
    LOG_PEAKHOLD = 81, LOG_1KHZ = 82, LOG_ACDC = 83, LOG_MAX = 91, LOG_MIN = 92,
    LOG_NUMBER = 100, LOG_INTERVAL_VALUE = 101, LOG_START_DATE = 102, LOG_START_TIME = 103,
    LOG_SERIAL = 104, LOG_RECALL_DATE = 110, LOG_RECALL_TIME = 111, LOG_DECIMAL = 200,
};

/* Fixed texts by field code (the original keeps them in initialised RAM) */
static const char *const log_text[93] = {
    [0] = ",", [1] = "\r\n", [2] = "-Overflow", [3] = "Overflow", [4] = "-",
    [5] = "START,", [6] = "ID,", [7] = "INTERVAL,", [8] = "sec,",
    [11] = ",,,MAIN,,,", [12] = "SUB-1,,,", [13] = "SUB-2,,,", [14] = "Remark,",
    [15] = "No. ,", [16] = "Func. ,", [17] = "Value,", [18] = "Unit,",
    [19] = "Date      ,", [20] = "Time    ,",
    [21] = "LowZ,", [22] = "DCV,", [23] = "ACV,", [24] = "DCmV,", [25] = "ACmV,",
    [26] = "TEMP.,", [27] = "FREQ.,", [28] = "PULSE,", [29] = "DUTY,", [30] = "OHM,",
    [31] = "BEEP,", [32] = "DIODE,", [33] = "CAP.,", [34] = "uVA,", [35] = "mVA,",
    [36] = "VA,", [37] = "ACuA,", [38] = "DCuA,", [39] = "ACmA,", [40] = "DCmA,",
    [41] = "ACA,", [42] = "DCA,", [43] = "DCuVA,", [44] = "DCmVA,", [45] = "DCVA,",
    [46] = "Burden,",
    [51] = "ACV,", [52] = "DCV,", [53] = "V,", [54] = "mV,", [55] = "'C,", [56] = "'F,",
    [57] = "Hz,", [58] = "KHz,", [59] = "mSec,", [60] = "%,", [61] = "ohm,", [62] = "Kohm,",
    [63] = "Mohm,", [64] = "nF,", [65] = "uF,", [66] = "uVA,", [67] = "mVA,", [68] = "VA,",
    [69] = "uA,", [70] = "mA,", [71] = "A,",
    [81] = "PeakHold", [82] = "1KHz", [83] = "ACDC", [91] = "MAX,", [92] = "MIN,",
};

static const char cal_file_name[] = CAL_FILE_NAME;

static int dual_function(uint8_t f)
{
    return f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24;
}

/* ---- text output -------------------------------------------------------------------------- */

/* orig: 0x0801F604 - append text to the open log file (and to g_sd_buffer for the read-back) */
uint8_t log_write(const char *text, uint16_t len)
{
    for (uint8_t i = 0; i < len; i++) {
        g_sd_buffer[g_log_buf_len] = (uint8_t)text[i];
        g_log_buf_len++;
    }
    g_sd_result = (uint8_t)f_write(&g_log_file, text, len, &g_sd_bw);
    if (g_sd_result == FR_OK && g_sd_bw == len) {
        g_log_bytes += len;
        return 0;
    }
    return 1;
}

static uint8_t log_text_out(const char *text)
{
    return log_write(text, (uint16_t)strlen(text));
}

/* orig: 0x0801F4B8 - "HH:MM:SS," */
uint8_t log_write_time(uint8_t hh, uint8_t mm, uint8_t ss)
{
    char text[12];

    text[0] = (char)(hh / 10u + '0');
    text[1] = (char)(hh % 10u + '0');
    text[2] = ':';
    text[3] = (char)(mm / 10u + '0');
    text[4] = (char)(mm % 10u + '0');
    text[5] = ':';
    text[6] = (char)(ss / 10u + '0');
    text[7] = (char)(ss % 10u + '0');
    text[8] = ',';
    text[9] = 0;
    return log_text_out(text) != 0;
}

/* orig: 0x0801F558 - "20YY/MM/DD," */
uint8_t log_write_date(uint8_t yy, uint8_t mm, uint8_t dd)
{
    char text[12];

    text[0] = '2';
    text[1] = '0';
    text[2] = (char)(yy / 10u + '0');
    text[3] = (char)(yy % 10u + '0');
    text[4] = '/';
    text[5] = (char)(mm / 10u + '0');
    text[6] = (char)(mm % 10u + '0');
    text[7] = '/';
    text[8] = (char)(dd / 10u + '0');
    text[9] = (char)(dd % 10u + '0');
    text[10] = ',';
    text[11] = 0;
    return log_text_out(text) != 0;
}

/* orig: 0x0801F7E0 - six digits of value with a '.' at position dp (from the right; 0 = none) */
char *value_to_text(uint32_t value, uint8_t dp)
{
    char *p = &g_value_text[7];
    uint8_t pos = 0;
    uint8_t digits = 0;

    *p = 0;
    do {
        if (dp != 0 && dp == pos) {
            *--p = '.';
        } else {
            *--p = (char)('0' + value % 10u);
            value /= 10u;
            digits++;
        }
        pos++;
    } while (digits < 6);
    return p;
}

/* orig: 0x0801E82E - write one log field; 1 if the write failed */
uint8_t log_field(int16_t code, uint32_t value, uint8_t dp)
{
    char text[12];

    if (code >= 0 && code < (int16_t)(sizeof log_text / sizeof log_text[0]) && log_text[code] != 0) {
        /* codes 9, 10, 47-50, 72-80, 84-90 have no text */
        return log_text_out(log_text[code]);
    }
    switch (code) {
    case LOG_NUMBER:
        return log_text_out(number_to_text(value));
    case LOG_INTERVAL_VALUE: {
        uint32_t rest = g_log_interval_s % 100u;

        text[0] = (char)(g_log_interval_s / 100u + '0');
        text[1] = (char)(rest / 10u + '0');
        text[2] = (char)(rest % 10u + '0');
        text[3] = ',';
        text[4] = 0;
        return log_text_out(text);
    }
    case LOG_START_DATE:
        return log_write_date(g_clock_a[0], g_clock_a[1], g_clock_a[2]);
    case LOG_START_TIME:
        return log_write_time(g_clock_a[3], g_clock_a[4], g_clock_a[5]);
    case LOG_SERIAL:
        number_to_digits(g_serial_yymm);
        text[0] = (char)(g_digits[1] + '0');
        text[1] = (char)(g_digits[2] + '0');
        text[2] = (char)(g_digits[3] + '0');
        text[3] = (char)(g_digits[4] + '0');
        number_to_digits(g_serial_number);
        text[4] = (char)(g_digits[0] + '0');
        text[5] = (char)(g_digits[1] + '0');
        text[6] = (char)(g_digits[2] + '0');
        text[7] = (char)(g_digits[3] + '0');
        text[8] = (char)(g_digits[4] + '0');
        text[9] = ',';
        text[10] = 0;
        return log_text_out(text);
    case LOG_RECALL_DATE:
        return log_write_date(g_recall_clock[0], g_recall_clock[1], g_recall_clock[2]);
    case LOG_RECALL_TIME:
        return log_write_time(g_recall_clock[3], g_recall_clock[4], g_recall_clock[5]);
    case LOG_DECIMAL:
        return log_text_out(value_to_text(value, dp));
    default:
        return 0;
    }
}

/* orig: 0x0801E018 - "n," */
uint8_t log_number(uint32_t n)
{
    if (log_field(LOG_NUMBER, n, 0) != 0) {
        return 1;
    }
    return log_field(LOG_COMMA, 0, 0) != 0;
}

/* orig: 0x0801E048 - function name (unknown functions log as DCV) */
uint8_t log_function_name(uint8_t function)
{
    if (function > 25) {
        function = 2;
    }
    return log_field((int16_t)(function + LOG_FUNC_NAMES), 0, 0) != 0;
}

/* orig: 0x0801E7FA - value with the decimal point of the function and range */
uint8_t log_value(uint8_t function, uint8_t range, int32_t value)
{
    return log_field(LOG_DECIMAL, (uint32_t)value, main_dp_for_range(function, range));
}

/* orig: 0x0801E076 - unit of a function and range */
uint8_t log_unit(uint8_t function, uint8_t range)
{
    int16_t code;

    switch (function) {
    case 0:  code = (g_flag_7fe5 != 0) ? 51 : 52; break;          /* ACV / DCV (LowZ) */
    case 1:
    case 2:
    case 11: code = 53; break;                                    /* V */
    case 3:
    case 4:
    case 25: code = 54; break;                                    /* mV */
    case 5:  code = (g_flag_7fea != 0) ? 55 : 56; break;          /* 'C / 'F */
    case 6:  code = (range > 1) ? 58 : 57; break;                 /* KHz / Hz */
    case 7:  code = 59; break;                                    /* mSec */
    case 8:  code = 60; break;                                    /* % */
    case 9:  code = (range < 2) ? 61 : (range > 4) ? 63 : 62; break;   /* ohm / Kohm / Mohm */
    case 10: code = 61; break;
    case 12: code = (range > 1) ? 65 : 64; break;                 /* uF / nF */
    case 13:
    case 22: code = 66; break;                                    /* uVA */
    case 14:
    case 23: code = 67; break;                                    /* mVA */
    case 15:
    case 24: code = (range != 0) ? 68 : 67; break;                /* VA / mVA */
    case 16:
    case 17: code = 69; break;                                    /* uA */
    case 18:
    case 19: code = 70; break;                                    /* mA */
    case 20:
    case 21: code = (range != 0) ? 71 : 70; break;                /* A / mA */
    default: return 0;
    }
    return log_field(code, 0, 0) != 0;
}

/* ---- header ----------------------------------------------------------------------------------- */

static uint8_t log_fields(const int16_t *codes, unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (log_field(codes[i], 0, 0) != 0) {
            return 1;
        }
    }
    return 0;
}

/* orig: 0x0801E512 - "START,date,time," */
uint8_t log_header_start(void)
{
    static const int16_t codes[] = {LOG_START, LOG_START_DATE, LOG_START_TIME, LOG_CRLF};

    return log_fields(codes, 4);
}

/* orig: 0x0801E4D2 - "ID,serial," */
uint8_t log_header_id(void)
{
    static const int16_t codes[] = {LOG_ID, LOG_SERIAL, LOG_CRLF};

    return log_fields(codes, 3);
}

/* orig: 0x0801E7A8 - "INTERVAL,nnn,sec," */
uint8_t log_header_interval(void)
{
    static const int16_t codes[] = {LOG_INTERVAL, LOG_INTERVAL_VALUE, LOG_SEC, LOG_CRLF};

    return log_fields(codes, 4);
}

/* orig: 0x0801E3D8 - ",,,MAIN,,,SUB-1,,,SUB-2,,,Remark," */
uint8_t log_header_groups(void)
{
    static const int16_t codes[] = {LOG_GROUPS_MAIN, LOG_GROUPS_SUB1, LOG_GROUPS_SUB2, LOG_REMARK,
                                    LOG_CRLF};

    return log_fields(codes, 5);
}

/* orig: 0x0801E43C - column titles */
uint8_t log_header_columns(void)
{
    static const int16_t first[] = {LOG_COL_NO, LOG_COL_DATE, LOG_COL_TIME};
    static const int16_t group[] = {LOG_COL_FUNC, LOG_COL_VALUE, LOG_COL_UNIT};

    if (log_fields(first, 3) != 0) {
        return 1;
    }
    for (uint8_t i = 0; i < 3; i++) {
        if (log_fields(group, 3) != 0) {
            return 1;
        }
    }
    return log_field(LOG_CRLF, 0, 0) != 0;
}

/* orig: 0x0801E330 */
uint8_t log_write_header(void)
{
    uint8_t r;

    g_log_start_pos = fil_tell(&g_log_file);
    g_log_end_pos = g_log_start_pos;
    r = log_header_start();
    if (r == 0) {
        r = log_header_id();
    }
    if (r == 0) {
        r = log_header_interval();
    }
    if (r == 0) {
        r = log_header_groups();
    }
    if (r == 0) {
        r = log_header_columns();
    }
    return r;
}

/* ---- records ---------------------------------------------------------------------------------- */

/* "Overflow" / "-Overflow" / value by the record flags (bit 7 overload, bit 6 negative) */
static uint8_t log_flagged_value(uint8_t function, uint8_t range, uint8_t flags, int32_t value)
{
    if (flags & 0x80u) {
        return log_field((flags & 0x40u) ? LOG_NEG_OVERFLOW : LOG_OVERFLOW, 0, 0) != 0;
    }
    if ((flags & 0x40u) && log_field(LOG_MINUS, 0, 0) != 0) {
        return 1;
    }
    return log_value(function, range, value) != 0;
}

/* a secondary reading: "Overflow" or a signed value (the recalled value is made positive) */
static uint8_t log_sub_value(uint8_t function, uint8_t range, uint8_t flags, int32_t *value)
{
    if (log_function_name(function) != 0) {
        return 1;
    }
    if (flags & 0x80u) {
        if (log_field(LOG_OVERFLOW, 0, 0) != 0) {
            return 1;
        }
    } else {
        if (*value < 0) {
            if (log_field(LOG_MINUS, 0, 0) != 0) {
                return 1;
            }
            *value = -*value;
        }
        if (log_value(function, range, *value) != 0) {
            return 1;
        }
    }
    if (log_field(LOG_COMMA, 0, 0) != 0) {
        return 1;
    }
    return log_unit(function, range) != 0;
}

static uint8_t log_commas(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        if (log_field(LOG_COMMA, 0, 0) != 0) {
            return 1;
        }
    }
    return 0;
}

/* orig: 0x0801DBF0 - one CSV line for MEM record i (numbered g_value_7d40) */
uint8_t log_write_record(uint16_t i)
{
    uint8_t f;

    if (log_number((uint32_t)g_value_7d40) != 0) {
        return 1;
    }
    mem_recall(i);
    if (log_field(LOG_RECALL_DATE, 0, 0) != 0 || log_field(LOG_RECALL_TIME, 0, 0) != 0) {
        return 1;
    }
    if (log_function_name(g_recall_function1) != 0) {
        return 1;
    }
    if (log_flagged_value(g_recall_function1, g_recall_range1, g_recall_flags1, g_recall_value1) != 0) {
        return 1;
    }
    if (log_field(LOG_COMMA, 0, 0) != 0 || log_unit(g_recall_function1, g_recall_range1) != 0) {
        return 1;
    }

    f = g_recall_function1;
    if (f == 16 || f == 18 || f == 20 || (f == 2 && g_flag_7fe9 == 0) || f == 4 || dual_function(f)) {
        if (log_sub_value(g_recall_function2, g_recall_range2, g_recall_flags2, &g_recall_value2) != 0) {
            return 1;
        }
        if (dual_function(g_recall_function1)) {
            if (log_sub_value(g_recall_function3, g_recall_range3, g_recall_flags3, &g_recall_value3) != 0) {
                return 1;
            }
        } else if (log_commas(3) != 0) {
            return 1;
        }
    } else if (log_commas(6) != 0) {
        return 1;
    }

    if (g_flag_7fe8 != 0) {
        if (log_field(LOG_PEAKHOLD, 0, 0) != 0) {
            return 1;
        }
    } else if (g_flag_7fe7 != 0) {
        if (log_field(LOG_1KHZ, 0, 0) != 0) {
            return 1;
        }
    } else if (g_flag_7fe9 != 0 && log_field(LOG_ACDC, 0, 0) != 0) {
        return 1;
    }
    if (log_field(LOG_COMMA, 0, 0) != 0) {
        return 1;
    }
    return log_field(LOG_CRLF, 0, 0) != 0;
}

static uint8_t log_summary_line(int16_t title, int32_t number, uint8_t mode, uint8_t flags,
                                int32_t value)
{
    g_recall_function1 = mode & 0x1Fu;
    g_recall_range1 = mode >> 5;
    if (log_field(title, 0, 0) != 0 || log_number((uint32_t)number) != 0) {
        return 1;
    }
    if (log_function_name(g_recall_function1) != 0) {
        return 1;
    }
    if (log_flagged_value(g_recall_function1, g_recall_range1, flags, value) != 0) {
        return 1;
    }
    if (log_field(LOG_COMMA, 0, 0) != 0 || log_unit(g_recall_function1, g_recall_range1) != 0) {
        return 1;
    }
    return log_field(LOG_CRLF, 0, 0) != 0;
}

/* orig: 0x0801E564 - "MAX,n,..." and "MIN,n,..." lines */
uint8_t log_write_summary(void)
{
    if (log_summary_line(LOG_MAX, g_cmp_a_d54, g_cmp_a_mode, g_cmp_a_flags, g_cmp_a_value) != 0) {
        return 1;
    }
    return log_summary_line(LOG_MIN, g_cmp_b_d58, g_cmp_b_mode, g_cmp_b_flags, g_cmp_b_value);
}

/* signed value of a record: bit 6 of the flags marks a one's-complement magnitude */
static int32_t record_value(uint8_t flags, int32_t magnitude)
{
    return (flags & 0x40u) ? ~magnitude : magnitude;
}

/* orig: 0x0801DA40 - recalled reading above the maximum (not overloaded) becomes the maximum */
void log_track_max(void)
{
    if ((g_recall_flags1 & 0x80u) == 0
        && record_value(g_cmp_a_flags, g_cmp_a_value) < record_value(g_recall_flags1, g_recall_value1)) {
        mem_recall_to_a();
    }
}

/* orig: 0x0801DAB2 - ... below the minimum (a negative overload counts) */
void log_track_min(void)
{
    if (((g_recall_flags1 & 0x80u) == 0 || (g_recall_flags1 & 0x40u) != 0)
        && record_value(g_recall_flags1, g_recall_value1) < record_value(g_cmp_b_flags, g_cmp_b_value)) {
        mem_recall_to_b();
    }
}

/*
 * orig: 0x0801D9AC - the first record sets both; afterwards a maximum that
 * is a positive overload (a minimum that is a negative overload) stays
 */
void log_track_minmax(void)
{
    if (g_value_7d40 == 1) {
        mem_recall_to_a();
    } else if (!((g_cmp_a_flags & 0x80u) && !(g_cmp_a_flags & 0x40u))) {
        log_track_max();
    }
    if (g_value_7d40 == 1) {
        mem_recall_to_b();
    } else if (!((g_cmp_b_flags & 0x80u) && (g_cmp_b_flags & 0x40u))) {
        log_track_min();
    }
}

/* orig: 0x0801D8FC - write the header (new file) or the pending records */
uint8_t log_write_pending(void)
{
    uint8_t r;

    g_log_buf_len = 0;
    if (g_log_header_due != 0) {
        return log_write_header();
    }
    g_log_record_saved = g_value_7d40;
    for (uint16_t i = 0; i < (uint32_t)g_value_7d44; i++) {
        g_value_7d40++;
        r = log_write_record(i);
        if (r != 0) {
            g_log_resync = 1;
            g_value_7d40 = g_log_record_saved;
            return r;
        }
        log_track_minmax();
    }
    g_log_record_saved = 0;
    if (g_value_7d44 == 60) {
        g_log_blocks++;
    }
    g_value_7d44 = 0;
    return 0;
}

/*
 * orig: 0x0801F6B4 - read back what was written since g_log_start_pos and
 * compare it with g_sd_buffer; 0 or 8. As in the original, a mismatch in one
 * block does not stop the loop: it goes on reading without advancing the
 * buffer position and ends with 0 unless a read fails.
 */
uint8_t log_verify(void)
{
    uint8_t r = 8;
    uint16_t pos = 0;
    uint16_t remaining;
    uint16_t n;

    g_sd_result = (uint8_t)f_open(&g_log_file, g_log_name, FA_READ);
    if (g_sd_result == FR_OK) {
        g_sd_result = (uint8_t)f_lseek(&g_log_file, g_log_start_pos);
    }
    if (g_sd_result == FR_OK) {
        remaining = g_log_buf_len;
        for (;;) {
            n = (remaining > 0x200) ? 0x200 : remaining;
            remaining = (uint16_t)(remaining - n);
            g_sd_result = (uint8_t)f_read(&g_log_file, g_sd_read_buf, n, &g_sd_br);
            if (g_sd_result != FR_OK || g_sd_br != n) {
                r = 8;
                break;
            }
            r = 0;
            for (uint16_t i = 0; i < n; i++) {
                if (g_sd_buffer[(uint16_t)(i + pos)] != g_sd_read_buf[i]) {
                    r = 8;
                    break;
                }
            }
            if (r != 0) {
                continue;
            }
            if (remaining == 0) {
                break;
            }
            pos = (uint16_t)(n + pos);
        }
    }
    f_close(&g_log_file);
    return r;
}

/* ---- files ------------------------------------------------------------------------------------ */

/* orig: 0x0801D838 - "YYMMDDNN.csv" from the clock */
void log_name_from_date(int32_t index)
{
    g_log_filename[0] = (char)(g_rtc_set_reg2 / 10u + '0');
    g_log_filename[1] = (char)(g_rtc_set_reg2 % 10u + '0');
    g_log_filename[2] = (char)(g_rtc_set_reg4 / 10u + '0');
    g_log_filename[3] = (char)(g_rtc_set_reg4 % 10u + '0');
    g_log_filename[4] = (char)(g_rtc_set_reg6 / 10u + '0');
    g_log_filename[5] = (char)(g_rtc_set_reg6 % 10u + '0');
    g_log_filename[6] = (char)(index / 10 + '0');
    g_log_filename[7] = (char)(index - (index / 10) * 10 + '0');
    g_log_filename[8] = '.';
    g_log_filename[9] = 'c';
    g_log_filename[10] = 's';
    g_log_filename[11] = 'v';
    g_log_name = g_log_filename;
}

/* orig: 0x0801D79C - NN from g_count_7fae (for 1 the name first moves two characters left) */
void log_name_from_count(void)
{
    if (g_count_7fae == 1) {
        g_log_filename[0] = g_log_filename[2];
        g_log_filename[1] = g_log_filename[3];
        g_log_filename[2] = g_log_filename[4];
        g_log_filename[3] = g_log_filename[5];
        g_log_filename[4] = g_log_filename[6];
        g_log_filename[5] = g_log_filename[7];
    }
    g_log_filename[6] = (char)(g_count_7fae / 10u + '0');
    g_log_filename[7] = (char)(g_count_7fae % 10u + '0');
    g_log_name = g_log_filename;
}

/* orig: 0x0801D640 - highest NN of today's "YYMMDDNN.csv" files (0 if over 99) */
uint8_t log_find_index(const char *path)
{
    int32_t highest;

    g_sd_result = (uint8_t)f_opendir(&g_log_dir, path);
    if (g_sd_result != FR_OK || g_count_7fae != 0) {
        return g_sd_result;
    }
    highest = 0;
    for (;;) {
        const char *n = g_log_info.fname;
        int32_t index;

        g_sd_result = 0;
        g_sd_result = (uint8_t)f_readdir(&g_log_dir, &g_log_info);
        if (g_sd_result != FR_OK || n[0] == 0) {
            break;
        }
        if (n[0] == '.') {
            continue;
        }
        if (!((n[9] == 'c' && n[10] == 's' && n[11] == 'v') || (n[9] == 'C' && n[10] == 'S' && n[11] == 'V'))) {
            continue;
        }
        g_log_file_yy = (uint8_t)(n[0] * 10 + n[1] - 16);
        g_log_file_mm = (uint8_t)(n[2] * 10 + n[3] - 16);
        g_log_file_dd = (uint8_t)(n[4] * 10 + n[5] - 16);
        index = (int32_t)((uint8_t)n[6] * 10u + (uint8_t)n[7]) - 0x20F;
        if (g_log_file_yy == g_rtc_set_reg2 && g_log_file_mm == g_rtc_set_reg4
            && g_log_file_dd == g_rtc_set_reg6 && highest < index) {
            highest = index;
        }
    }
    g_log_index = highest;
    if (highest > 99) {
        g_log_index = 0;
    }
    return g_sd_result;
}

/* orig: 0x0801D53A - create the log file (second != 0: through g_log_file2, then reopen g_log_name2) */
uint8_t log_open(uint8_t second, uint8_t tries)
{
    for (uint8_t i = 0; i < tries; i++) {
        if (second == 0) {
            g_sd_result = (uint8_t)f_open(&g_log_file, g_log_name, FA_WRITE | FA_CREATE_ALWAYS);
        } else {
            g_sd_result = (uint8_t)f_open(&g_log_file2, g_log_name, FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
            if (g_sd_result == FR_OK) {
                g_sd_result = (uint8_t)f_open(&g_log_file, g_log_name2, FA_READ);
            }
        }
        if (g_sd_result == FR_OK) {
            return 0;
        }
    }
    return g_sd_result;
}

/* orig: 0x0801D5B0 - name and create a new log file */
uint8_t log_create(uint8_t tries)
{
    g_sd_drive = '0';
    g_log_resync = ':';
    g_sd_transfer_error = 0;
    for (uint8_t i = 0; i < tries; i++) {
        g_sd_result = log_find_index(&g_sd_drive);
        if (g_sd_result == 0) {
            if (g_count_7fae == 0) {
                log_name_from_date(g_log_index);
            } else {
                log_name_from_count();
            }
            g_sd_result = log_open(0, 10);
            if (g_sd_result == 0) {
                break;
            }
        }
    }
    return g_sd_result;
}

/* orig: 0x0801D48C - reopen the log file and seek to g_log_end_pos; 0, 4 (open) or 5 (seek) */
uint8_t log_reopen(void)
{
    uint8_t r = 0;
    uint8_t i = 0;

    do {
        g_sd_result = (uint8_t)f_open(&g_log_file, g_log_name, FA_WRITE);
        if (g_sd_result == FR_OK) {
            break;
        }
        i++;
    } while (i < 21);
    if (g_sd_result != FR_OK) {
        r = 4;
    }
    if (g_sd_result == FR_OK) {
        i = 0;
        g_sd_result = 0;
        do {
            g_sd_result = (uint8_t)f_lseek(&g_log_file, g_log_end_pos);
            if (g_sd_result == FR_OK) {
                g_log_resync = 0;
                g_log_start_pos = fil_tell(&g_log_file);
                return r;
            }
            i++;
        } while (i < 21);
        r = 5;
    }
    return r;
}

/* orig: 0x0801D37C - close and stamp the log file (or cal.bin) with the clock; 1 when it reads back */
uint8_t log_set_timestamp(uint8_t cal_file)
{
    f_close(&g_log_file);
    for (uint8_t i = 0; i < 10; i++) {
        uint16_t fdate = (uint16_t)(((g_rtc_set_reg2 + 20u) << 9) | ((uint32_t)g_rtc_set_reg4 << 5)
                                    | g_rtc_set_reg6);
        uint16_t ftime = (uint16_t)(((uint32_t)g_rtc_set_reg10 << 11) | ((uint32_t)g_rtc_set_reg12 << 5)
                                    | (g_rtc_set_reg14 >> 1));

        g_log_info.fdate = fdate;
        g_log_info.ftime = ftime;
        g_sd_result = (uint8_t)f_utime((cal_file == 0) ? g_log_name : cal_file_name, &g_log_info);
        if (g_sd_result == FR_OK) {
            g_sd_result = (uint8_t)f_stat((cal_file == 0) ? g_log_name : cal_file_name, &g_log_info);
            if (g_sd_result == FR_OK && fdate == g_log_info.fdate && ftime == g_log_info.ftime) {
                return 1;
            }
        }
    }
    return 0;
}

/*
 * orig: 0x0801D1A8 - write the pending records (or a new file's header),
 * check them, stamp the file. Returns 0, or 1 (no card), 3 (create), 4/5
 * (reopen), 7 (write), 8 (read-back).
 */
uint8_t log_flush(void)
{
    uint8_t created = 0;
    uint8_t r = 0;

    clock_init(1);
    if (sd_open_retry(10) != 0) {
        return 1;
    }
    f_mount(0, &g_fatfs);
    if (g_flag_8024 == 0) {
        r = log_reopen();
    } else {
        g_log_bytes = 0;
        g_sd_result = log_create(10);
        if (g_sd_result == 0) {
            g_flag_8024 = 0;
            created = 1;
            if (g_count_7fae == 0) {
                g_log_header_due = 1;
            }
        } else {
            r = 3;
        }
    }
    if (r == 0 && log_write_pending() != 0) {
        r = 7;
    }
    if (created) {
        (void)log_set_timestamp(0);
    } else {
        f_close(&g_log_file);
    }
    if (r == 0) {
        g_log_end_pos = fil_tell(&g_log_file);
        r = log_verify();
        if (r != 0) {
            g_log_end_pos = g_log_start_pos;
            g_log_resync = 1;
            if ((uint32_t)g_value_7d40 >= 60u) {
                g_value_7d40 -= 60;
                g_log_blocks--;
            }
        }
        if (r == 0 && g_log_header_due != 0) {
            g_log_header_due = 0;
        }
    }
    if (r == 0 && (uint32_t)g_value_7d40 >= LOG_RECORD_MAX) {
        log_stop(0);
        g_logging_active = 0;
        sym_w0_b16_update(1);
        g_flag_801d = 1;
        g_disp_flag_8062 = 1;
    } else if (g_flag_803b != 0) {
        log_stop(0);
        g_logging_active = 0;
        sym_w0_b16_update(1);
        g_flag_801b = 100;
        g_disp_flag_8062 = 1;
    }
    if (r == 0 && g_log_summary_due != 0) {
        g_log_summary_due = 0;
        for (uint8_t i = 0; i < 20; i++) {
            r = log_reopen();
            if (r == 0 && log_write_summary() == 0) {
                break;
            }
        }
        r = 0;
        (void)log_set_timestamp(0);
    }
    sd_power_off();
    clock_init(0);
    return r;
}

/*
 * orig: 0x0801D126 - log_flush() up to 20 times; if all fail, start a new
 * file (mem_flags_801c_set) and try 20 more times; 1 if that fails too
 */
uint8_t log_flush_retry(void)
{
    uint8_t r;

    g_log_retries = 20;
    do {
        if (log_flush() == 0) {
            return 0;
        }
        g_log_retries--;
    } while (g_log_retries != 0);

    mem_flags_801c_set();
    g_log_retries = 20;
    do {
        r = log_flush();
        if (r == 0) {
            return 0;
        }
        g_log_retries--;
    } while (g_log_retries != 0);
    g_disp_flag_8062 = 1;
    g_flag_801b = r;
    return 1;
}

/* orig: 0x0801F87E - power the card up and mount it (up to 0x301 attempts); 0, 1 (failed) or 2 (no card) */
uint8_t sd_open(void)
{
    int32_t tries = 0x300;
    int32_t left;
    int more;

    do {
        sd_low_level_deinit();
        left = tries;
        if (sd_mount() == 0) {
            break;
        }
        left = tries - 1;
        more = tries != 0;
        tries = left;
    } while (more);
    if (left == 0) {
        return 1;
    }
    if (sd_detect() == 0) {
        return 2;
    }
    return 0;
}

/* orig: 0x0801F84C */
uint8_t sd_open_retry(uint8_t tries)
{
    uint8_t r = 0;

    for (uint8_t i = 0; i < tries; i++) {
        r = sd_open();
        if (r == 0) {
            break;
        }
    }
    return r;
}

/* ---- calibration backup ------------------------------------------------------------------------- */

/* orig: 0x0801D0D6 - create cal.bin; 0 when open, 1 after `tries` failures */
uint8_t cal_file_create(uint8_t tries)
{
    g_sd_drive = '0';
    g_log_resync = ':';
    g_sd_transfer_error = 0;
    for (uint8_t i = 0; i < tries; i++) {
        if (f_open(&g_log_file, cal_file_name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
            return 0;
        }
    }
    return 1;
}

/* compare the next `n` bytes of cal.bin with g_sd_buffer[from..] */
static uint8_t cal_file_check(uint32_t from, UINT n)
{
    g_sd_result = (uint8_t)f_read(&g_log_file, g_sd_read_buf, n, &g_sd_br);
    if (g_sd_result != FR_OK || g_sd_br != n) {
        return 8;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (g_sd_buffer[from + i] != g_sd_read_buf[i]) {
            return 9;
        }
    }
    return 0;
}

/*
 * orig: 0x0801CD14 - "SdSAv": EEPROM bytes 0..0x4AF to cal.bin, read back
 * and compared; 0, 1 (no card), 4 (create), 7 (write), 8 (read), 9 (compare)
 */
uint8_t cal_backup_save(void)
{
    uint8_t r;

    for (uint32_t i = 0; i < SD_BUFFER_SIZE; i++) {
        g_sd_buffer[i] = settings_read_byte(i);
    }
    clock_init(1);
    r = (sd_open_retry(10) != 0) ? 1 : 0;
    if (r == 0) {
        f_mount(0, &g_fatfs);
    }
    if (r == 0 && cal_file_create(10) != 0) {
        r = 4;
    }
    if (r == 0) {
        g_sd_result = (uint8_t)f_write(&g_log_file, g_sd_buffer, SD_BUFFER_SIZE, &g_sd_bw);
        if (g_sd_result != FR_OK || g_sd_bw != SD_BUFFER_SIZE) {
            r = 7;
        }
    }
    f_close(&g_log_file);
    f_open(&g_log_file, cal_file_name, FA_READ);
    if (r == 0) {
        r = cal_file_check(0, 0x200);
    } else {
        g_sd_result = (uint8_t)f_read(&g_log_file, g_sd_read_buf, 0x200, &g_sd_br);
        if (g_sd_result != FR_OK || g_sd_br != 0x200) {
            r = 8;
        }
    }
    if (r == 0) {
        r = cal_file_check(0x200, 0x200);
    }
    if (r == 0) {
        r = cal_file_check(0x400, 0xB0);
    }
    (void)log_set_timestamp(1);
    sd_power_off();
    clock_init(0);
    return r;
}

/* read the next `n` bytes of cal.bin into g_sd_buffer[to..] */
static uint8_t cal_file_load(uint32_t to, UINT n)
{
    g_sd_result = (uint8_t)f_read(&g_log_file, g_sd_read_buf, n, &g_sd_br);
    if (g_sd_result != FR_OK || g_sd_br != n) {
        return 8;
    }
    for (uint32_t i = 0; i < n; i++) {
        g_sd_buffer[to + i] = g_sd_read_buf[i];
    }
    return 0;
}

/*
 * orig: 0x0801CF06 - "SdSEt": cal.bin to EEPROM bytes 0..0x4AF, read back
 * and compared; 0, 1 (no card), 4 (no file), 8 (read), 9 (compare)
 */
uint8_t cal_backup_restore(void)
{
    uint8_t check[SD_BUFFER_SIZE];
    uint8_t r = 0;

    clock_init(1);
    if (sd_open_retry(10) != 0) {
        r = 1;
    }
    if (r == 0) {
        f_mount(0, &g_fatfs);
    }
    if (f_open(&g_log_file, cal_file_name, FA_READ) != FR_OK) {
        r = 4;
    }
    if (r == 0) {
        r = cal_file_load(0, 0x200);
    }
    if (r == 0) {
        r = cal_file_load(0x200, 0x200);
    }
    if (r == 0) {
        r = cal_file_load(0x400, 0xB0);
    }
    f_close(&g_log_file);
    sd_power_off();
    clock_init(0);
    if (r == 0) {
        for (uint32_t i = 0; i < SD_BUFFER_SIZE; i++) {
            settings_write_byte(i, g_sd_buffer[i]);
        }
        for (uint32_t i = 0; i < SD_BUFFER_SIZE; i++) {
            check[i] = settings_read_byte(i);
        }
        for (uint32_t i = 0; i < SD_BUFFER_SIZE; i++) {
            if (check[i] != g_sd_buffer[i]) {
                return 9;
            }
        }
    }
    return r;
}

/* ---- logging control ---------------------------------------------------------------------------- */

/* orig: 0x08010908 - main reading for the MEM record: magnitude (one's complement) with sign/overload flags */
void log_capture_value1(void)
{
    uint8_t flags = g_cur_flags1;
    uint8_t f = g_function;

    if (g_flag_8049 != 0) {
        g_cur_value1 = INT32_MAX;
        flags = g_cur_flags1 | 0x80u;
        if (g_peak_is_min != 0) {
            flags = g_cur_flags1 | 0xC0u;
        }
    } else if ((f < 6 || f > 8) && f != 12 && f != 5 && f != 9) {
        if (g_scaled_value < 0) {
            g_cur_value1 = ~g_scaled_value;
            flags = g_cur_flags1 | 0x40u;
        } else {
            g_cur_value1 = g_scaled_value;
        }
    } else if (g_derived_value < 0) {
        g_cur_value1 = ~g_derived_value;
        flags = g_cur_flags1 | 0x40u;
    } else {
        g_cur_value1 = g_derived_value;
    }
    g_cur_flags1 = flags;
}

/* orig: 0x080109EA - start logging (not with a low battery): manual range, HOLD/REL/MIN-MAX off */
void log_start(void)
{
    if (g_flag_803b != 0) {
        g_flag_801b = 20;
        g_disp_flag_8062 = 1;
        return;
    }
    g_mode_8037 = 100;
    if (g_autohold_mode == 0) {
        g_flag_8043 = g_manual_range;
        g_manual_range = 1;
        sym_w7_b0_update(1);
    }
    g_rel_active = 0;
    g_minmax_mode = 0;
    g_hold_active = 0;
    g_flag_7fe3 = 1;
    g_log_due = 0;
    g_menu_active = 0;
    g_value_7f9c = 0;
    g_mem_count = 0;
    g_log_count = 0;
    g_log_blocks = 0;
    g_flag_8024 = 1;
    g_flag_801c = 1;
    g_flag_801d = 0;
    g_log_record_saved = 0;
    g_value_7d40 = 0;
    g_log_header_due = 0;
    g_log_summary_due = 0;
    g_count_7fae = 0;
    g_log_tick_count = (uint16_t)(g_log_interval_s - 1u);
    g_flag_7ffa = 0;
    mem_save_function();
    mem_clock_snapshot_a();
}

/* orig: 0x08010AF8 - stop logging; flush != 1 leaves the last records for the next flush */
void log_stop(uint8_t flush)
{
    secondary_mode_default();
    mem_mark_counts();
    mem_clock_snapshot_b();
    if (g_mem_count != 0 && g_mem_count < 60) {
        g_value_7d44 = g_mem_count;
    }
    g_log_summary_due = 1;
    g_flag_8024 = 0;
    if (flush == 1) {
        (void)log_flush_retry();
    }
    mem_recall_init();
    if (g_autohold_mode == 0) {
        g_manual_range = g_flag_8043;
        sym_w7_b0_update(1);
    }
}

/* orig: 0x0801657C - logging on/off (with force or in auto-hold mode, that mode follows) */
void log_toggle(uint8_t force)
{
    g_logging_active = (g_logging_active != 1);
    if (force != 0 || g_autohold_mode != 0) {
        autohold_mode_set(g_logging_active, 1);
    }
    if (g_logging_active == 0) {
        log_stop(1);
    } else {
        log_start();
    }
    sym_w5_b7_update(0);
    sym_w0_b16_update(1);
    beep_long();
}

/*
 * orig: 0x08010516 - the readings a MEM record stores: mode (function |
 * range << 5) and flags of the main reading, the frequency / VA auxiliary
 * value or the first dual reading, and the second dual reading
 */
void log_capture_values(void)
{
    uint8_t f = g_function;

    g_cur_mode1 = (uint8_t)(g_function + g_range * 32u);
    g_cur_flags1 = (g_flag_8098 != 0) ? 1u : 0u;
    if (g_flag_8099 != 0) {
        g_cur_flags1 |= 0x02u;
    }
    if (g_flag_80b9 != 0) {
        g_cur_flags1 |= 0x04u;
    }
    if (g_flag_80b7 != 0) {
        g_cur_flags1 |= 0x08u;
    }
    if (g_flag_80ba != 0) {
        g_cur_flags1 |= 0x10u;
    }
    if (g_temp_unit_c != 0) {
        g_cur_flags1 |= 0x20u;
    }
    log_capture_value1();

    g_cur_flags2 = 0;
    g_cur_mode2 = 0;
    g_cur_value2 = 0;
    f = g_function;
    if (f == 16 || f == 18 || f == 20 || (f == 2 && g_flag_80ba == 0) || f == 4) {
        if ((f == 16 || f == 18 || f == 20) && g_flag_80a5 != 0) {
            g_cur_mode2 = 25;
            if (g_flag_80a7 != 0) {
                g_cur_flags2 = 0x80;
            }
            g_cur_value2 = g_va_aux;
        } else {
            g_cur_mode2 = (uint8_t)(g_freq_decimal * 32u + 6u);
            if (g_flag_8047 != 0) {
                g_cur_flags2 = 0x80;
            }
            g_cur_value2 = g_freq_raw;
        }
    } else if (dual_function(f)) {
        g_cur_mode2 = (f == 13 || f == 14 || f == 15) ? 2 : 1;
        if (g_range == 1 || g_range == 3) {
            g_cur_mode2 = (uint8_t)(g_cur_mode2 + 32u);
        }
        if (g_disp_flag_80ca != 0 || g_disp_flag_80cb != 0) {
            g_cur_flags2 = 0x80;
        }
        g_cur_value2 = g_disp_eac;
    }

    g_cur_flags3 = 0;
    g_cur_mode3 = 0;
    f = g_function;
    if (!dual_function(f)) {
        g_cur_value3 = 0;
        g_cur_mode3 = 0;
        g_cur_flags3 = 0;
        return;
    }
    switch (f) {
    case 13: g_cur_mode3 = 16; break;
    case 14: g_cur_mode3 = 18; break;
    case 15: g_cur_mode3 = 20; break;
    case 22: g_cur_mode3 = 17; break;
    case 23: g_cur_mode3 = 19; break;
    default: g_cur_mode3 = 21; break;     /* 24 */
    }
    if (g_range > 1) {
        g_cur_mode3 = (uint8_t)(g_cur_mode3 + ((f == 15 || f == 24) ? 64u : 32u));
    }
    if (g_disp_flag_80cb != 0 || g_disp_flag_80ca != 0) {
        g_cur_flags3 = 0x80;
    }
    g_cur_value3 = (f == 13 || f == 22) ? g_disp_eb4 : g_disp_eb0;
}
