/*
 * MEM: stored readings (60 records in RAM) and clock snapshots.
 */
#ifndef MEM_RECORDS_H
#define MEM_RECORDS_H

#include <stdint.h>
#include "app_globals.h"

#define MEM_RECORDS 60u

/* Records (parallel arrays) */
extern uint8_t g_mem_rtc_reg2[MEM_RECORDS];    /* 0x20007810 */
extern uint8_t g_mem_rtc_reg4[MEM_RECORDS];    /* 0x2000784C */
extern uint8_t g_mem_rtc_reg6[MEM_RECORDS];    /* 0x20007888 */
extern uint8_t g_mem_rtc_reg10[MEM_RECORDS];   /* 0x200078C4 */
extern uint8_t g_mem_rtc_reg12[MEM_RECORDS];   /* 0x20007900 */
extern uint8_t g_mem_rtc_reg14[MEM_RECORDS];   /* 0x2000793C */
extern uint8_t g_mem_mode1[MEM_RECORDS];       /* 0x20007978  function | range << 5 */
extern uint8_t g_mem_flags1[MEM_RECORDS];      /* 0x200079B4 */
extern int32_t g_mem_value1[MEM_RECORDS];      /* 0x200073C8 */
extern uint8_t g_mem_mode2[MEM_RECORDS];       /* 0x200079F0 */
extern uint8_t g_mem_flags2[MEM_RECORDS];      /* 0x20007A2C */
extern int32_t g_mem_value2[MEM_RECORDS];      /* 0x200074B8 */
extern uint8_t g_mem_mode3[MEM_RECORDS];       /* 0x20007A68 */
extern uint8_t g_mem_flags3[MEM_RECORDS];      /* 0x20007AA4 */
extern int32_t g_mem_value3[MEM_RECORDS];      /* 0x200075A8 */

/* Readings to be stored */
extern uint8_t g_cur_mode1, g_cur_flags1;      /* 0x20007FEE, 0x20007FEF */
extern int32_t g_cur_value1;                   /* 0x20007D28 */
extern uint8_t g_cur_mode2, g_cur_flags2;      /* 0x20007FF0, 0x20007FF1 */
extern int32_t g_cur_value2;                   /* 0x20007D2C */
extern uint8_t g_cur_mode3, g_cur_flags3;      /* 0x20007FF2, 0x20007FF3 */
extern int32_t g_cur_value3;                   /* 0x20007D30 */

/* Recalled record */
extern uint8_t g_recall_clock[6];              /* 0x20008007 */
extern uint8_t g_recall_mode1, g_recall_flags1;  /* 0x20007FF4, 0x20007FF5 */
extern uint8_t g_recall_function1, g_recall_range1;  /* 0x2000801E, 0x2000801F */
extern int32_t g_recall_value1;                /* 0x20007D1C */
extern uint8_t g_recall_mode2, g_recall_flags2;  /* 0x20007FF6, 0x20007FF7 */
extern uint8_t g_recall_function2, g_recall_range2;  /* 0x20008020, 0x20008021 */
extern int32_t g_recall_value2;                /* 0x20007D20 */
extern uint8_t g_recall_mode3, g_recall_flags3;  /* 0x20007FF8, 0x20007FF9 */
extern uint8_t g_recall_function3, g_recall_range3;  /* 0x20008022, 0x20008023 */
extern int32_t g_recall_value3;                /* 0x20007D24 */
extern uint8_t g_flag_7fe5, g_flag_7fe6, g_flag_7fea;  /* 0x20007FE5, 0x20007FE6, 0x20007FEA */
extern uint8_t g_range_7fec;                   /* 0x20007FEC  copy of g_range */

/* Record bookkeeping */
extern uint16_t g_mem_count;                   /* 0x20007FA4  records stored */
extern uint16_t g_mem_count_mark;              /* 0x20007FAC */
extern uint32_t g_log_count_mark;              /* 0x20007D3C */
extern uint16_t g_mem_span;                    /* 0x20007F9E */
extern uint16_t g_mem_first;                   /* 0x20007FA0 */
extern uint16_t g_mem_last;                    /* 0x20007FA2 */
extern uint8_t g_mem_saved_function;           /* 0x2000800D */
extern uint8_t g_mem_saved_range;              /* 0x2000800E */

/* Clock snapshots */
extern uint8_t g_clock_a[6];                   /* 0x20007FFB */
extern uint8_t g_clock_b[6];                   /* 0x20008001 */
extern uint8_t g_clock_a_backup[6];            /* 0x2000800F */
extern uint8_t g_clock_b_backup[6];            /* 0x20008015 */

void mem_mark_counts(void);
void mem_recall_init(void);
uint16_t mem_recall_index(uint16_t n);
void mem_save_function(void);
void mem_restore_function(void);
void mem_clock_snapshot_a(void);
void mem_clock_restore_a(void);
void mem_clock_snapshot_b(void);
void mem_clock_restore_b(void);
void mem_store_record(uint16_t i);
void mem_recall_record(uint16_t i);
void mem_store(uint16_t i);
void mem_recall(uint16_t i);

extern uint8_t g_flag_801c;          /* 0x2000801C */
extern uint8_t g_flag_8024;          /* 0x20008024 */
extern uint16_t g_count_7fae;        /* 0x20007FAE */
extern int32_t g_value_7d40;         /* 0x20007D40 */
extern int32_t g_cmp_a_d54, g_cmp_b_d58;     /* 0x20007D54, 0x20007D58 */
extern uint8_t g_cmp_a_mode, g_cmp_b_mode;   /* 0x20008027, 0x20008028 */
extern uint8_t g_cmp_a_flags, g_cmp_b_flags; /* 0x20008029, 0x2000802A */
extern int32_t g_cmp_a_value, g_cmp_b_value; /* 0x20007D4C, 0x20007D50 */

void mem_flags_801c_set(void);
void mem_recall_to_a(void);
void mem_recall_to_b(void);

#endif /* MEM_RECORDS_H */
