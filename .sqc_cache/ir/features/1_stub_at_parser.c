#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/benchmarks/cmcell_features/stub_at_parser.c"
/**
 * @file stub_at_parser.c
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
 * @brief Stubbing for AT & URC parsers
 * @date 2025-07-11
 */
#include "cell_internal.h"
#include "cell_api.h"
#include "cell_com.h"
#include "cell_log.h"
#include "cell_modem_bg95.h"
#include "cell_modem_common.h"
#include "cell_at.h"
#include "cell_os.h"
#include "cell_task.h"
#include "cell_driver_interface.h"
#include "cell_types.h"
#include "cell_urc.h"

#include <sqc_builtin.h>
#include <limits.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/*@ requires (\valid_read(at_req->p_send_buff)); @*/
at_req_status_t
at_command_request (at_request_t * at_req, uint32_t timeout_ms,
                    at_parser_handler_t at_parser_handler)
{
    *at_req->p_error_code          = sqc_interval(0, INT_MAX);

    char line[AT_MAX_LINE_LEN + 1] = {0};
    sqc_make_unknown(line, AT_MAX_LINE_LEN);
    line[AT_MAX_LINE_LEN] = '\0';

    if (at_parser_handler)
    {
        at_parser_handler(at_req, line, 0, 1);
    }
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

/*@ requires (\valid(line)) && (\valid(error_code)); @*/
at_req_status_t
seek_one_line_and_parse_final_resp (char * line, uint64_t start_time,
                                    uint32_t timeout_ms, uint16_t * error_code)
{
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

/*@ requires (\valid(line)) && (\valid(error_code)); @*/
at_req_status_t
seek_and_parse_error_resp (char * line, uint64_t start_time,
                           uint32_t timeout_ms, uint16_t * error_code)
{
    return sqc_interval(AT_TIMEOUT, AT_ERROR_CMS);
}

void
urc_set_handler_table (urc_map_handler_t * p_handlers)
{
}

void
check_at_req_status (at_req_status_t status)
{
    sqc_assert(status > AT_PENDING);
}

void
generalize_string (char * buffer, int length)
{
    for (int i = 0; i < length; i++)
    {
        buffer[i] = sqc_interval(0, 255);
    }
    buffer[length] = '\0';
}
