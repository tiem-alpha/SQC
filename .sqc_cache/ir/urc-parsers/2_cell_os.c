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
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_os.c"
/**
 * @file cell_os.c
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
 * @brief Weak implementations of OS abstraction functions.
 * @date 2025-03-31
 */
#include "cell_os.h"
#include <stdio.h>
#include <stdbool.h>

/*******************************************************
 * Third-party Header Files                            *
 *******************************************************/
#ifdef __TRUSTINSOFT_ANALYZER__
#include <sqc_builtin.h>
#endif

static uint8_t g_task_handle_dummy;
static uint8_t g_queue_handle_dummy;

__attribute__((weak)) cell_os_err_t
cell_os_timer_create (uint32_t * timer_id, void (*callback)(void * arg))
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) cell_os_err_t
cell_os_timer_start (uint32_t timer_id, uint32_t value)
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) cell_os_err_t
cell_os_timer_stop (uint32_t timer_id)
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) void
cell_os_timer_callback (void * arg)
{
}

__attribute__((weak)) cell_os_task_handle_t
cell_os_task_create (const char * name, cell_os_task_func task_func, void * arg,
                     uint16_t stack_size, uint8_t priority)
{
    (void) name;
    (void) task_func;
    (void) arg;
    (void) stack_size;
    (void) priority;

#ifdef __TRUSTINSOFT_ANALYZER__
    return tis_nondet_ptr((cell_os_task_handle_t) &g_task_handle_dummy, NULL);
#else
    return (cell_os_task_handle_t) &g_task_handle_dummy;
#endif
}

__attribute__((weak)) cell_os_queue_handle_t
cell_os_msg_create (uint32_t length, uint32_t msg_size)
{
    (void) length;
    (void) msg_size;

    return (cell_os_queue_handle_t) &g_queue_handle_dummy;
}

__attribute__((weak)) cell_os_err_t
cell_os_msg_write (cell_os_queue_handle_t queue_handle, const void * data,
                   uint16_t size, int32_t timeout)
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) cell_os_err_t
cell_os_msg_write_from_isr (cell_os_queue_handle_t queue_handle,
                            const void * data, uint16_t size)
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) cell_os_err_t
cell_os_msg_read (cell_os_queue_handle_t queue_handle, void * data,
                  int32_t timeout)
{
    return CELL_OS_SUCCESS;
}

__attribute__((weak)) void
cell_os_sleep_ms (uint32_t milliseconds)
{
}

__attribute__((weak)) void
cell_os_feed_wdg (void)
{
}

__attribute__((weak)) uint64_t
cell_os_get_time_ms (void)
{
    return (uint64_t) 0;
}
