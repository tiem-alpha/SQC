#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_quectel.c"
/**
 * @file cell_modem_quectel.c
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
 * @author Dev : HOANG THAI Duong
 * @brief Modem driver for Quectel cellular module
 * @date 2026-03-31
 */
#include "cell_modem_quectel.h"
#include "cell_modem_helper.h"
#include "cell_internal.h"
#include "cell_at.h"

#include "cell_log.h"
#include "cell_os.h"
#include "cell_connect.h"
#include "cell_types.h"
#include "cell_urc.h"
#include "cell_dfota.h"
#include "cell_socket.h"
#include "cell_com.h"

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#define QUECTEL_PDP_ACTIVATION_REQ_TIMEOUT_MS   (150000UL)
#define QUECTEL_PDP_DEACTIVATION_REQ_TIMEOUT_MS (40000UL)

/**
 * @brief Maximum number of sockets
 */
#define QUECTEL_MAXIMUM_SUPPORT_SOCKET        12

#define QUECTEL_CMD_RESPONSE_POLL_INTERVAL_MS 3
static at_req_status_t quectel_enable_functionality(uint16_t * error_code);

typedef enum
{
    BUFFER_ACCESS_MODE = 0,
    PUSH_MODE,
    TRANSPARENT_MODE,
} access_mode_t;

typedef struct
{
    const char *    type;
    cell_rat_type_t value;
} cell_rat_map_t;

static const cell_rat_map_t rat_map[] = {
    {"GSM",   CELL_RAT_GSM  },
    {"LTE",   CELL_RAT_LTE  },
    {"eMTC",  CELL_RAT_CATM1},
    {"NBIoT", CELL_RAT_NBIOT}
};

static at_req_status_t at_parser_iccid_get(at_request_t * at_req, char * line,
                                           uint64_t start_time,
                                           uint32_t timeout_ms);

static at_req_status_t at_parser_pdp_get(at_request_t * at_req, char * line,
                                         uint64_t start_time,
                                         uint32_t timeout_ms);

static at_req_status_t at_parser_pdp_settings_get(at_request_t * at_req,
                                                  char *         line,
                                                  uint64_t       start_time,
                                                  uint32_t       timeout_ms);

static at_req_status_t at_parser_operator_info_get(at_request_t * at_req,
                                                   char *         line,
                                                   uint64_t       start_time,
                                                   uint32_t       timeout_ms);

static at_req_status_t at_parser_quectel_firmware_version(at_request_t * at_req,
                                                          char *         line,
                                                          uint64_t start_time,
                                                          uint32_t timeout_ms);

static at_req_status_t at_parser_serving_cell_get(at_request_t * at_req,
                                                  char *         line,
                                                  uint64_t       start_time,
                                                  uint32_t       timeout_ms);

static at_req_status_t at_parser_neighbour_cell_get(at_request_t * at_req,
                                                    char *         line,
                                                    uint64_t       start_time,
                                                    uint32_t       timeout_ms);

static void build_socket_create_command(const cell_socket_settings_t * socket,
                                        uint32_t socket_id, char * cmd_buf,
                                        uint16_t len);

static at_req_status_t at_parser_socket_query_service_status(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t at_parser_socket_check_usability(at_request_t * at_req,
                                                        char *         line,
                                                        uint64_t start_time,
                                                        uint32_t timeout_ms);

static at_req_status_t at_parser_socket_get_data_verify(at_request_t * at_req,
                                                        char *         line,
                                                        uint64_t start_time,
                                                        uint32_t timeout_ms);

static at_req_status_t send_socket_data_wait_prefix(at_request_t * at_req,
                                                    uint32_t       timeout_ms);

static void build_socket_send_command(cell_socket_t * socket, char * cmd_buf,
                                      uint16_t cmd_len, size_t send_len);
static at_req_status_t at_parser_socket_get_unread_recv_len(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t at_parser_socket_receive(at_request_t * at_req,
                                                char *         line,
                                                uint64_t       start_time,
                                                uint32_t       timeout_ms);

static void build_socket_recv_command(cell_socket_t * socket, char * cmd_buf,
                                      uint16_t cmd_len, uint16_t recv_len);

static bool parse_socket_recv_info(socket_data_recv_info_t * recv_info,
                                   char * line, int * read_actual_len);

static void handle_urc_reg(urc_creg_status_t status);

at_req_status_t
quectel_iccid_get (char * p_iccid, uint16_t * error_code)
{
    at_req_status_t ret              = AT_PENDING;
    at_request_t    at_req_get_iccid = {.p_send_buff  = "AT+QCCID\r",
                                        .resp_prefix  = "+QCCID",
                                        .p_data       = p_iccid,
                                        .data_len     = CELL_ICCID_MAX_SIZE + 1,
                                        .p_error_code = error_code};

    at_req_get_iccid.send_buff_len   = strlen(at_req_get_iccid.p_send_buff);
    ret = at_command_request(&at_req_get_iccid, 300, at_parser_iccid_get);

    return ret;
}

at_req_status_t
quectel_sim_readiness_get (cell_sim_lock_status_t * status,
                           uint16_t *               error_code)
{
    at_req_status_t ret = AT_PENDING;

    /**
     * Quectel-specific: Before checking SIM readiness, the modem must be set to
     * full functionality
     */
    ret = quectel_enable_functionality(error_code);

    if (AT_SUCCESS == ret)
    {
        ret = common_sim_readiness_check(status, error_code);
    }

    return ret;
}

at_req_status_t
quectel_sim_pin_enter (cell_simcard_settings_t sim_settings,
                       bool * is_sim_ready_requires_urc, uint16_t * error_code)
{
    at_req_status_t ret = AT_PENDING;

    ret                 = common_pin_enter(sim_settings, error_code);

    /**
     * Quectel-specific: after entering the PIN, the modem will send +QUSIM URC
     * to indicate SIM readiness, so the user must wait for this URC before
     * treating the SIM as ready. As a result, set the flag
     * is_sim_ready_requires_urc as true.
     */
    if (AT_SUCCESS == ret)
    {
        *is_sim_ready_requires_urc = true;
    }

    return ret;
}

at_req_status_t
quectel_pdp_activate (uint8_t ctx_id, uint16_t * error_code)
{
    at_req_status_t at_status      = AT_PENDING;
    char            cmd_buf[16]    = {'\0'};

    at_request_t    at_req_act_pdp = {.p_send_buff  = cmd_buf,
                                      .resp_prefix  = NULL,
                                      .p_data       = NULL,
                                      .data_len     = 0,
                                      .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QIACT=%d\r", ctx_id);
    at_req_act_pdp.send_buff_len = strlen(at_req_act_pdp.p_send_buff);
    at_status                    = at_command_request(
        &at_req_act_pdp, QUECTEL_PDP_ACTIVATION_REQ_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
quectel_pdp_deactivate (uint8_t ctx_id, uint16_t * error_code)
{
    at_req_status_t at_status        = AT_PENDING;
    char            cmd_buf[16]      = {'\0'};

    at_request_t    at_req_deact_pdp = {.p_send_buff  = cmd_buf,
                                        .resp_prefix  = NULL,
                                        .p_data       = NULL,
                                        .data_len     = 0,
                                        .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QIDEACT=%d\r", ctx_id);
    at_req_deact_pdp.send_buff_len = strlen(at_req_deact_pdp.p_send_buff);
    at_status                      = at_command_request(
        &at_req_deact_pdp, QUECTEL_PDP_DEACTIVATION_REQ_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
quectel_pdp_status_get (cell_network_info_t * p_net_info, uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;
    at_request_t    at_req_get_pdp_status
        = {.p_send_buff  = "AT+QIACT?\r",
           .resp_prefix  = "+QIACT",
           .p_data       = p_net_info,
           .data_len     = (uint16_t) sizeof(cell_network_info_t),
           .p_error_code = error_code};

    at_req_get_pdp_status.send_buff_len
        = strlen(at_req_get_pdp_status.p_send_buff);
    at_status = at_command_request(&at_req_get_pdp_status,
                                   AT_COMMAND_COMMON_TIMEOUT_MS,
                                   at_parser_pdp_get);
    return at_status;
}

at_req_status_t
quectel_firmware_version_get (char * p_fw_ver, uint16_t * error_code)
{
    at_req_status_t ret               = AT_PENDING;
    at_request_t    at_req_fw_version = {.p_send_buff = "AT+QGMR\r",
                                         .resp_prefix = NULL,
                                         .p_data      = p_fw_ver,
                                         .data_len = CELL_FW_VERSION_MAX_SIZE + 1,
                                         .p_error_code = error_code};

    at_req_fw_version.send_buff_len   = strlen(at_req_fw_version.p_send_buff);
    ret                               = at_command_request(
        &at_req_fw_version, 300, at_parser_quectel_firmware_version);

    return ret;
}

at_req_status_t
quectel_module_poweroff (uint16_t * error_code)
{
    at_req_status_t at_status       = AT_PENDING;
    at_request_t    at_req_poweroff = {.p_send_buff  = "AT+QPOWD=1\r",
                                       .resp_prefix  = NULL,
                                       .p_data       = NULL,
                                       .data_len     = 0,
                                       .p_error_code = error_code};

    at_req_poweroff.send_buff_len   = strlen(at_req_poweroff.p_send_buff);
    // Send AT command
    at_status = at_command_request(
        &at_req_poweroff, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
quectel_pdp_settings_get (cell_pdp_settings_t * pdp_settings,
                          uint16_t *            error_code)
{
    at_req_status_t ret         = AT_PENDING;
    char            cmd_buf[20] = {'\0'};
    at_request_t    at_req_get_pdp_settings
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = "+QICSGP",
           .p_data       = pdp_settings,
           .data_len     = (uint16_t) sizeof(cell_pdp_settings_t),
           .p_error_code = error_code};

    (void) snprintf(
        cmd_buf, sizeof(cmd_buf), "AT+QICSGP=%d\r", pdp_settings->context_id);
    at_req_get_pdp_settings.send_buff_len
        = strlen(at_req_get_pdp_settings.p_send_buff);

    ret = at_command_request(
        &at_req_get_pdp_settings, 300, at_parser_pdp_settings_get);

    return ret;
}

at_req_status_t
quectel_pdp_settings_set (cell_pdp_settings_t * pdp_settings,
                          uint16_t *            error_code)
{
    at_req_status_t ret                     = AT_PENDING;
    char            cmd_buf[180]            = {'\0'};
    at_request_t    at_req_set_pdp_settings = {.p_send_buff  = cmd_buf,
                                               .resp_prefix  = NULL,
                                               .p_data       = NULL,
                                               .data_len     = 0,
                                               .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+QICSGP=%d,%d,\"%s\",\"%s\",\"%s\",%d\r",
                    pdp_settings->context_id,
                    (int) pdp_settings->type,
                    pdp_settings->apn_name,
                    pdp_settings->username,
                    pdp_settings->password,
                    (int) pdp_settings->auth_type);
    at_req_set_pdp_settings.send_buff_len
        = strlen(at_req_set_pdp_settings.p_send_buff);

    ret = at_command_request(&at_req_set_pdp_settings, 300, NULL);

    return ret;
}

at_req_status_t
quectel_operator_info_get (cell_operator_info_t * info, uint16_t * error_code)
{
    at_req_status_t ret             = AT_PENDING;
    at_request_t    at_req_settings = {.p_send_buff = "AT+COPS?\r",
                                       .resp_prefix = "+COPS",
                                       .p_data      = info,
                                       .data_len = sizeof(cell_operator_info_t),
                                       .p_error_code = error_code};

    at_req_settings.send_buff_len   = strlen(at_req_settings.p_send_buff);
    ret                             = at_command_request(
        &at_req_settings, 180000, at_parser_operator_info_get);

    return ret;
}

at_req_status_t
quectel_operator_settings_set (cell_operator_settings_t * operator_settings,
                               uint16_t *                 error_code)
{
    at_req_status_t ret                          = AT_PENDING;
    char            cmd_buf[100]                 = {'\0'};
    at_request_t    at_req_set_operator_settings = {.p_send_buff  = cmd_buf,
                                                    .resp_prefix  = NULL,
                                                    .p_data       = NULL,
                                                    .data_len     = 0,
                                                    .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+COPS=%d,%d\r",
                    (int) operator_settings->selection_mode,
                    (int) operator_settings->format_type);
    at_req_set_operator_settings.send_buff_len
        = strlen(at_req_set_operator_settings.p_send_buff);

    ret = at_command_request(&at_req_set_operator_settings, 180000, NULL);

    return ret;
}

at_req_status_t
quectel_socket_send (cell_socket_t * socket, const uint8_t * data,
                     size_t * length, uint16_t * error_code)
{
    at_req_status_t ret                = AT_PENDING;
    char            cmd_buf[85]        = {'\0'};
    at_request_t    at_req_socket_send = {.p_send_buff  = cmd_buf,
                                          .resp_prefix  = NULL,
                                          .p_data       = NULL,
                                          .data_len     = 0,
                                          .p_error_code = error_code};

    build_socket_send_command(socket, cmd_buf, sizeof(cmd_buf), *length);
    at_req_socket_send.send_buff_len = strlen(at_req_socket_send.p_send_buff);
    ret = send_socket_data_wait_prefix(&at_req_socket_send, 120000);

    if (AT_SUCCESS == ret)
    {
        at_req_socket_send.p_send_buff   = (char *) data;
        at_req_socket_send.send_buff_len = *length;

        ret = at_command_request(&at_req_socket_send, 120000, NULL);
    }

    return ret;
}

at_req_status_t
quectel_socket_get_data_verify (
    cell_socket_t * socket, cell_socket_data_verify_info_t * data_verify_info,
    uint16_t * error_code)
{
    at_req_status_t ret         = AT_PENDING;
    char            cmd_buf[70] = {'\0'};
    at_request_t    at_req_socket_get_byte_sent
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = "+QISEND",
           .p_data       = data_verify_info,
           .data_len     = sizeof(cell_socket_data_verify_info_t),
           .p_error_code = error_code};

    build_socket_send_command(socket, cmd_buf, sizeof(cmd_buf), 0);
    at_req_socket_get_byte_sent.send_buff_len
        = strlen(at_req_socket_get_byte_sent.p_send_buff);

    ret = at_command_request(
        &at_req_socket_get_byte_sent, 120000, at_parser_socket_get_data_verify);

    return ret;
}

static at_req_status_t
quectel_serving_cell_get (cell_cellular_info_t * cellular_info,
                          uint16_t *             error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_serving_cell
        = {.p_send_buff  = "AT+QENG=\"servingcell\"\r",
           .resp_prefix  = "+QENG",
           .p_data       = cellular_info,
           .data_len     = (uint16_t) sizeof(cell_cellular_info_t),
           .p_error_code = error_code};

    at_req_get_serving_cell.send_buff_len
        = strlen(at_req_get_serving_cell.p_send_buff);

    ret = at_command_request(
        &at_req_get_serving_cell, 300, at_parser_serving_cell_get);
    return ret;
}

static at_req_status_t
quectel_neighbour_cell_get (cell_cellular_info_t * cellular_info,
                            uint16_t *             error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_neighbour_cell
        = {.p_send_buff  = "AT+QENG=\"neighbourcell\"\r",
           .resp_prefix  = "+QENG",
           .p_data       = cellular_info,
           .data_len     = (uint16_t) sizeof(cell_cellular_info_t),
           .p_error_code = error_code};

    at_req_get_neighbour_cell.send_buff_len
        = strlen(at_req_get_neighbour_cell.p_send_buff);

    ret = at_command_request(
        &at_req_get_neighbour_cell, 5000, at_parser_neighbour_cell_get);
    return ret;
}

at_req_status_t
quectel_cellular_info_get (cell_cellular_info_t * cellular_info,
                           uint16_t *             error_code)
{
    at_req_status_t ret = AT_PENDING;
    ret                 = quectel_serving_cell_get(cellular_info, error_code);

    if (AT_SUCCESS == ret)
    {
        ret = quectel_neighbour_cell_get(cellular_info, error_code);
    }

    return ret;
}

at_req_status_t
quectel_urc_port_config (uint16_t * error_code)
{
    at_req_status_t ret  = AT_PENDING;
    at_request_t at_req  = {.p_send_buff  = "AT+QURCCFG=\"urcport\",\"uart1\"\r",
                            .resp_prefix  = NULL,
                            .p_data       = NULL,
                            .data_len     = 0,
                            .p_error_code = error_code};

    at_req.send_buff_len = strlen(at_req.p_send_buff);
    ret = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return ret;
}

at_req_status_t
quectel_socket_receive (cell_socket_t * socket, const uint8_t * data,
                        size_t * data_length, uint16_t * error_code)
{
    at_req_status_t ret             = AT_PENDING;
    char            cmd_buf[32]     = {'\0'};
    size_t          unread_recv_len = 0;

    if (CELL_SOCKET_SERVICE_TYPE_TCP_SERVER == socket->service_type)
    {
        ret = quectel_socket_recv_unread_len_get(
            socket->incoming_id, &unread_recv_len, error_code);
    }
    else
    {
        ret = quectel_socket_recv_unread_len_get(
            socket->socket_id, &unread_recv_len, error_code);
    }

    if (AT_SUCCESS == ret)
    {
        if (unread_recv_len < *data_length)
        {
            *data_length = unread_recv_len;
        }

        cell_log_debug("data length = %d", *data_length);

        socket_data_recv_info_t data_recv_info
            = {socket->service_type, data, data_length, &socket->remote};
        at_request_t at_req_socket_receive
            = {.p_send_buff  = cmd_buf,
               .resp_prefix  = "+QIRD",
               .p_data       = &data_recv_info,
               .data_len     = sizeof(data_recv_info),
               .p_error_code = error_code};

        build_socket_recv_command(
            socket, cmd_buf, sizeof(cmd_buf), *data_length);
        at_req_socket_receive.send_buff_len
            = strlen(at_req_socket_receive.p_send_buff);

        ret = at_command_request(
            &at_req_socket_receive, 120000, at_parser_socket_receive);
    }

    return ret;
}

at_req_status_t
quectel_socket_recv_unread_len_get (uint32_t   socket_id,
                                    size_t *   unread_recv_len,
                                    uint16_t * error_code)
{
    at_req_status_t ret                        = AT_PENDING;
    char            cmd_buf[32]                = {'\0'};
    at_request_t    at_req_socket_get_recv_len = {.p_send_buff = cmd_buf,
                                                  .resp_prefix = "+QIRD",
                                                  .p_data   = unread_recv_len,
                                                  .data_len = sizeof(size_t),
                                                  .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QIRD=%u,0\r", socket_id);
    at_req_socket_get_recv_len.send_buff_len
        = strlen(at_req_socket_get_recv_len.p_send_buff);

    ret = at_command_request(&at_req_socket_get_recv_len,
                             120000,
                             at_parser_socket_get_unread_recv_len);

    return ret;
}

static at_req_status_t
at_parser_iccid_get (at_request_t * at_req, char * line, uint64_t start_time,
                     uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    char *          info   = (char *) at_req->p_data;

    int             parsed = sscanf(line, "+QCCID: %20[^\n]", info);
    if (parsed == 1)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_pdp_get (at_request_t * at_req, char * line, uint64_t start_time,
                   uint32_t timeout_ms)
{
    at_req_status_t at_status = AT_SUCCESS;
    if (strstr(line, "OK") != NULL)
    {
        // There are not any activated PDP contexts, but it is still considered
        // a successful response
        at_status = AT_SUCCESS;
    }
    else
    {
        cell_network_info_t * p_net_info
            = (cell_network_info_t *) at_req->p_data;
        char ip_addr[40] = {'\0'};
        if ((sscanf(line,
                    "+QIACT: %d,%d,%d,\"%39[^\"]\"",
                    (int *) &p_net_info->cid,
                    (int *) &p_net_info->state,
                    (int *) &p_net_info->pdp_type,
                    ip_addr)
             != 4))
        {
            at_status = AT_TIMEOUT;
        }
        else
        {
            helper_ip_convert(ip_addr, p_net_info);
        }

        if (AT_SUCCESS == at_status)
        {
            at_status = seek_one_line_and_parse_final_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
        }
        else
        {
            at_status = seek_and_parse_error_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
        }
    }

    return at_status;
}

static at_req_status_t
at_parser_pdp_settings_get (at_request_t * at_req, char * line,
                            uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_pdp_settings_t * pdp_settings = (cell_pdp_settings_t *) at_req->p_data;

    // Response example : +QICSGP: 1,"viettel","admin","admin",1
    size_t offset         = 0;
    int    consumed_bytes = 0;
    size_t line_len       = strlen(line);
    int    parsed         = sscanf(line,
                        "+QICSGP: %2hhu,%n",
                        (uint8_t *) &pdp_settings->type,
                        (int *) &offset);

    // Parse apn_name within a quoted
    consumed_bytes = helper_extract_quoted_string(
        line, offset, pdp_settings->apn_name, CELL_APN_MAX_SIZE + 1);
    if (consumed_bytes > 0)
    {
        offset += (size_t) consumed_bytes; // Safety cast: value already checked
                                           // to be non-negative
        parsed++;
    }

    // Expecting ','
    if (offset < line_len && line[offset] == ',')
    {
        offset++;
    }

    // Parse username within a quoted
    consumed_bytes = helper_extract_quoted_string(
        line, offset, pdp_settings->username, CELL_PDP_USERNAME_MAX_SIZE + 1);
    if (consumed_bytes > 0)
    {
        offset += (size_t) consumed_bytes; // Safety cast: value already checked
                                           // to be non-negative
        parsed++;
    }

    // Expecting ','
    if (offset < line_len && line[offset] == ',')
    {
        offset++;
    }

    // Parse password within a quoted
    consumed_bytes = helper_extract_quoted_string(
        line, offset, pdp_settings->password, CELL_PDP_PASSWORD_MAX_SIZE + 1);
    if (consumed_bytes > 0)
    {
        offset += (size_t) consumed_bytes; // Safety cast: value already checked
                                           // to be non-negative
        parsed++;
    }

    // Expecting ','
    if (offset < line_len && line[offset] == ',')
    {
        offset++;
    }

    if (offset < line_len)
    {
        // Parse authentication type
        parsed += sscanf(
            line + offset, "%2hhu", (uint8_t *) &pdp_settings->auth_type);
    }

    if (parsed >= 2)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_operator_info_get (at_request_t * at_req, char * line,
                             uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t        ret = AT_SUCCESS;
    cell_operator_info_t * operator=(cell_operator_info_t *) at_req -> p_data;
    int                    mode   = 0;
    int                    format = 0;
    int                    rat    = 0;

    int                    parsed = sscanf(line,
                        "+COPS: %d,%d,\"%64[^\"]\",%d",
                        &mode,
                        &format,
                        operator->name,
                        &rat);

    if (parsed >= 1)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    if (AT_SUCCESS == ret)
    {
        operator->mode   =(cell_operator_selection_mode_t) mode;
        operator->format =(cell_operator_name_format_t) format;
        switch (rat)
        {
            case URC_ACT_GSM:
                operator->rat = CELL_RAT_GSM;
                break;

            case URC_ACT_eMTC:
                operator->rat = CELL_RAT_CATM1;
                break;

            case URC_ACT_EUTRAN:
                operator->rat = CELL_RAT_LTE;
                break;

            case URC_ACT_NB_IOT:
                operator->rat = CELL_RAT_NBIOT;
                break;
            default:
                ret = AT_TIMEOUT;
                break;
        }
    }
    return ret;
}

static at_req_status_t
at_parser_quectel_firmware_version (at_request_t * at_req, char * line,
                                    uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    char *          info   = (char *) at_req->p_data;

    int             parsed = sscanf(line, "%32[^\n]", info);
    if (parsed == 1)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

void
quectel_handle_urc_socket_recv (void * data)
{
    urc_qiurc_recv_buffer_mode_type_t * qiurc_recv_info
        = (urc_qiurc_recv_buffer_mode_type_t *) data;

    if (qiurc_recv_info->connect_id < QUECTEL_MAXIMUM_SUPPORT_SOCKET)
    {
        socket_process_urc_incoming_data(qiurc_recv_info->connect_id);
    }
}

void
quectel_handle_urc_qusim (void * data)
{
    urc_qusim_state_t *   type     = (urc_qusim_state_t *) data;
    connection_urc_type_t urc_type = CONNECTION_URC_INVALID;

    switch (*type)
    {
        case URC_QUSIM_USIM_DETECTED:
            urc_type = CONNECTION_URC_SIM_READY;
            break;

        default:
            break;
    }

    if (urc_type != CONNECTION_URC_INVALID)
    {
        connection_notify_urc(urc_type);
    }
}

static void
handle_urc_reg (urc_creg_status_t status)
{
    connection_urc_type_t urc_type = CONNECTION_URC_INVALID;

    switch (status)
    {
        case URC_CREG_STATUS_REGISTERED:
        case URC_CREG_STATUS_REGISTERED_ROAMING:
            urc_type = CONNECTION_URC_REGISTERED;
            break;

        case URC_CREG_STATUS_NOT_REGISTERED_AND_IDLE:
            urc_type = CONNECTION_URC_NOT_REGISTERING_IDLE;
            break;

        case URC_CREG_STATUS_UNKNOWN:
            urc_type = CONNECTION_URC_NOT_REGISTERING_UNKNOWN;
            break;

        case URC_CREG_STATUS_DENIED:
            urc_type = CONNECTION_URC_REJECTED;
            break;

        case URC_CREG_STATUS_NOT_REGISTERED_AND_SEARCHING:
            urc_type = CONNECTION_URC_REGISTERING;
            break;

        default:
            break;
    }

    if (urc_type != CONNECTION_URC_INVALID)
    {
        connection_notify_urc(urc_type);
    }
}

void
quectel_handle_urc_cereg (void * data)
{
    urc_creg_status_t status = ((urc_cereg_type_t *) data)->status;

    handle_urc_reg(status);
}

void
quectel_handle_urc_cgreg (void * data)
{
    urc_creg_status_t status = ((urc_cgreg_type_t *) data)->status;

    handle_urc_reg(status);
}

void
quectel_handle_urc_socket_open (void * data)
{
    urc_qiopen_type_t * qiopen_info = (urc_qiopen_type_t *) data;

    if (qiopen_info->connect_id < QUECTEL_MAXIMUM_SUPPORT_SOCKET)
    {
        if (0 == qiopen_info->result)
        {
            socket_process_urc_opened_success(qiopen_info->connect_id);
        }
        else
        {
            socket_process_urc_open_failed(qiopen_info->connect_id);
        }
    }
}

void
quectel_handle_urc_socket_closed (void * data)
{
    urc_qiurc_closed_type_t * qi_close_info = (urc_qiurc_closed_type_t *) data;

    if (qi_close_info->connect_id < QUECTEL_MAXIMUM_SUPPORT_SOCKET)
    {
        socket_process_urc_closed(qi_close_info->connect_id);
    }
}

void
quectel_handle_urc_socket_incoming (void * data)
{
    urc_qiurc_incoming_type_t * qiurc_incoming_info
        = (urc_qiurc_incoming_type_t *) data;

    if ((qiurc_incoming_info->connect_id < QUECTEL_MAXIMUM_SUPPORT_SOCKET)
        && (qiurc_incoming_info->server_id < QUECTEL_MAXIMUM_SUPPORT_SOCKET))
    {
        socket_process_urc_incoming_connection(qiurc_incoming_info->connect_id,
                                               qiurc_incoming_info->server_id);
    }
}

void
quectel_handle_urc_cpin (void * data)
{
    urc_cpin_type_t *     type     = (urc_cpin_type_t *) data;
    connection_urc_type_t urc_type = CONNECTION_URC_INVALID;

    switch (*type)
    {
        case URC_CPIN_SIM_NOT_READY:
            urc_type = CONNECTION_URC_SIM_NOT_READY;
            break;

        case URC_CPIN_SIM_NOT_INSERTED:
            urc_type = CONNECTION_URC_SIM_NOT_INSERTED;
            break;

        case URC_CPIN_SIM_PIN:
            urc_type = CONNECTION_URC_SIM_PIN;
            break;

        default:
            break;
    }

    if (urc_type != CONNECTION_URC_INVALID)
    {
        connection_notify_urc(urc_type);
    }
}

void
quectel_handle_urc_qiurc_pdpdeact (void * data)
{
    connection_notify_urc(CONNECTION_URC_PDP_DEACTIVATED);
}

cme_error_t
quectel_map_cme_error (uint16_t error_code)
{
    cme_error_t cme_err = CME_ERR_INVALID;
    switch (error_code)
    {
        case QUECTEL_CME_ERR_USIM_NOT_INSERTED:
            cme_err = CME_ERR_SIM_NOT_INSERTED;
            break;

        case QUECTEL_CME_ERR_USIM_FAILURE:
            cme_err = CME_ERR_SIM_NOT_READY;
            break;

        default:
            break;
    }

    return cme_err;
}

cms_error_t
quectel_map_cms_error (uint16_t error_code)
{
    cms_error_t cms_err = CMS_ERR_INVALID;
    return cms_err;
}

static at_req_status_t
quectel_enable_functionality (uint16_t * error_code)
{
    at_req_status_t         ret   = AT_PENDING;
    cell_ue_functionality_t state = CELL_UE_MINIMUM_FUNCTIONALITY;

    ret = common_ue_functionality_get(&state, error_code);

    if (AT_SUCCESS == ret && CELL_UE_FULL_FUNCTIONALITY != state)
    {
        state = CELL_UE_FULL_FUNCTIONALITY;
        ret   = common_ue_functionality_set(state, error_code);
        if (AT_SUCCESS == ret)
        {
            // wait for modem to be fully
            // enabled
            cell_os_sleep_ms(5000);
        }
    }

    return ret;
}

static cell_rat_type_t
get_rat_type (const char * rat_type_str)
{
    cell_rat_type_t rat_type = CELL_RAT_INVALID; // Default unknown type

    for (size_t i = 0; i < sizeof(rat_map) / sizeof(rat_map[0]); i++)
    {
        if (strcmp(rat_type_str, rat_map[i].type) == 0)
        {
            rat_type = rat_map[i].value;
            break;
        }
    }

    return rat_type;
}

static at_req_status_t
at_parser_serving_cell_get (at_request_t * at_req, char * line,
                            uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t        ret       = AT_SUCCESS;
    cell_cellular_info_t * cell_info = (cell_cellular_info_t *) at_req->p_data;

    bool                   b_is_data_parsed = true;
    char                   state_str[8]     = {0};
    char                   rat_str[6]       = {0};
    int parsed = sscanf(line, "+QENG: \"servingcell\",\"%7[^\"]\"", state_str);

    if (parsed == 1)
    {
        if (strcmp(state_str, "SEARCH") != 0)
        {
            parsed = sscanf(line,
                            "+QENG: \"servingcell\",\"%7[^\"]\",\"%5[^\"]\"",
                            state_str,
                            rat_str);
            if (parsed != 2)
            {
                ret              = AT_TIMEOUT;
                b_is_data_parsed = false;
            }
            else
            {
                cell_info->serving_cell.rat = get_rat_type(rat_str);
                switch (cell_info->serving_cell.rat)
                {
                    case CELL_RAT_GSM:
                    {
                        parsed = sscanf(line,
                                        "+QENG: "
                                        "\"servingcell\",\"%7[^\"]\",\"%5[^\"]"
                                        "\",%3[^,],%3[^,],%*x,%x",
                                        state_str,
                                        rat_str,
                                        cell_info->serving_cell.plmn.mcc,
                                        cell_info->serving_cell.plmn.mnc,
                                        &cell_info->serving_cell.cid);
                        if (parsed != 5)
                        {
                            b_is_data_parsed = false;
                            ret              = AT_TIMEOUT;
                        }
                    }
                    break;

                    case CELL_RAT_LTE:
                    case CELL_RAT_CATM1:
                    case CELL_RAT_NBIOT:
                    {
                        char network_mode[4] = {0};
                        parsed               = sscanf(
                            line,
                            "+QENG: "
                                          "\"servingcell\",\"%7[^\"]\",\"%5[^\"]\",\"%3[^\"]"
                                          "\",%3[^,],%3[^,],%x,%*d,%u,%2hhu,%2hhu,"
                                          "%2hhu,%hx,%hd,%hd,%hd,%hd,%*d",
                            state_str,
                            rat_str,
                            network_mode,
                            cell_info->serving_cell.plmn.mcc,
                            cell_info->serving_cell.plmn.mnc,
                            &cell_info->serving_cell.cid,
                            &cell_info->serving_cell.earfcn,
                            &cell_info->serving_cell.band_ind,
                            &cell_info->serving_cell.ul_bw,
                            &cell_info->serving_cell.dl_bw,
                            &cell_info->serving_cell.tac,
                            &cell_info->serving_cell.signal.rsrp,
                            &cell_info->serving_cell.signal.rsrq,
                            &cell_info->serving_cell.signal.rssi,
                            &cell_info->serving_cell.signal.sinr);
                        if (parsed != 15)
                        {
                            b_is_data_parsed = false;
                            ret              = AT_TIMEOUT;
                        }
                        else
                        {
                            cell_info->serving_cell.is_tdd
                                = strcmp(network_mode, "FDD") != 0;
                        }
                    }

                    break;

                    default:
                    {
                        ret = AT_TIMEOUT;
                    }
                    break;
                }
            }
        }
    }
    else
    {
        ret              = AT_TIMEOUT;
        b_is_data_parsed = false;
    }

    if (true == b_is_data_parsed || AT_SUCCESS == ret)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_neighbour_cell_get (at_request_t * at_req, char * line,
                              uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t        ret       = AT_SUCCESS;
    cell_cellular_info_t * cell_info = (cell_cellular_info_t *) at_req->p_data;

    bool                   b_is_data_parsed = true;
    char                   rat_str[6]       = {0};
    char                   dummy_str[32]    = {0};
    cell_info->neighbour_cell_num           = 0;

    while (cell_info->neighbour_cell_num < CELL_NEIGHBOUR_CELL_MAX_SIZE)
    {
        /*
         * If the current response is OK, then it's a success.
         * In the absence of a neighbor cell, the modem may respond OK
         * immediately.
         */

        if (strcmp(line, "OK") == 0)
        {
            return AT_SUCCESS;
        }

        cell_cellular_cell_info_t * cell
            = &cell_info->neighbour_cell[cell_info->neighbour_cell_num];
        int parsed = sscanf(
            line, "+QENG: \"%31[^\"]\",\"%5[^\"]\"", dummy_str, rat_str);
        if (parsed == 2)
        {
            cell->rat = get_rat_type(rat_str);
            switch (cell->rat)
            {
                case CELL_RAT_GSM:
                {
                    parsed = sscanf(
                        line,
                        "+QENG: "
                        "\"neighbourcell\",\"%5[^\"]\",%3[^,],%3[^,],%*x,%x",
                        rat_str,
                        cell->plmn.mcc,
                        cell->plmn.mnc,
                        &cell->cid);

                    if (parsed != 4)
                    {
                        b_is_data_parsed = false;
                        ret              = AT_TIMEOUT;
                    }
                }
                break;

                case CELL_RAT_LTE:
                case CELL_RAT_CATM1:
                case CELL_RAT_NBIOT:
                {
                    parsed = sscanf(
                        line,
                        "+QENG: \"neighbourcell "
                        "%31[^\"]\",\"%5[^\"]\",%u,%*d,%hd,%hd,%hd,%hd,%*d",
                        dummy_str,
                        rat_str,
                        &cell->earfcn,
                        &cell->signal.rsrq,
                        &cell->signal.rsrp,
                        &cell->signal.rssi,
                        &cell->signal.sinr);
                    if (parsed != 7)
                    {
                        b_is_data_parsed = false;
                        ret              = AT_TIMEOUT;
                    }
                }
                break;

                default:
                {
                    ret = AT_TIMEOUT;
                }
                break;
            }
        }
        else
        {
            b_is_data_parsed = false;
            ret              = AT_TIMEOUT;
        }

        if (true == b_is_data_parsed || AT_SUCCESS == ret)
        {
            cell_info->neighbour_cell_num++;
            ret = seek_one_line_and_parse_final_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
            if (ret == AT_SUCCESS)
            {
                break;
            }
        }
        else
        {
            ret = seek_and_parse_error_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
            break;
        }
    }

    if (ret != AT_SUCCESS)
    {
        cell_info->neighbour_cell_num = 0;
    }

    return ret;
}

at_req_status_t
quectel_socket_create (const cell_socket_settings_t * socket,
                       uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t ret                  = AT_PENDING;
    char            cmd_buf[128]         = {'\0'};
    at_request_t    at_req_socket_create = {.p_send_buff  = cmd_buf,
                                            .resp_prefix  = NULL,
                                            .p_data       = NULL,
                                            .data_len     = 0,
                                            .p_error_code = error_code};

    build_socket_create_command(socket, socket_id, cmd_buf, sizeof(cmd_buf));
    at_req_socket_create.send_buff_len
        = strlen(at_req_socket_create.p_send_buff);

    ret = at_command_request(&at_req_socket_create, 150000, NULL);

    return ret;
}

at_req_status_t
quectel_socket_close (uint32_t connect_id, cell_socket_service_type_t type,
                      uint16_t * error_code)
{
    (void) type;
    at_req_status_t ret                 = AT_PENDING;
    char            cmd_buf[32]         = {'\0'};
    at_request_t    at_req_socket_close = {.p_send_buff  = cmd_buf,
                                           .resp_prefix  = NULL,
                                           .p_data       = NULL,
                                           .data_len     = 0,
                                           .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QICLOSE=%u\r", connect_id);
    at_req_socket_close.send_buff_len = strlen(at_req_socket_close.p_send_buff);

    ret = at_command_request(&at_req_socket_close, 20000, NULL);

    return ret;
}

at_req_status_t
quectel_socket_query_service_status (cell_socket_t * socket,
                                     uint16_t *      error_code)
{
    at_req_status_t ret         = AT_PENDING;
    char            cmd_buf[32] = {'\0'};
    at_request_t    at_req_socket_query_service_status
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = "+QISTATE",
           .p_data       = &socket->socket_id,
           .data_len     = sizeof(socket->socket_id),
           .p_error_code = error_code};

    (void) snprintf(
        cmd_buf, sizeof(cmd_buf), "AT+QISTATE=1,%u\r", socket->socket_id);
    at_req_socket_query_service_status.send_buff_len
        = strlen(at_req_socket_query_service_status.p_send_buff);

    ret = at_command_request(&at_req_socket_query_service_status,
                             300,
                             at_parser_socket_query_service_status);

    return ret;
}

at_req_status_t
quectel_socket_check_usability (all_socket_status_t * socket_state_handle,
                                uint8_t context_id, uint16_t * error_code)
{
    at_req_status_t ret         = AT_PENDING;
    char            cmd_buf[32] = {'\0'};
    at_request_t    at_req_socket_query_all_service_status
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = "+QISTATE",
           .p_data       = socket_state_handle,
           .data_len     = sizeof(all_socket_status_t),
           .p_error_code = error_code};

    socket_state_handle->no_supported_socket = QUECTEL_MAXIMUM_SUPPORT_SOCKET;

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QISTATE=0,%d\r", context_id);
    at_req_socket_query_all_service_status.send_buff_len
        = strlen(at_req_socket_query_all_service_status.p_send_buff);

    ret = at_command_request(&at_req_socket_query_all_service_status,
                             3000,
                             at_parser_socket_check_usability);

    return ret;
}

static void
build_socket_create_command (const cell_socket_settings_t * socket,
                             uint32_t socket_id, char * cmd_buf,
                             uint16_t cmd_len)
{
    char          service_type[15]                          = {0};
    char          ip_address[CELL_IP_ADDRESS_MAX_SIZE + 1U] = {0};
    access_mode_t access_mode = BUFFER_ACCESS_MODE;

    switch (socket->service_type)
    {
        case CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT:
        {
            (void) strcpy(service_type, "TCP");
            helper_ip_to_str(socket->remote, ip_address);
        }
        break;
        case CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT:
        {
            (void) strcpy(service_type, "UDP");
            helper_ip_to_str(socket->remote, ip_address);
        }
        break;
        case CELL_SOCKET_SERVICE_TYPE_TCP_SERVER:
        {
            (void) strcpy(service_type, "TCP LISTENER");
            (void) strcpy(ip_address, "127.0.0.1");
        }
        break;
        case CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE:
        {
            (void) strcpy(service_type, "UDP SERVICE");
            (void) strcpy(ip_address, "127.0.0.1");
        }
        break;
        default:
            break;
    }

    (void) snprintf(cmd_buf,
                    cmd_len,
                    "AT+QIOPEN=%d,%u,\"%s\",\"%s\",%d,%d,%d\r",
                    socket->context_id,
                    socket_id,
                    service_type,
                    ip_address,
                    socket->remote.port,
                    socket->local.port,
                    (int) access_mode);
}

static at_req_status_t
at_parser_socket_query_service_status (at_request_t * at_req, char * line,
                                       uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret              = AT_SUCCESS;
    uint32_t *      config_socket_id = (uint32_t *) at_req->p_data;
    uint32_t        recv_socket_id   = 0;

    int             parsed = sscanf(line, "+QISTATE: %u", &recv_socket_id);

    if ((parsed == 1) && (recv_socket_id == *config_socket_id))
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_socket_get_data_verify (at_request_t * at_req, char * line,
                                  uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t                  ret = AT_SUCCESS;
    cell_socket_data_verify_info_t * data_verify_info
        = (cell_socket_data_verify_info_t *) at_req->p_data;

    int parsed = sscanf(line,
                        "+QISEND: %hu,%hu,%hu",
                        &data_verify_info->total_sent_bytes,
                        &data_verify_info->acked_bytes,
                        &data_verify_info->unacked_bytes);

    if (parsed == 3)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_socket_get_unread_recv_len (at_request_t * at_req, char * line,
                                      uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret           = AT_SUCCESS;
    size_t *        unread_length = (size_t *) at_req->p_data;

    int             total_len = 0, already_read_len = 0;
    int             parsed = sscanf(
        line, "+QIRD: %d,%d,%u", &total_len, &already_read_len, unread_length);

    if (parsed == 3)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static at_req_status_t
at_parser_socket_check_usability (at_request_t * at_req, char * line,
                                  uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret = AT_SUCCESS;
    all_socket_status_t * socket_state_handle
        = (all_socket_status_t *) at_req->p_data;

    uint32_t recv_socket_id         = 0;
    uint8_t  no_existing_connection = 0; /**< Number of existing connections */

    if (0 == strcmp(line, "OK"))
    {
        ret = AT_SUCCESS;
    }
    else
    {
        while (no_existing_connection < QUECTEL_MAXIMUM_SUPPORT_SOCKET)
        {
            int parsed = sscanf(line, "+QISTATE: %u", &recv_socket_id);

            if (1 == parsed)
            {
                if (recv_socket_id < MAXIMUM_SUPPORT_SOCKET)
                {
                    socket_state_handle->sockets[recv_socket_id] = true;
                }
                no_existing_connection++;
                ret = seek_one_line_and_parse_final_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                if (ret == AT_SUCCESS)
                {
                    break;
                }
            }
            else
            {
                ret = seek_and_parse_error_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                break;
            }
        }
    }

    return ret;
}

/**
 * @brief Sends socket data and waiting for send data prefix (>)
 *
 * @param[in] at_req Pointer to the AT command request structure containing
 *                   the command and associated parameters.
 * @param[in] timeout_ms Timeout in milliseconds to wait for the response.
 *
 * @return Returns a status of type `at_req_status_t` indicating the result
 *         of the operation.
 */
static at_req_status_t
send_socket_data_wait_prefix (at_request_t * at_req, uint32_t timeout)
{
    at_req_status_t ret        = AT_TIMEOUT;
    uint64_t        start_time = 0;

    cell_com_send_buffer(at_req->p_send_buff, at_req->send_buff_len);

    start_time                         = cell_os_get_time_ms();

    char     byte_read                 = 0;
    char     line[AT_MAX_LINE_LEN + 1] = {0};
    uint16_t idx                       = 0;
    while (cell_os_get_time_ms() - start_time < timeout)
    {
        int32_t num_byte_read
            = cell_com_read_last_bytes(&byte_read, 1, false, '\0');

        if (num_byte_read > 0)
        {
            if ('\n' != byte_read)
            {
                // If received send data prefix, break the loop
                if ('>' == byte_read)
                {
                    ret = AT_SUCCESS;
                    break;
                }
                else
                {
                    // If not received data prefix, put data into buffer
                    if (idx < AT_MAX_LINE_LEN - 1)
                    {
                        line[idx++] = byte_read;
                    }
                    else
                    {
                        // Reset line buffer index to prevent overflow
                        idx = 0;
                    }
                }
            }
            else
            {
                // If recevied \n, indicating an URC response line received
                // before data prefix (>), do parsing URC here
                line[idx]     = byte_read;
                line[idx + 1] = '\0';
                if (trim_and_validate_line(line))
                {
                    cell_log_debug("<== ...RX(%d): %s", strlen(line), line);
                    urc_parse_unsolicited_response(line);
                }
                idx = 0; // Reset index after processing line
            }
        }
        else
        {
            cell_os_sleep_ms(QUECTEL_CMD_RESPONSE_POLL_INTERVAL_MS);
        }
    }

    return ret;
}

/**
 * @brief Parses and receives socket data from an AT command response.
 *
 * This function processes the response line from an AT command related to
 * socket data reception. It uses the service type to determine how to parse the
 * response and attempts to read the expected amount of data from the
 * communication buffer within the specified timeout.
 */
static at_req_status_t
at_parser_socket_receive (at_request_t * at_req, char * line,
                          uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t           ret = AT_SUCCESS;
    socket_data_recv_info_t * recv_info
        = (socket_data_recv_info_t *) at_req->p_data;
    int  read_actual_length = 0;
    bool parsed_result
        = parse_socket_recv_info(recv_info, line, &read_actual_length);

    if (parsed_result)
    {
        int32_t byte_to_read = *recv_info->data_length;
        if (read_actual_length < byte_to_read)
        {
            // Only read the maximum possible bytes
            byte_to_read = read_actual_length;
        }

        /* TIS: to save analysis recources, keep only a single state after this
         * point where number of bytes to read is maximum, which is equivalent
         * to the size of socket data buffer initialized in
         * tis_commands_bg95.c:109 */
        sqc_assert((byte_to_read == 256));

        /* Loop to read the expected amount of data (byte_to_read) from the
         * communication buffer within the specified timeout. */
        int32_t total_byte_read = 0;
        while ((byte_to_read) && (total_byte_read < byte_to_read)
               && (cell_os_get_time_ms() - start_time < timeout_ms))
        {
#ifdef __TRUSTINSOFT_ANALYZER__
            // Force spliting states to avoid false alarms
            tis_variable_split(
                &total_byte_read, sizeof(total_byte_read), 10000);
#endif
            total_byte_read += cell_com_read_last_bytes(
                (char *) recv_info->data + total_byte_read,
                byte_to_read - total_byte_read,
                false,
                '\0');
        }

        if (total_byte_read >= byte_to_read)
        {
            *recv_info->data_length = total_byte_read;
            ret                     = seek_one_line_and_parse_final_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
        }
        else
        {
            ret = AT_TIMEOUT;
        }
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static void
build_socket_send_command (cell_socket_t * socket, char * cmd_buf,
                           uint16_t cmd_len, size_t send_len)
{
    if ((socket->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT)
        || (socket->service_type == CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT))

    {
        (void) snprintf(
            cmd_buf, cmd_len, "AT+QISEND=%u,%u\r", socket->socket_id, send_len);
    }
    else if (socket->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_SERVER)
    {
        (void) snprintf(cmd_buf,
                        cmd_len,
                        "AT+QISEND=%u,%u\r",
                        socket->incoming_id,
                        send_len);
    }
    else
    {
        char ip_address[CELL_IP_ADDRESS_MAX_SIZE + 1] = {0};
        helper_ip_to_str(socket->remote, ip_address);
        (void) snprintf(cmd_buf,
                        cmd_len,
                        "AT+QISEND=%u,%u,\"%s\",%d\r",
                        socket->socket_id,
                        send_len,
                        ip_address,
                        socket->remote.port);
    }
}

static void
build_socket_recv_command (cell_socket_t * socket, char * cmd_buf,
                           uint16_t cmd_len, uint16_t recv_len)
{
    if ((socket->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT)
        || (socket->service_type == CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT))

    {
        (void) snprintf(
            cmd_buf, cmd_len, "AT+QIRD=%u,%d\r", socket->socket_id, recv_len);
    }
    else if (socket->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_SERVER)
    {
        (void) snprintf(
            cmd_buf, cmd_len, "AT+QIRD=%u,%d\r", socket->incoming_id, recv_len);
    }
    else
    {
        (void) snprintf(cmd_buf, cmd_len, "AT+QIRD=%u\r", socket->socket_id);
    }
}

/**
 * @brief This function parses to get socket received information based on the
 * service type. The information including:
 * - Actual length data read from modem
 * - The remote IP and port in case receving data from UDP service socket, the
 * remote IP and port will be saved in socket info for socket send command
 */
static bool
parse_socket_recv_info (socket_data_recv_info_t * recv_info, char * line,
                        int * read_actual_len)
{
    bool parsed_result                           = false;
    char remote_ip[CELL_IP_ADDRESS_MAX_SIZE + 1] = {'\0'};

    if ((recv_info->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT)
        || (recv_info->service_type == CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT)
        || (recv_info->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_SERVER))
    {
        int parsed = sscanf(line, "+QIRD: %d", read_actual_len);

        if (1 == parsed)
        {
            parsed_result = true;
        }
    }
    else
    {
        // CELL_IP_ADDRESS_MAX_SIZE = 39, Note: update parsing format (%39) if
        // this macro value changes
        int parsed = sscanf(line,
                            "+QIRD: %d,\"%39[^\"]\",%hu",
                            read_actual_len,
                            remote_ip,
                            &recv_info->remote->port);

        if (3 == parsed)
        {
            if (helper_str_to_ip(recv_info->remote, remote_ip))
            {
                parsed_result = true;
            }
        }
    }

    return parsed_result;
}
