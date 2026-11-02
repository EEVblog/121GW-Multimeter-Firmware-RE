/*
 * MEM: stored readings (60 records in RAM) and the clock snapshots taken
 * with them.
 *
 * A record holds the clock (the six RTC set registers) and three readings,
 * each as a mode byte, a flags byte and a 32-bit value. The records are
 * kept as parallel arrays, as in the original.
 */
#include "mem_records.h"
#include "display_update.h"
#include "hy3131_ctrl.h"
#include "rtc.h"

#define MEM_PAGE  60u   /* records shown per recall page */

/* orig: 0x08010BCA - remember the record count and the log count */
void mem_mark_counts(void)
{
    g_mem_count_mark = g_mem_count;
    g_log_count_mark = g_log_count;
}

/* orig: 0x08010C34 - set up recall of the last (at most 60) records */
void mem_recall_init(void)
{
    if (g_log_count <= MEM_PAGE) {
        g_mem_first = 1;
        g_mem_span = g_mem_count;
    } else {
        g_mem_first = (uint16_t)(g_mem_count + 1);
        g_mem_span = MEM_PAGE;
    }
    g_mem_last = g_mem_count;
    g_value_7f9c = 1;
}

/* orig: 0x08010C8C - record index of recall position n (1-based), wrapping at g_mem_span */
uint16_t mem_recall_index(uint16_t n)
{
    int32_t index = (int32_t)g_mem_first + n - 1;

    if ((int32_t)g_mem_span < index) {
        return (uint16_t)(index - g_mem_span);
    }
    return (uint16_t)index;
}

/* orig: 0x08010CCC */
void mem_save_function(void)
{
    g_mem_saved_function = g_function;
    g_mem_saved_range = g_range;
}

/* orig: 0x08010CE2 */
void mem_restore_function(void)
{
    g_function_7feb = g_mem_saved_function;
    g_range_7fec = g_mem_saved_range;
}

static void clock_copy(uint8_t dst[6])
{
    dst[0] = g_rtc_set_reg2;
    dst[1] = g_rtc_set_reg4;
    dst[2] = g_rtc_set_reg6;
    dst[3] = g_rtc_set_reg10;
    dst[4] = g_rtc_set_reg12;
    dst[5] = g_rtc_set_reg14;
}

/* orig: 0x08010CFC - clock snapshot A, with a backup copy */
void mem_clock_snapshot_a(void)
{
    clock_copy(g_clock_a);
    clock_copy(g_clock_a_backup);
}

/* orig: 0x08010D92 */
void mem_clock_restore_a(void)
{
    for (unsigned i = 0; i < 6; i++) {
        g_clock_a[i] = g_clock_a_backup[i];
    }
}

/* orig: 0x08010DE0 - clock snapshot B, with a backup copy */
void mem_clock_snapshot_b(void)
{
    clock_copy(g_clock_b);
    clock_copy(g_clock_b_backup);
}

/* orig: 0x08010E98 */
void mem_clock_restore_b(void)
{
    for (unsigned i = 0; i < 6; i++) {
        g_clock_b[i] = g_clock_b_backup[i];
    }
}

/* orig: 0x0801217C - store the current clock and readings as record i */
void mem_store_record(uint16_t i)
{
    g_mem_rtc_reg2[i] = g_rtc_set_reg2;
    g_mem_rtc_reg4[i] = g_rtc_set_reg4;
    g_mem_rtc_reg6[i] = g_rtc_set_reg6;
    g_mem_rtc_reg10[i] = g_rtc_set_reg10;
    g_mem_rtc_reg12[i] = g_rtc_set_reg12;
    g_mem_rtc_reg14[i] = g_rtc_set_reg14;
    g_mem_mode1[i] = g_cur_mode1;
    g_mem_flags1[i] = g_cur_flags1;
    g_mem_value1[i] = g_cur_value1;
    g_mem_mode2[i] = g_cur_mode2;
    g_mem_flags2[i] = g_cur_flags2;
    g_mem_value2[i] = g_cur_value2;
    g_mem_mode3[i] = g_cur_mode3;
    g_mem_flags3[i] = g_cur_flags3;
    g_mem_value3[i] = g_cur_value3;
}

/* orig: 0x08012260 - load record i for display (modes split into low 5 bits and the rest) */
void mem_recall_record(uint16_t i)
{
    uint8_t flags;

    g_recall_clock[0] = g_mem_rtc_reg2[i];
    g_recall_clock[1] = g_mem_rtc_reg4[i];
    g_recall_clock[2] = g_mem_rtc_reg6[i];
    g_recall_clock[3] = g_mem_rtc_reg10[i];
    g_recall_clock[4] = g_mem_rtc_reg12[i];
    g_recall_clock[5] = g_mem_rtc_reg14[i];

    g_recall_mode1 = g_mem_mode1[i];
    g_recall_function1 = g_recall_mode1 & 0x1Fu;
    g_recall_range1 = g_recall_mode1 >> 5;
    g_recall_flags1 = g_mem_flags1[i];
    flags = g_recall_flags1;
    g_flag_7fe5 = (flags & 0x01u) != 0;
    g_flag_7fe6 = (flags & 0x02u) != 0;
    g_flag_7fe7 = (flags & 0x04u) != 0;
    g_flag_7fe8 = (flags & 0x08u) != 0;
    g_flag_7fe9 = (flags & 0x10u) != 0;
    g_flag_7fea = (flags & 0x20u) != 0;
    g_recall_value1 = g_mem_value1[i];

    g_recall_mode2 = g_mem_mode2[i];
    g_recall_function2 = g_recall_mode2 & 0x1Fu;
    g_recall_range2 = g_recall_mode2 >> 5;
    g_recall_flags2 = g_mem_flags2[i];
    g_recall_value2 = g_mem_value2[i];

    g_recall_mode3 = g_mem_mode3[i];
    g_recall_function3 = g_recall_mode3 & 0x1Fu;
    g_recall_range3 = g_recall_mode3 >> 5;
    g_recall_flags3 = g_mem_flags3[i];
    g_recall_value3 = g_mem_value3[i];
}

/* orig: 0x08010EF4 */
void mem_store(uint16_t i)
{
    mem_store_record(i);
}

/* orig: 0x08010F02 */
void mem_recall(uint16_t i)
{
    mem_recall_record(i);
}

/* orig: 0x0801D51A */
void mem_flags_801c_set(void)
{
    g_flag_801c = 1;
    g_flag_8024 = 1;
    g_count_7fae++;
}

/* orig: 0x0801DB34 - keep the recalled reading (with 0x20007D40) as comparison A */
void mem_recall_to_a(void)
{
    g_cmp_a_d54 = g_value_7d40;
    g_cmp_a_mode = g_recall_mode1;
    g_cmp_a_flags = g_recall_flags1;
    g_cmp_a_value = g_recall_value1;
}

/* orig: 0x0801DB64 - ... as comparison B */
void mem_recall_to_b(void)
{
    g_cmp_b_d58 = g_value_7d40;
    g_cmp_b_mode = g_recall_mode1;
    g_cmp_b_flags = g_recall_flags1;
    g_cmp_b_value = g_recall_value1;
}
