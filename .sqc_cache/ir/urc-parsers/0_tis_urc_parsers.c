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
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/trustinsoft/at_parser/tis_urc_parsers.c"
/**
 * @file tis_urc_parsers.c
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
 * @author Dev : NGUYEN Thanh Minh
 * @brief TIS test driver for URC parsers
 * @date 2025-07-11
 */
#include "cell_api.h"
#include "cell_at.h"
#include "cell_urc.h"
#include "cell_os.h"
#include "cell_types.h"
#include "cell_com.h"

#include <sqc_builtin.h>
#include <limits.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

urc_map_handler_t urc_handler_table[URC_TYPE_NUMBER] = {0};

void
urc_empty_handler (void * urc_data)
{
}

int
tis_main ()
{
    char line[AT_MAX_LINE_LEN];
    for (int i = 0; i < AT_MAX_LINE_LEN - 1; i++)
    {
        line[i] = tis_interval(0, 255);
    }
    line[AT_MAX_LINE_LEN - 1] = '\0';

    for (int i = 0; i < URC_TYPE_NUMBER; i++)
    {
        urc_handler_table[i].handler = urc_empty_handler;
    }
    urc_set_handler_table(urc_handler_table);

    urc_parse_unsolicited_response(line);

    return 0;
}
