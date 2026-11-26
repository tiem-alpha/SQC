#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_api.c"
/**
 * @file cell_api.c
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
 * @brief Common module cellular APIs source file
 * @date 2025-02-28
 */
#include "cell_api.h"
#include "cell_internal.h"
#include "cell_task.h"
#include "cell_log.h"
#include "cell_connect.h"
#include "cell_monitoring.h"
#include "cell_dfota.h"
#include "cell_test.h"
#include "cell_com.h"
#include "cell_os.h"
#include "cell_socket.h"

cell_err_t
cell_init (char * buffer, int32_t size)
{
    return cell_com_init(buffer, size);
}

void
cell_deinit (void)
{
}

cell_err_t
cell_connect (cell_context_t * const context, cell_settings_t * cell_settings)
{
    connection_notify_action(context, cell_settings);
    return CELL_SUCCESS;
}

cell_err_t
cell_context_create (cell_context_settings_t * context_settings,
                     cell_context_t *          cellular_context)
{
    if ((context_settings == NULL) || (cellular_context == NULL))
    {
        return CELL_ERR_INVALID_PARAMS;
    }

    if (context_settings->create_cb == NULL)
    {
        return CELL_ERR_NO_CALLBACK;
    }

    send_msg_with_data(MESSAGE_CONTEXT_CREATION,
                       0,
                       context_settings,
                       sizeof(cell_context_settings_t),
                       cellular_context,
                       false);
    return CELL_SUCCESS;
}

cell_err_t
cell_PDP_config (cell_context_t * const      context,
                 const cell_pdp_settings_t * pdp_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_DNS_config (cell_context_t * const      context,
                 const cell_dns_settings_t * dns_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_simcard_info_get (cell_context_t * const context,
                       cell_simcard_info_t *  simcard_info)
{
    monitoring_notify_get_simcard_info(context, simcard_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_simcard_swap_start (cell_context_t * const               context,
                         const cell_simcard_swap_settings_t * swap_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_simcard_swap_abort (cell_context_t * const context)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_modem_info_get (cell_context_t * const context,
                     cell_modem_info_t *    modem_info)
{
    monitoring_notify_get_modem_info(context, modem_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_cellular_info_get (cell_context_t * const context,
                        cell_cellular_info_t * cellular_info)
{
    monitoring_notify_get_cellular_info(context, cellular_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_service_status_get (cell_context_t * const  context,
                         cell_service_status_t * service_status)
{
    monitoring_notify_get_service_status(context, service_status);
    return CELL_SUCCESS;
}

cell_err_t
cell_network_info_get (cell_context_t * const context,
                       cell_network_info_t *  network_info)
{
    monitoring_notify_get_network_info(context, network_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_reject_cause_get (cell_context_t * const context,
                       cell_reject_cause_t *  reject_cause)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_psm_config (cell_context_t * const      context,
                 const cell_psm_settings_t * psm_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_psm_settings_get (cell_context_t * const context,
                       cell_psm_settings_t *  psm_settings)
{
    monitoring_notify_get_psm_setting(context, psm_settings);
    return CELL_SUCCESS;
}

cell_err_t
cell_edrx_config (cell_context_t * const       context,
                  const cell_edrx_settings_t * edrx_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_edrx_settings_get (cell_context_t * const context,
                        cell_edrx_settings_t * edrx_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_ping_answer_config (
    cell_context_t * const              context,
    const cell_ping_answer_settings_t * ping_answer_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_socket_create (cell_socket_settings_t * socket_settings,
                    cell_socket_t *          socket)
{
    socket_notify_create(socket_settings, socket);
    return CELL_SUCCESS;
}

cell_err_t
cell_socket_send (cell_socket_t * socket, cell_socket_data_info_t * data_info)
{
    socket_notify_send(socket, data_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_socket_receive (cell_socket_t *           socket,
                     cell_socket_data_info_t * data_info)
{
    socket_notify_recv(socket, data_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_socket_close (cell_socket_t * socket)
{
    socket_notify_close(socket);
    return CELL_SUCCESS;
}

cell_err_t
cell_socket_verify_data (cell_socket_t *                  socket,
                         cell_socket_data_verify_info_t * data_verify_info)
{
    socket_notify_verify_data(socket, data_verify_info);
    return CELL_SUCCESS;
}

cell_err_t
cell_time_sync (cell_context_t * const context,
                cell_time_settings_t * time_settings)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_time_get (cell_context_t * const context, cell_time_info_t * time)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_dfota_start (cell_context_t * const        context,
                  const cell_dfota_settings_t * dfota_settings)
{
    dfota_notify_start(context, dfota_settings);
    return CELL_SUCCESS;
}

cell_err_t
cell_dfota_write (cell_context_t * const     context,
                  const cell_dfota_block_t * p_block)
{
    dfota_notify_write(context, p_block);
    return CELL_SUCCESS;
}

cell_err_t
cell_dfota_activate (cell_context_t * const context)
{
    dfota_notify_activate(context);
    return CELL_SUCCESS;
}

cell_err_t
cell_dfota_abort (cell_context_t * const context)
{
    dfota_notify_cancel(context);
    return CELL_SUCCESS;
}

cell_err_t
cell_log_config (const cell_log_settings_t * log_settings)
{
    cell_log_set_level(log_settings->level);
    return CELL_SUCCESS;
}

cell_err_t
cell_tcp_config (cell_context_t * const    context,
                 const cell_tcp_config_t * tcp_config)
{
    return CELL_SUCCESS;
}

cell_err_t
cell_test (cell_context_t * const context, const cell_test_request_t * request,
           cell_test_response_t * response)
{
    cell_test_notify_custom_at(context, request, response);
    return CELL_SUCCESS;
}
