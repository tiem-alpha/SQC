#include <sqc_builtin.h>
#ifndef SQC_LEGACY_COMPAT_ALIASES
#define SQC_LEGACY_COMPAT_ALIASES
#ifndef tis_interval
#define tis_interval sqc_interval
#endif
#ifndef tis_int_interval
#define tis_int_interval sqc_int_interval
#endif
#ifndef tis_long_long_interval
#define tis_long_long_interval sqc_long_long_interval
#endif
#ifndef tis_unsigned_char_interval
#define tis_unsigned_char_interval sqc_unsigned_char_interval
#endif
#ifndef tis_unsigned_int_interval
#define tis_unsigned_int_interval sqc_unsigned_int_interval
#endif
#ifndef tis_unsigned_long_interval
#define tis_unsigned_long_interval sqc_unsigned_long_interval
#endif
#ifndef tis_interval_split
#define tis_interval_split sqc_interval_split
#endif
#ifndef tis_make_unknown
#define tis_make_unknown sqc_make_unknown
#endif
#ifndef tis_nondet
#define tis_nondet sqc_nondet
#endif
#ifndef tis_nondet_ptr
#define tis_nondet_ptr sqc_nondet_ptr
#endif
#ifndef tis_nondet3_ptr
#define tis_nondet3_ptr sqc_nondet3_ptr
#endif
#ifndef tis_nondet5_ptr
#define tis_nondet5_ptr sqc_nondet5_ptr
#endif
#ifndef tis_find_variable
#define tis_find_variable sqc_find_variable
#endif
#ifndef tis_override
#define tis_override sqc_override
#endif
#ifndef tis_ub
#define tis_ub sqc_ub
#endif
#ifndef tis_entropy_source
#define tis_entropy_source sqc_entropy_source
#endif
#endif
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_log.c"
/**
 * @file cell_log.c
 * @copyright
 * Copyright (C) 2025 Sagemcom. All rights reserved.
 * @par Licence
 * The source code contained herein contains proprietary
 * information which is the property of Sagemcom.
 * The source code and program may be used and/or copied
 * only with the written permission of Sagemcom.
 * or in accordance with the terms and conditions
 * stipulated in the agreement/contract under which the programs
 * have been supplied.
 * @author Dev : NGUYEN TRI Dung
 * @brief Implementation of logging functions for the cellular module.
 * @date 2025-03-31
 */
#include "cell_log.h"

/**
 * @brief Global variable to hold the current log level.
 */
static cell_log_levels_t g_log_level = CELL_LOG_LEVEL_OFF;

void
cell_log_set_level (cell_log_levels_t level)
{
    g_log_level = level;
}

__attribute__((weak)) void
cell_log_print (const char * prefix, const char * format, va_list args)
{
    printf("CMCELL [%s]: ", prefix);
    vprintf(format, args);
    printf("\n");
}

void
cell_log_normal (const char * format, ...)
{
    if (CELL_LOG_LEVEL_NORMAL <= g_log_level)
    {
        va_list args;
        va_start(args, format);
        cell_log_print("NORMAL", format, args);
        va_end(args);
    }
}

void
cell_log_debug (const char * format, ...)
{
    if (CELL_LOG_LEVEL_DEBUG <= g_log_level)
    {
        va_list args;
        va_start(args, format);
        cell_log_print("DEBUG", format, args);
        va_end(args);
    }
}

void
cell_log_verbose (const char * format, ...)
{
    if (CELL_LOG_LEVEL_VERBOSE <= g_log_level)
    {
        va_list args;
        va_start(args, format);
        cell_log_print("VERBOSE", format, args);
        va_end(args);
    }
}
