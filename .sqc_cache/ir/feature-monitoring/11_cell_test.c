#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_test.c"
/**
 * @file cell_test.c
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
 * @author Dev : HOANG THANH Long
 * @brief Common module cellular testing feature source file
 * @date 2025-07-02
 */
#include "cell_test.h"
#include "cell_log.h"
#include "cell_os.h"
#include "cell_com.h"
#include "cell_at.h"
#include "cell_modem_common.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief Current modem driver
 */
extern modem_driver_t *       gp_modem_driver;

static cell_test_response_t * gp_response = NULL;

static void                   request_prepare(cell_test_type_t      type,
                                              cell_test_request_t * request);
static void result_process(cell_context_t * ctx, cell_test_type_t type);
static void event_notify(cell_context_t * ctx, cell_test_event_t event);
static at_req_status_t send_custom_at_command(cell_test_request_t * request);
static at_req_status_t at_parser_custom_at(at_request_t * at_req, char * line,
                                           uint64_t start_time,
                                           uint32_t timeout_ms);

void
cell_test_process (message_t * msg)
{
    if ((NULL == gp_modem_driver) || (NULL == msg->data.ptr)
        || (NULL == gp_response))
    {
        cell_log_verbose("The test feature is not ready!");
        return;
    }

    cell_test_request_t * p_request = (cell_test_request_t *) msg->data.ptr;
    request_prepare((cell_test_type_t) msg->sub_type, p_request);
    at_req_status_t at_status = send_custom_at_command(p_request);
    if (AT_SUCCESS == at_status)
    {
        gp_response->success = true;
    }
    else
    {
        gp_response->success = false;
    }
    result_process(msg->context, p_request->type);
}

void
cell_test_notify_custom_at (cell_context_t * const      context,
                            const cell_test_request_t * request,
                            cell_test_response_t *      response)
{
    gp_response = response;
    send_msg_with_data(MESSAGE_CUSTOM_AT_CMD,
                       (uint8_t) request->type,
                       (void *) request,
                       sizeof(cell_test_request_t),
                       context,
                       false);
}

static void
request_prepare (cell_test_type_t type, cell_test_request_t * request)
{
    switch (type)
    {
        case CELL_TEST_GET_IMEI:
        {
            (void) strcpy(request->data, "AT+CGSN");
            request->prefix[0] = '\0';
        }
        break;

        case CELL_TEST_GET_IMSI:
        {
            (void) strcpy(request->data, "AT+CIMI");
            request->prefix[0] = '\0';
        }
        break;

        case CELL_TEST_CUSTOM_AT:
            break;

        default:
            break;
    }
}

static void
result_process (cell_context_t * ctx, cell_test_type_t type)
{
    switch (type)
    {
        case CELL_TEST_GET_IMEI:
            event_notify(ctx, CELL_TEST_EVENT_GET_IMEI);
            break;

        case CELL_TEST_GET_IMSI:
            event_notify(ctx, CELL_TEST_EVENT_GET_IMSI);
            break;

        case CELL_TEST_CUSTOM_AT:
            event_notify(ctx, CELL_TEST_EVENT_CUSTOM_AT);
            break;

        default:
            break;
    }
}

static void
event_notify (cell_context_t * ctx, cell_test_event_t event)
{
    if (NULL != ctx)
    {
        if (NULL != ctx->test_cb)
        {
            ctx->test_cb(event);
        }
    }
}

static at_req_status_t
send_custom_at_command (cell_test_request_t * request)
{
    at_req_status_t ret         = AT_PENDING;
    uint16_t        error_code  = 0;
    char            cmd_buf[64] = {0};
    at_request_t    at_req;
    (void) memset(&at_req, 0x0, sizeof(at_req));

    at_req.p_send_buff  = cmd_buf;
    at_req.p_data       = NULL;
    at_req.data_len     = 0;
    at_req.p_error_code = &error_code;
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "%s\r", request->data);
    at_req.send_buff_len = (uint16_t) strlen(at_req.p_send_buff);

    if ('\0' == request->prefix[0])
    {
        at_req.resp_prefix = NULL;
    }
    else
    {
        at_req.resp_prefix = request->prefix;
    }

    ret = at_command_request(&at_req, request->timeout, at_parser_custom_at);

    return ret;
}

static at_req_status_t
at_parser_custom_at (at_request_t * at_req, char * line, uint64_t start_time,
                     uint32_t timeout_ms)
{
    at_req_status_t ret = AT_SUCCESS;
    if (0 == strcmp("OK", line))
    {
        return ret;
    }
    (void) strncpy(gp_response->data, line, sizeof(gp_response->data) - 1);
    gp_response->data[sizeof(gp_response->data) - 1] = '\0';

    ret = seek_and_parse_final_resp(
        line, start_time, timeout_ms, at_req->p_error_code);
    return ret;
}
