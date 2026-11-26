#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/benchmarks/cmcell_features/sqc_features.c"
/**
 * @file main_features.c
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
 * @author Dev : NGUYEN HUY Hai
 * @brief TIS test driver for common module main features
 * @date 2025-04-18
 */
#include "cell_internal.h"
#include "cell_api.h"
#include "cell_com.h"
#include "cell_log.h"
#include "cell_modem_common.h"
#include "cell_modem_quectel.h"
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

#define URC_DATA_MAXIMUM_SIZE 64

struct cell_simcard_swap_settings
{
};

struct cell_ping_answer_settings
{
};

/**
 * @brief Analysis cases to select which "features" to be analyzed
 */
typedef enum
{
    ANALYSIS_CASE_INVALID = 0,
    ANALYSIS_CASE_CONNECT,
    ANALYSIS_CASE_GET_SIMCARD_INFO,
    ANALYSIS_CASE_START_SIM_SWAP,
    ANALYSIS_CASE_ABORT_SIM_SWAP,
    ANALYSIS_CASE_GET_MODEM_INFO,
    ANALYSIS_CASE_GET_CELLULAR_INFO,
    ANALYSIS_CASE_GET_SERVICE_STATUS,
    ANALYSIS_CASE_GET_NETWORK_INFO,
    ANALYSIS_CASE_GET_REJECT_CAUSE,
    ANALYSIS_CASE_CONFIG_PSM,
    ANALYSIS_CASE_GET_PSM_SETTINGS,
    ANALYSIS_CASE_CONFIG_EDRX,
    ANALYSIS_CASE_GET_EDRX_SETTINGS,
    ANALYSIS_CASE_CONFIG_PING_ANSWER,
    ANALYSIS_CASE_SOCKET,
    ANALYSIS_CASE_SYNC_TIME,
    ANALYSIS_CASE_GET_TIME,
    ANALYSIS_CASE_START_DFOTA,
    ANALYSIS_CASE_WRITE_DFOTA,
    ANALYSIS_CASE_ABORT_DFOTA,
    ANALYSIS_CASE_ACTIVATE_DFOTA,
    ANALYSIS_CASE_CONFIG_LOG,
    ANALYSIS_CASE_CONFIG_TCP,
    ANALYSIS_CASE_TEST_CELL,
    ANALYSIS_CASE_CONFIG_PDP,
    ANALYSIS_CASE_CONFIG_DNS,
    ANALYSIS_CASE_INCOMING_DATA,

    ANALYSIS_CASE_MAXIMUM,
} analysis_case_t;

typedef enum
{
    MSG_CASE_INVALID = 0,
    MSG_CASE_CONTEXT_CREATION,
    MSG_CASE_CONNECT_ESTABLISH,
    MSG_CASE_CONNECT_URC,
    MSG_CASE_CONNECT_TIMER_EXPIRED,
    MSG_CASE_DATA_INCOMING,
    MSG_CASE_MONITORING_SIMCARD_INFO,
    MSG_CASE_MONITORING_MODEM_INFO,
    MSG_CASE_MONITORING_PSM_SETTING,
    MSG_CASE_MONITORING_CELLULAR_INFO,
    MSG_CASE_MONITORING_SERVICE_STATUS,
    MSG_CASE_MONITORING_NETWORK_INFO,
    MSG_CASE_DFOTA_INIT_ACTION,
    MSG_CASE_DFOTA_INIT_TIMER,
    MSG_CASE_DFOTA_WRITE,
    MSG_CASE_DFOTA_ACTIVATE,
    MSG_CASE_DFOTA_CANCEL,
    MSG_CASE_DFOTA_EVENT_URC,
    MSG_CASE_SOCKET_CREATE,
    MSG_CASE_SOCKET_SEND,
    MSG_CASE_SOCKET_READ,
    MSG_CASE_SOCKET_CLOSE,
    MSG_CASE_SOCKET_VERIFY_DATA,
    MSG_CASE_SOCKET_REMOTE_CLOSE,
    MSG_CASE_CUSTOM_AT_CMD,
    MSG_CASE_MAXIMUM,
} msg_case_t;

struct cell_os_queue_t
{
    uint32_t dummy;
} task_queue = {0};

static at_req_status_t specific_power_off_software(uint16_t * error_code);
static at_req_status_t specific_iccid_get(char *     p_iccid,
                                          uint16_t * error_code);
static at_req_status_t specific_modem_specific_features_set(
    uint16_t * error_code);
static at_req_status_t specific_pdp_settings_get(
    cell_pdp_settings_t * pdp_setttings, uint16_t * error_code);
static at_req_status_t specific_pdp_config(cell_pdp_settings_t * pdp_setttings,
                                           uint16_t *            error_code);
static at_req_status_t specific_operator_info_get(
    cell_operator_info_t * operator_info, uint16_t * error_code);
static at_req_status_t specific_operator_config(
    cell_operator_settings_t * operator_settings, uint16_t * error_code);
static at_req_status_t specific_rat_settings_get(
    cell_rat_settings_t * rat_settings, uint16_t * error_code);
static at_req_status_t specific_rat_config(cell_rat_settings_t * rat_settings,
                                           bool *     is_trigger_reboot,
                                           uint16_t * error_code);
static at_req_status_t specific_service_domain_settings_get(
    cell_service_domain_t * service_domain, uint16_t * error_code);
static at_req_status_t specific_service_domain_config(
    cell_service_domain_t * service_domain, uint16_t * error_code);
static at_req_status_t specific_pdp_context_activate(uint8_t    context_id,
                                                     uint16_t * error_code);
static at_req_status_t specific_pdp_context_deactivate(uint8_t    context_id,
                                                       uint16_t * error_code);
static at_req_status_t specific_pdp_status_get(cell_network_info_t * p_net_info,
                                               uint16_t * error_code);
static at_req_status_t specific_socket_create(cell_socket_settings_t * socket,
                                              uint32_t   socket_id,
                                              uint16_t * err_code);
static at_req_status_t specific_socket_send(cell_socket_t * socket,
                                            const uint8_t * data,
                                            size_t *        length,
                                            uint16_t *      error_code);
static at_req_status_t specific_socket_sent_info_get(
    cell_socket_t * socket, cell_socket_data_verify_info_t * data_verify_info,
    uint16_t * error_code);
static at_req_status_t specific_socket_receive(cell_socket_t * socket,
                                               const uint8_t * data,
                                               size_t *        data_length,
                                               uint16_t *      error_code);
static at_req_status_t specific_socket_close(uint32_t connect_id,
                                             cell_socket_service_type_t type,
                                             uint16_t * error_code);
static at_req_status_t specific_socket_query_service_status(
    cell_socket_t * socket, uint16_t * error_code);
static at_req_status_t specific_socket_check_usability(
    all_socket_status_t * socket_state_handle, uint8_t context_id,
    uint16_t * error_code);
static at_req_status_t specific_firmware_version_get(char *     p_fw_ver,
                                                     uint16_t * error_code);
static at_req_status_t specific_cellular_info_get(
    cell_cellular_info_t * cellular_info, uint16_t * error_code);
static at_req_status_t specific_dfota_local_init(
    cell_dfota_settings_t * settings, uint16_t * error_code);
static at_req_status_t specific_dfota_wireless_init(
    cell_dfota_settings_t * settings, uint16_t * error_code);
static at_req_status_t bg95_dfota_local_update(void *     arg,
                                               uint16_t * error_code);
static at_req_status_t specific_dfota_wireless_start(
    cell_dfota_settings_t * settings, uint16_t * error_code);
static at_req_status_t specific_dfota_write(cell_dfota_block_t * data,
                                            uint16_t *           error_code);
static at_req_status_t specific_dfota_download_cancel(void *     arg,
                                                      uint16_t * error_code);
static at_req_status_t specific_firmware_version_get(char *     p_fw_ver,
                                                     uint16_t * error_code);
static at_req_status_t specific_enable_functionality(uint16_t * error_code);
static at_req_status_t specific_sim_readiness_get(
    cell_sim_lock_status_t * status, uint16_t * error_code);

static at_req_status_t specific_sim_pin_enter(
    cell_simcard_settings_t sim_settings, bool * is_sim_ready_requires_urc,
    uint16_t * error_code);
static uint8_t g_tis_task_handle_storage;
#define TIMERS_MAXIMUM (8)
static uint32_t timers_count;
static void (*timer_callbacks[TIMERS_MAXIMUM])(void * arg)    = {NULL};
static uint32_t                       timers_count            = 0;

static cell_context_settings_t        context_settings        = {0};
static cell_context_t                 cellular_context        = {0};
static cell_settings_t                cell_settings           = {0};
static cell_modem_info_t              modem_info              = {0};
static cell_simcard_info_t            simcard_info            = {0};
static cell_psm_settings_t            psm_settings            = {0};
static cell_cellular_info_t           cellular_info           = {0};
static cell_service_status_t          service_status          = {0};
static cell_network_info_t            network_info            = {0};

static cell_dfota_settings_t          dfota_settings          = {0};
static cell_dfota_block_t             data_block              = {0};
static uint8_t                        dfota_data[1024]        = {0};

static cell_socket_settings_t         socket_settings         = {0};
static cell_socket_t                  socket_context          = {0};
static cell_socket_data_info_t        socket_data_info        = {0};
static cell_socket_data_verify_info_t socket_data_verify_info = {0};

static cell_test_request_t            test_request            = {0};
static cell_test_response_t           test_response           = {0};
modem_driver_t *                      gp_modem_driver         = NULL;

extern modem_driver_t                 bg95_driver;
extern modem_driver_t                 eg915u_driver;
extern modem_driver_t                 semtech_hl7900_driver;

/* Mock driver 1: Quectel BG95 (full DFOTA, socket verify/query, RAT priority = true) */
static modem_driver_t g_mock_bg95_driver = {
    .urc_handler                 = NULL,
    .map_cme_error               = NULL,
    .map_cms_error               = NULL,
    .power_off_software          = specific_power_off_software,
    .iccid_get                   = specific_iccid_get,
    .modem_specific_features_set = specific_modem_specific_features_set,
    .pdp_settings_get            = specific_pdp_settings_get,
    .sim_readiness_get           = specific_sim_readiness_get,
    .sim_pin_enter               = specific_sim_pin_enter,
    .pdp_config                  = specific_pdp_config,
    .operator_info_get           = specific_operator_info_get,
    .operator_config             = specific_operator_config,
    .rat_settings_get            = specific_rat_settings_get,
    .rat_config                  = specific_rat_config,
    .service_domain_settings_get = specific_service_domain_settings_get,
    .service_domain_config       = specific_service_domain_config,
    .pdp_context_activate        = specific_pdp_context_activate,
    .pdp_context_deactivate      = specific_pdp_context_deactivate,
    .pdp_status_get              = specific_pdp_status_get,
    .firmware_version_get        = specific_firmware_version_get,
    .cellular_info_get           = specific_cellular_info_get,
    .dfota_local_init            = specific_dfota_local_init,
    .dfota_wireless_init         = specific_dfota_wireless_init,
    .dfota_local_update          = bg95_dfota_local_update,
    .dfota_wireless_start        = specific_dfota_wireless_start,
    .dfota_write                 = specific_dfota_write,
    .dfota_download_cancel       = specific_dfota_download_cancel,
    .socket_create               = specific_socket_create,
    .socket_send                 = specific_socket_send,
    .socket_sent_info_get        = specific_socket_sent_info_get,
    .socket_receive              = specific_socket_receive,
    .socket_close                = specific_socket_close,
    .socket_query_service_status = specific_socket_query_service_status,
    .socket_check_usability      = specific_socket_check_usability,
    .rat_priority_supported      = true,
};

/* Mock driver 2: Quectel EG915U (no DFOTA callbacks, RAT priority = false) */
static modem_driver_t g_mock_eg915u_driver = {
    .urc_handler                 = NULL,
    .map_cme_error               = NULL,
    .map_cms_error               = NULL,
    .power_off_software          = specific_power_off_software,
    .iccid_get                   = specific_iccid_get,
    .modem_specific_features_set = specific_modem_specific_features_set,
    .pdp_settings_get            = specific_pdp_settings_get,
    .sim_readiness_get           = specific_sim_readiness_get,
    .sim_pin_enter               = specific_sim_pin_enter,
    .pdp_config                  = specific_pdp_config,
    .operator_info_get           = specific_operator_info_get,
    .operator_config             = specific_operator_config,
    .rat_settings_get            = specific_rat_settings_get,
    .rat_config                  = specific_rat_config,
    .service_domain_settings_get = specific_service_domain_settings_get,
    .service_domain_config       = specific_service_domain_config,
    .pdp_context_activate        = specific_pdp_context_activate,
    .pdp_context_deactivate      = specific_pdp_context_deactivate,
    .pdp_status_get              = specific_pdp_status_get,
    .firmware_version_get        = specific_firmware_version_get,
    .cellular_info_get           = specific_cellular_info_get,
    .dfota_local_init            = NULL,
    .dfota_wireless_init         = NULL,
    .dfota_local_update          = NULL,
    .dfota_wireless_start        = NULL,
    .dfota_write                 = NULL,
    .dfota_download_cancel       = NULL,
    .socket_create               = specific_socket_create,
    .socket_send                 = specific_socket_send,
    .socket_sent_info_get        = specific_socket_sent_info_get,
    .socket_receive              = specific_socket_receive,
    .socket_close                = specific_socket_close,
    .socket_query_service_status = specific_socket_query_service_status,
    .socket_check_usability      = specific_socket_check_usability,
    .rat_priority_supported      = false,
};

/* Mock driver 3: Semtech HL7900 (no DFOTA, no socket_sent_info_get/query, RAT priority = false) */
static modem_driver_t g_mock_hl7900_driver = {
    .urc_handler                 = NULL,
    .map_cme_error               = NULL,
    .map_cms_error               = NULL,
    .power_off_software          = specific_power_off_software,
    .iccid_get                   = specific_iccid_get,
    .modem_specific_features_set = specific_modem_specific_features_set,
    .pdp_settings_get            = specific_pdp_settings_get,
    .sim_readiness_get           = specific_sim_readiness_get,
    .sim_pin_enter               = specific_sim_pin_enter,
    .pdp_config                  = specific_pdp_config,
    .operator_info_get           = specific_operator_info_get,
    .operator_config             = specific_operator_config,
    .rat_settings_get            = specific_rat_settings_get,
    .rat_config                  = specific_rat_config,
    .service_domain_settings_get = specific_service_domain_settings_get,
    .service_domain_config       = specific_service_domain_config,
    .pdp_context_activate        = specific_pdp_context_activate,
    .pdp_context_deactivate      = specific_pdp_context_deactivate,
    .pdp_status_get              = specific_pdp_status_get,
    .firmware_version_get        = specific_firmware_version_get,
    .cellular_info_get           = specific_cellular_info_get,
    .dfota_local_init            = NULL,
    .dfota_wireless_init         = NULL,
    .dfota_local_update          = NULL,
    .dfota_wireless_start        = NULL,
    .dfota_write                 = NULL,
    .dfota_download_cancel       = NULL,
    .socket_create               = specific_socket_create,
    .socket_send                 = specific_socket_send,
    .socket_sent_info_get        = NULL,
    .socket_receive              = specific_socket_receive,
    .socket_close                = specific_socket_close,
    .socket_query_service_status = NULL,
    .socket_check_usability      = specific_socket_check_usability,
    .rat_priority_supported      = false,
};

static void
cell_socket_event_handler (cell_socket_event_t event)
{
}

static void
cell_modem_event_handler (cell_modem_event_t event)
{
}

static void
cell_get_event_handler (cell_get_event_t event, bool result)
{
}

static void
cell_dfota_handler (cell_dfota_event_t event)
{
}

static void
cell_test_event_handler (cell_test_event_t event)
{
}

static void
cell_context_event_handler (cell_context_event_t result)
{
    (void) result;
}

cell_os_err_t
cell_os_timer_create (uint32_t * timer_id, void (*callback)(void * arg))
{
    cell_os_err_t ret = sqc_interval(CELL_OS_SUCCESS, CELL_OS_ERR_UNKNOWN);

    if (CELL_OS_SUCCESS == ret)
    {
        if (timers_count >= TIMERS_MAXIMUM)
        {
            timers_count = 0;
        }

        timer_callbacks[timers_count] = callback;
        *timer_id                     = timers_count;
        timers_count++;
    }

    return ret;
}

cell_os_err_t
cell_os_timer_start (uint32_t timer_id, uint32_t value)
{
    (void) value;

    if ((timer_id < timers_count) && (timer_id < TIMERS_MAXIMUM))
    {
        void (*callback)(void * arg) = timer_callbacks[timer_id];

        if (NULL != callback)
        {
            callback(NULL);
        }
    }

    return sqc_interval(CELL_OS_SUCCESS, CELL_OS_ERR_UNKNOWN);
}

cell_os_queue_handle_t
cell_os_msg_create (uint32_t length, uint32_t msg_size)
{
    cell_os_queue_handle_t queue = sqc_nondet(&task_queue, NULL);
    return sqc_nondet_ptr(queue, NULL);
}

/*@ requires (data != NULL) && (size == sizeof(message_t)); @*/
cell_os_err_t
cell_os_msg_write (cell_os_queue_handle_t queue_handle, const void * data,
                   uint16_t size, int32_t timeout)
{
    return CELL_OS_SUCCESS;
}

/*@ requires (data != NULL) && (size == sizeof(message_t)); @*/
cell_os_err_t
cell_os_msg_write_from_isr (cell_os_queue_handle_t queue_handle,
                            const void * data, uint16_t size)
{
    return CELL_OS_SUCCESS;
}

static void
copy_msg_common_fields (message_t * msg_out, message_type_t msg_type,
                        uint8_t sub_type, uint16_t size, void * ctx)
{
    msg_out->type     = msg_type;
    msg_out->sub_type = sub_type;
    msg_out->size     = size;
    msg_out->context  = ctx;
}

static void
copy_msg_with_data (message_t * msg_out, message_type_t msg_type,
                    uint8_t sub_type, void * data, uint16_t size, void * ctx,
                    bool is_in_isr)
{
    copy_msg_common_fields(msg_out, msg_type, sub_type, size, ctx);
    msg_out->data.ptr = data;
}

static void
copy_msg_with_value (message_t * msg_out, message_type_t msg_type,
                     uint8_t sub_type, uint32_t value, void * ctx,
                     bool is_in_isr)
{
    copy_msg_common_fields(msg_out, msg_type, sub_type, 0, ctx);
    msg_out->data.value = value;
}

/**
 * @brief Connection Events (stubbed from enum `event_t` in cell_connect.c)
 */
typedef enum
{
    CONNECT_EVENT_INVALID = 0,   /** Invalid event */
    CONNECT_EVENT_CONNECT,       /** Connection procedure is requested */
    CONNECT_EVENT_URC,           /** Received related URC */
    CONNECT_EVENT_TIMER_EXPIRED, /** Timer expired */
} connect_event_t;

/**
 * @brief Monitoring Events (stubbed from enum `monitoring_event_t` in
 * cell_monitoring.c)
 */
typedef enum
{
    MONITORING_EVENT_SIMCARD_INFO = 0, /** Get simcard info event */
    MONITORING_EVENT_MODEM_INFO,       /** Get modem info event */
    MONITORING_EVENT_PSM_SETTING,      /** Get psm settings event */
    MONITORING_EVENT_CELLULAR_INFO,    /** Get cellulr info event */
    MONITORING_EVENT_SERVICE_STATUS,   /** Get service status event */
    MONITORING_EVENT_NETWORK_INFO      /** Get network info event */
} monitoring_event_t;

/**
 * @brief DFOTA events
 *
 */
typedef enum
{
    DFOTA_EVENT_INVALID = 0, /**< Invalid event */
    DFOTA_EVENT_START,       /**< Request to start the dfota procedure */
    DFOTA_EVENT_WRITE,       /**< Request to write a data block */
    DFOTA_EVENT_ACTIVATE,
    DFOTA_EVENT_DOWNLOAD_CANCEL, /**< Cancel the current DFOTA package download
                                  */
    DFOTA_EVENT_URC,             /**< Received related URC */
    DFOTA_EVENT_TIMER_EXPIRED    /**< Timer expired */
} dfota_event_t;

/**
 * @brief Socket Events (stubbed from enum `event_t` in cell_socket.c)
 */
typedef enum
{
    SOCKET_EVENT_CREATE = 0,   /** Create and open socket request */
    SOCKET_EVENT_SEND,         /** Socket send data request */
    SOCKET_EVENT_READ,         /** Socket read data request */
    SOCKET_EVENT_CLOSE,        /** Close socket request */
    SOCKET_EVENT_VERIFY_DATA,  /** Verify socket data sent request */
    SOCKET_EVENT_REMOTE_CLOSE, /** Close remote socket */
} socket_event_t;

cell_os_err_t
cell_os_msg_read (cell_os_queue_handle_t queue_handle, void * data,
                  int32_t timeout)
{
#if defined(SQC_UNIT_CONNECT)
    msg_case_t mcase = sqc_interval(MSG_CASE_CONNECT_ESTABLISH, MSG_CASE_CONNECT_TIMER_EXPIRED);
#elif defined(SQC_UNIT_MONITORING)
    msg_case_t mcase = sqc_interval(MSG_CASE_MONITORING_SIMCARD_INFO, MSG_CASE_MONITORING_NETWORK_INFO);
#elif defined(SQC_UNIT_DFOTA)
    msg_case_t mcase = sqc_interval(MSG_CASE_DFOTA_INIT_ACTION, MSG_CASE_DFOTA_EVENT_URC);
#elif defined(SQC_UNIT_SOCKET)
    msg_case_t mcase = sqc_interval(MSG_CASE_SOCKET_CREATE, MSG_CASE_SOCKET_REMOTE_CLOSE);
#elif defined(SQC_UNIT_APIS)
    msg_case_t mcase = sqc_nondet(
        sqc_nondet(MSG_CASE_CONTEXT_CREATION, MSG_CASE_DATA_INCOMING),
        sqc_nondet(MSG_CASE_CUSTOM_AT_CMD, MSG_CASE_MAXIMUM));
#else
    msg_case_t mcase = sqc_interval(MSG_CASE_INVALID, MSG_CASE_MAXIMUM);
#endif
    switch (mcase)
    {
        case MSG_CASE_CONTEXT_CREATION:
        {
            context_settings.create_cb
                = sqc_nondet_ptr(cell_context_event_handler, NULL);

            copy_msg_with_data(data,
                               MESSAGE_CONTEXT_CREATION,
                               0,
                               sqc_nondet_ptr(&context_settings, NULL),
                               sqc_nondet(0, sizeof(cell_context_settings_t)),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_DATA_INCOMING:
        {
            copy_msg_with_data(
                data, MESSAGE_DATA_INCOMING, 0, NULL, 0, NULL, true);
        }
        break;

        case MSG_CASE_CONNECT_ESTABLISH:
        {
            sqc_make_unknown(&cell_settings, sizeof(cell_settings_t));
            cell_settings.valid = sqc_nondet(true, false);
            cell_settings.custom_at_list.enable = sqc_nondet(true, false);
            cell_settings.pdp.apn_name[CELL_APN_MAX_SIZE] = '\0';
            copy_msg_with_data(data,
                               MESSAGE_CONNECT,
                               CONNECT_EVENT_CONNECT,
                               sqc_nondet_ptr(&cell_settings, NULL),
                               sizeof(cell_settings_t),
                               &cellular_context,
                               false);
        }
        break;

        case MSG_CASE_CONNECT_URC:
        {
            copy_msg_with_value(data,
                                MESSAGE_CONNECT,
                                CONNECT_EVENT_URC,
                                sqc_interval(0, 10),
                                NULL,
                                false);
        }
        break;

        case MSG_CASE_CONNECT_TIMER_EXPIRED:
        {
            copy_msg_with_value(data,
                                MESSAGE_CONNECT,
                                CONNECT_EVENT_TIMER_EXPIRED,
                                sqc_interval(0, 10),
                                NULL,
                                false);
        }
        break;

        case MSG_CASE_MONITORING_SIMCARD_INFO:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_SIMCARD_INFO,
                               sqc_nondet_ptr(&simcard_info, NULL),
                               sizeof(cell_simcard_info_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_MONITORING_MODEM_INFO:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_MODEM_INFO,
                               sqc_nondet_ptr(&modem_info, NULL),
                               sizeof(cell_modem_info_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_MONITORING_PSM_SETTING:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_PSM_SETTING,
                               sqc_nondet_ptr(&psm_settings, NULL),
                               sizeof(cell_psm_settings_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_MONITORING_CELLULAR_INFO:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_CELLULAR_INFO,
                               sqc_nondet_ptr(&cellular_info, NULL),
                               sizeof(cell_cellular_info_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_MONITORING_SERVICE_STATUS:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_SERVICE_STATUS,
                               sqc_nondet_ptr(&service_status, NULL),
                               sizeof(cell_service_status_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_MONITORING_NETWORK_INFO:
        {
            copy_msg_with_data(data,
                               MESSAGE_MONITORING,
                               MONITORING_EVENT_NETWORK_INFO,
                               sqc_nondet_ptr(&network_info, NULL),
                               sizeof(cell_network_info_t),
                               sqc_nondet_ptr(&cellular_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_DFOTA_INIT_ACTION:
            copy_msg_with_data(data,
                               MESSAGE_DFOTA,
                               (uint8_t) DFOTA_EVENT_START,
                               sqc_nondet_ptr(&dfota_settings, NULL),
                               sizeof(dfota_settings),
                               &cellular_context,
                               false);
            break;

        case MSG_CASE_DFOTA_INIT_TIMER:
            copy_msg_with_value(data,
                                MESSAGE_DFOTA,
                                (uint8_t) DFOTA_EVENT_TIMER_EXPIRED,
                                0,
                                NULL,
                                false);
            break;

        case MSG_CASE_DFOTA_WRITE:
            copy_msg_with_data(data,
                               MESSAGE_DFOTA,
                               (uint8_t) DFOTA_EVENT_WRITE,
                               sqc_nondet_ptr(&data_block, NULL),
                               sizeof(cell_dfota_block_t),
                               &cellular_context,
                               false);
            break;

        case MSG_CASE_DFOTA_ACTIVATE:
            copy_msg_with_value(data,
                                MESSAGE_DFOTA,
                                (uint8_t) DFOTA_EVENT_ACTIVATE,
                                0,
                                &cellular_context,
                                false);
            break;

        case MSG_CASE_DFOTA_CANCEL:
            copy_msg_with_value(data,
                                MESSAGE_DFOTA,
                                (uint8_t) DFOTA_EVENT_DOWNLOAD_CANCEL,
                                0,
                                &cellular_context,
                                false);
            break;

        case MSG_CASE_DFOTA_EVENT_URC:
            copy_msg_with_value(data,
                                MESSAGE_DFOTA,
                                (uint8_t) DFOTA_EVENT_URC,
                                sqc_interval(INT_MIN, INT_MAX),
                                NULL,
                                false);
            break;

        case MSG_CASE_SOCKET_CREATE:
        {
            copy_msg_with_data(data,
                               MESSAGE_SOCKET,
                               SOCKET_EVENT_CREATE,
                               sqc_nondet_ptr(&socket_settings, NULL),
                               sqc_interval(0, sizeof(cell_socket_settings_t)),
                               sqc_nondet_ptr(&socket_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_SOCKET_SEND:
        {
            copy_msg_with_data(data,
                               MESSAGE_SOCKET,
                               SOCKET_EVENT_SEND,
                               sqc_nondet_ptr(&socket_data_info, NULL),
                               sizeof(cell_socket_data_info_t),
                               sqc_nondet_ptr(&socket_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_SOCKET_READ:
        {
            copy_msg_with_data(data,
                               MESSAGE_SOCKET,
                               SOCKET_EVENT_READ,
                               sqc_nondet_ptr(&socket_data_info, NULL),
                               sizeof(cell_socket_data_info_t),
                               sqc_nondet_ptr(&socket_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_SOCKET_CLOSE:
        {
            copy_msg_with_data(data,
                               MESSAGE_SOCKET,
                               SOCKET_EVENT_CLOSE,
                               NULL,
                               0,
                               sqc_nondet_ptr(&socket_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_SOCKET_VERIFY_DATA:
        {
            copy_msg_with_data(data,
                               MESSAGE_SOCKET,
                               SOCKET_EVENT_VERIFY_DATA,
                               NULL,
                               0,
                               sqc_nondet_ptr(&socket_context, NULL),
                               false);
        }
        break;

        case MSG_CASE_SOCKET_REMOTE_CLOSE:
        {
            copy_msg_with_value(data,
                                MESSAGE_SOCKET,
                                SOCKET_EVENT_REMOTE_CLOSE,
                                sqc_interval(0, MAXIMUM_SUPPORT_SOCKET - 1),
                                NULL,
                                false);
        }
        break;

        case MSG_CASE_CUSTOM_AT_CMD:
        {
            copy_msg_with_data(
                data,
                MESSAGE_CUSTOM_AT_CMD,
                sqc_interval(CELL_TEST_INVALID, CELL_TEST_CUSTOM_AT),
                sqc_nondet_ptr(&test_request, NULL),
                sqc_nondet(sizeof(cell_context_settings_t), 0),
                sqc_nondet_ptr(&cellular_context, NULL),
                false);
        }
        break;

        default:
            return CELL_OS_ERR_UNKNOWN;
    }

    return CELL_OS_SUCCESS;
}

modem_type_t
common_get_modem_type (cell_modem_info_t * modem_info)
{
    //@ assert UB: valid_buffer : \valid(modem_info);
    return sqc_interval(MODEM_TYPE_INVALID, MODEM_TYPE_MAXIMUM - 1);
}

modem_driver_t *
common_get_modem_driver (modem_type_t modem_type)
{
    g_mock_bg95_driver.urc_handler      = bg95_driver.urc_handler;
    g_mock_bg95_driver.map_cme_error    = bg95_driver.map_cme_error;
    g_mock_bg95_driver.map_cms_error    = bg95_driver.map_cms_error;

    g_mock_eg915u_driver.urc_handler    = eg915u_driver.urc_handler;
    g_mock_eg915u_driver.map_cme_error  = eg915u_driver.map_cme_error;
    g_mock_eg915u_driver.map_cms_error  = eg915u_driver.map_cms_error;

    g_mock_hl7900_driver.urc_handler    = semtech_hl7900_driver.urc_handler;
    g_mock_hl7900_driver.map_cme_error  = semtech_hl7900_driver.map_cme_error;
    g_mock_hl7900_driver.map_cms_error  = semtech_hl7900_driver.map_cms_error;

    switch (modem_type)
    {
        case MODEM_TYPE_BG95:
            return &g_mock_bg95_driver;
        case MODEM_TYPE_EG915U:
            return &g_mock_eg915u_driver;
        case MODEM_TYPE_HL7900:
            return &g_mock_hl7900_driver;
        default:
            return NULL;
    }
}

at_req_status_t
common_at_readiness_check (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_echo_turn_off (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_imei_get (char * p_imei, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_manufacturer_info_get (char * p_manufacturer_id, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_manufacturer_id, CELL_MANUFACTURE_ID_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_model_id_get (char * p_model_id, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_model_id, CELL_MODEL_ID_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_firmware_version_get (char * p_fw_ver, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_fw_ver, CELL_FW_VERSION_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_imsi_get (char * p_imsi, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_imsi, CELL_IMSI_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_psm_config_get (cell_psm_settings_t * p_psm, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_psm, sizeof(cell_psm_settings_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_psm_config_set (cell_psm_settings_t * p_psm_config,
                       uint16_t *            error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_ue_reset (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_ue_functionality_set (cell_ue_functionality_t func,
                             uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_ue_functionality_get (cell_ue_functionality_t * func,
                             uint16_t *                error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(func, sizeof(cell_ue_functionality_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_reporting_cme_error_enable (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_eps_network_registration_urc_enable (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_gprs_network_registration_urc_enable (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_signal_quality_report (cell_signal_info_t * signal,
                              uint16_t *           error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(signal, sizeof(cell_signal_info_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_cs_network_reg_status_get (cell_network_reg_status_t * reg_status,
                                  uint16_t *                  error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(reg_status, sizeof(cell_network_reg_status_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_eps_network_reg_status_get (cell_network_reg_status_t * reg_status,
                                   uint16_t *                  error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(reg_status, sizeof(cell_network_reg_status_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
common_gprs_network_reg_status_get (cell_network_reg_status_t * reg_status,
                                    uint16_t *                  error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(reg_status, sizeof(cell_network_reg_status_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_power_off_software (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_iccid_get (char * p_iccid, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_iccid, CELL_ICCID_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_modem_specific_features_set (uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

at_req_status_t
specific_pdp_settings_get (cell_pdp_settings_t * pdp_setttings,
                           uint16_t *            error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(pdp_setttings, sizeof(cell_pdp_settings_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_pdp_config (cell_pdp_settings_t * pdp_setttings, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_operator_info_get (cell_operator_info_t * operator_info,
                            uint16_t *             error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(operator_info, sizeof(cell_operator_info_t));
    operator_info->name[CELL_OPERATOR_NAME_MAX_SIZE] = '\0';
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_operator_config (cell_operator_settings_t * operator_settings,
                          uint16_t *                 error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_rat_settings_get (cell_rat_settings_t * rat_settings,
                           uint16_t *            error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(rat_settings, sizeof(cell_rat_settings_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_rat_config (cell_rat_settings_t * rat_settings,
                     bool * is_trigger_reboot, uint16_t * error_code)
{
    *error_code        = sqc_interval(0, INT_MAX);
    *is_trigger_reboot = sqc_nondet(0, 1);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_service_domain_settings_get (cell_service_domain_t * service_domain,
                                      uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(service_domain, sizeof(cell_service_domain_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_service_domain_config (cell_service_domain_t * service_domain,
                                uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_pdp_context_activate (uint8_t context_id, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_pdp_context_deactivate (uint8_t context_id, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_pdp_status_get (cell_network_info_t * p_net_info,
                         uint16_t *            error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_net_info, sizeof(cell_network_info_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_firmware_version_get (char * p_fw_ver, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(p_fw_ver, CELL_FW_VERSION_MAX_SIZE);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_cellular_info_get (cell_cellular_info_t * cellular_info,
                            uint16_t *             error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(cellular_info, sizeof(cell_cellular_info_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_dfota_local_init (cell_dfota_settings_t * settings,
                           uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_dfota_wireless_init (cell_dfota_settings_t * settings,
                              uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
bg95_dfota_local_update (void * arg, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_dfota_wireless_start (cell_dfota_settings_t * settings,
                               uint16_t *              error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_dfota_write (cell_dfota_block_t * data, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_dfota_download_cancel (void * arg, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

bool
cell_com_is_char_available (void) __attribute__((sqc_override))
{
#if defined(SQC_UNIT_APIS) || defined(SQC_UNIT_SOCKET)
    return sqc_nondet(true, false);
#else
    return false;
#endif
}

static at_req_status_t
specific_socket_create (cell_socket_settings_t * socket, uint32_t socket_id,
                        uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_send (cell_socket_t * socket, const uint8_t * data,
                      size_t * length, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(length, sizeof(size_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_sent_info_get (
    cell_socket_t * socket, cell_socket_data_verify_info_t * data_verify_info,
    uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_receive (cell_socket_t * socket, const uint8_t * data,
                         size_t * data_length, uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    sqc_make_unknown(data_length, sizeof(size_t));
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_close (uint32_t connect_id, cell_socket_service_type_t type,
                       uint16_t * error_code)
{
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_query_service_status (cell_socket_t * socket,
                                      uint16_t *      error_code)
{
    (void) socket;
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_socket_check_usability (all_socket_status_t * socket_state_handle,
                                 uint8_t context_id, uint16_t * error_code)
{
    socket_state_handle->no_supported_socket
        = sqc_interval(0, MAXIMUM_SUPPORT_SOCKET);
    for (uint8_t i = 0; i < MAXIMUM_SUPPORT_SOCKET; i++)
    {
        socket_state_handle->sockets[i] = sqc_nondet(true, false);
    }
    *error_code = sqc_interval(0, INT_MAX);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_enable_functionality (uint16_t * error_code)
{
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_sim_readiness_get (cell_sim_lock_status_t * status,
                            uint16_t *               error_code)
{
    *status = sqc_interval(CELL_SIM_CARD_READY, CELL_SIM_CARD_UNKNOWN);
    return sqc_interval(AT_SUCCESS, AT_ERROR_CMS);
}

static at_req_status_t
specific_sim_pin_enter (cell_simcard_settings_t sim_settings,
                        bool * is_sim_ready_requires_urc, uint16_t * error_code)
{
    at_req_status_t ret        = sqc_interval(AT_SUCCESS, AT_ERROR_CMS);

    *is_sim_ready_requires_urc = (ret == AT_SUCCESS);

    return ret;
}

void
process_remaining_data (void)
{
    if ((NULL == gp_modem_driver) || (NULL == gp_modem_driver->urc_handler))
    {
        return;
    }

    uint8_t urc_data[URC_DATA_MAXIMUM_SIZE];
    sqc_make_unknown(&urc_data, sizeof(urc_data));
    for (int handler_index = 0; handler_index < URC_TYPE_NUMBER;
         handler_index++)
    {
        if ((NULL != gp_modem_driver)
            && (NULL != gp_modem_driver->urc_handler)
            && (NULL != gp_modem_driver->urc_handler[handler_index].handler))
        {
            gp_modem_driver->urc_handler[handler_index].handler(&urc_data);
        }
    }
}

int
sqc_main (void)
{
    if (CELL_SUCCESS != cell_init(NULL, 0))
    {
        return -1;
    }

    cell_startup();

    // Mock gp_modem_driver across all 3 supported modems (BG95, EG915U, HL7900) + NULL
    gp_modem_driver = common_get_modem_driver(
        sqc_interval(MODEM_TYPE_INVALID, MODEM_TYPE_HL7900));

    // always prepare context settings as this is used for all analysis cases
    context_settings.modem_cb = sqc_nondet_ptr(cell_modem_event_handler, NULL);
    context_settings.get_cb   = sqc_nondet_ptr(cell_get_event_handler, NULL);
    context_settings.test_cb  = sqc_nondet_ptr(cell_test_event_handler, NULL);
    context_settings.dfota_cb = sqc_nondet_ptr(cell_dfota_handler, NULL);
    context_settings.create_cb
        = sqc_nondet_ptr(cell_context_event_handler, NULL);

    int * value = sqc_find_variable("g_current_context_idx", "cell_task.c");
    if (value != NULL)
    {
        *value = sqc_interval(0, 7);
    }

    int * p_status = (int *) sqc_find_variable("g_status", "cell_connect.c");
    if (p_status != NULL)
    {
        *p_status = sqc_interval(0, 10);
    }

    cell_context_create(sqc_nondet_ptr(&context_settings, NULL),
                        sqc_nondet_ptr(&cellular_context, NULL));

    // Prepare symbolic payloads for DFOTA, Socket, and Custom AT units
    dfota_settings.type = sqc_interval(CELL_DFOTA_TYPE_HTTP, CELL_DFOTA_TYPE_INVALID);
    dfota_settings.block_num = sqc_interval(0, 2000);
    dfota_settings.file_size = sqc_interval(1, 100000);
    memset(dfota_settings.url, 0, sizeof(dfota_settings.url));
    data_block.id   = sqc_interval(0, 2000);
    data_block.len  = sqc_interval(1, 1024);
    data_block.data = dfota_data;

    sqc_make_unknown(&socket_settings, sizeof(cell_socket_settings_t));
    socket_settings.event_cb = sqc_nondet_ptr(cell_socket_event_handler, NULL);
    socket_settings.service_type = sqc_interval(
        CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT,
        CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE + 1);
    sqc_make_unknown(&socket_data_info, sizeof(cell_socket_data_info_t));

    sqc_make_unknown(&test_request, sizeof(cell_test_request_t));
    test_request.data[sizeof(test_request.data) - 1]     = '\0';
    test_request.prefix[sizeof(test_request.prefix) - 1] = '\0';

#if defined(SQC_UNIT_CONNECT)
    analysis_case_t acase = ANALYSIS_CASE_CONNECT;
#elif defined(SQC_UNIT_DFOTA)
    analysis_case_t acase = sqc_interval(ANALYSIS_CASE_START_DFOTA, ANALYSIS_CASE_ACTIVATE_DFOTA);
#elif defined(SQC_UNIT_MONITORING)
    analysis_case_t acase = sqc_interval(ANALYSIS_CASE_GET_SIMCARD_INFO, ANALYSIS_CASE_GET_REJECT_CAUSE);
#elif defined(SQC_UNIT_SOCKET)
    analysis_case_t acase = ANALYSIS_CASE_SOCKET;
#else
    analysis_case_t acase = sqc_interval(ANALYSIS_CASE_INVALID, ANALYSIS_CASE_MAXIMUM);
#endif

    switch (acase)
    {
        case ANALYSIS_CASE_CONNECT:
        {
            sqc_make_unknown(&cell_settings, sizeof(cell_settings_t));
            // validity of the settings
            cell_settings.valid = sqc_nondet(true, false);
            // custom AT list settings
            cell_settings.custom_at_list.enable = sqc_nondet(true, false);
            // apn name
            cell_settings.pdp.apn_name[CELL_APN_MAX_SIZE] = '\0';
            cell_connect(&cellular_context, &cell_settings);
        }
        break;

        case ANALYSIS_CASE_GET_SIMCARD_INFO:
        {
            cell_simcard_info_t simcard_info;
            cell_simcard_info_get(&cellular_context, &simcard_info);
        }
        break;

        case ANALYSIS_CASE_START_SIM_SWAP:
        {
            cell_simcard_swap_settings_t swap_settings;
            sqc_make_unknown(&swap_settings,
                             sizeof(cell_simcard_swap_settings_t));
            cell_simcard_swap_start(&cellular_context, &swap_settings);
        }
        break;

        case ANALYSIS_CASE_ABORT_SIM_SWAP:
        {
            cell_simcard_swap_abort(&cellular_context);
            break;
        }

        case ANALYSIS_CASE_GET_MODEM_INFO:
        {
            cell_modem_info_t modem_info;
            cell_modem_info_get(&cellular_context, &modem_info);
        }
        break;

        case ANALYSIS_CASE_GET_CELLULAR_INFO:
        {
            cell_cellular_info_t cellular_info;
            cell_cellular_info_get(&cellular_context, &cellular_info);
        }
        break;

        case ANALYSIS_CASE_GET_SERVICE_STATUS:
        {
            cell_service_status_t service_status;
            cell_service_status_get(&cellular_context, &service_status);
        }
        break;

        case ANALYSIS_CASE_GET_NETWORK_INFO:
        {
            cell_network_info_t network_info;
            cell_network_info_get(&cellular_context, &network_info);
        }
        break;

        case ANALYSIS_CASE_GET_REJECT_CAUSE:
        {
            cell_reject_cause_t reject_cause;
            cell_reject_cause_get(&cellular_context, &reject_cause);
        }
        break;

        case ANALYSIS_CASE_CONFIG_PSM:
        {
            cell_psm_settings_t psm_settings;
            sqc_make_unknown(&psm_settings, sizeof(cell_psm_settings_t));
            cell_psm_config(&cellular_context, &psm_settings);
        }
        break;

        case ANALYSIS_CASE_GET_PSM_SETTINGS:
        {
            cell_psm_settings_t psm_settings;
            cell_psm_settings_get(&cellular_context, &psm_settings);
        }
        break;

        case ANALYSIS_CASE_CONFIG_EDRX:
        {
            cell_edrx_settings_t edrx_settings;
            sqc_make_unknown(&edrx_settings, sizeof(cell_edrx_settings_t));
            cell_edrx_config(&cellular_context, &edrx_settings);
        }
        break;

        case ANALYSIS_CASE_GET_EDRX_SETTINGS:
        {
            cell_edrx_settings_t edrx_settings;
            sqc_make_unknown(&edrx_settings, sizeof(cell_edrx_settings_t));
            cell_edrx_settings_get(&cellular_context, &edrx_settings);
        }
        break;

        case ANALYSIS_CASE_CONFIG_PING_ANSWER:
        {
            cell_ping_answer_settings_t ping_answer_settings;
            sqc_make_unknown(&ping_answer_settings,
                             sizeof(cell_ping_answer_settings_t));
            cell_ping_answer_config(&cellular_context, &ping_answer_settings);
        }
        break;

        case ANALYSIS_CASE_SOCKET:
        {
            sqc_make_unknown(&socket_settings, sizeof(cell_socket_settings_t));
            socket_settings.event_cb
                = sqc_nondet_ptr(cell_socket_event_handler, NULL);
            socket_settings.service_type
                = sqc_interval(CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT,
                               CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE + 1);

            sqc_make_unknown(&socket_data_info,
                             sizeof(cell_socket_data_info_t));

            if (cell_socket_create(&socket_settings, &socket_context)
                == CELL_SUCCESS)
            {
                switch (sqc_interval(0, 3))
                {
                    case 0:
                    {
                        cell_socket_send(&socket_context, &socket_data_info);
                    }
                    break;

                    case 1:
                    {
                        cell_socket_verify_data(&socket_context,
                                                &socket_data_verify_info);
                    }
                    break;

                    case 2:
                    {
                        cell_socket_receive(&socket_context, &socket_data_info);
                    }
                    break;
                }
                cell_socket_close(&socket_context);
            }
        }
        break;

        case ANALYSIS_CASE_SYNC_TIME:
        {
            cell_time_settings_t time_settings;
            sqc_make_unknown(&time_settings, sizeof(cell_time_settings_t));
            cell_time_sync(&cellular_context, &time_settings);
        }
        break;

        case ANALYSIS_CASE_GET_TIME:
        {
            cell_time_info_t time;
            cell_time_get(&cellular_context, &time);
        }
        break;

        case ANALYSIS_CASE_START_DFOTA:
        {
            dfota_settings.type
                = sqc_interval(CELL_DFOTA_TYPE_HTTP, CELL_DFOTA_TYPE_INVALID);
            dfota_settings.block_num = sqc_interval(0, 2000);
            dfota_settings.file_size = sqc_interval(1, 100000);
            memset(dfota_settings.url, 0, sizeof(dfota_settings.url));
            cell_dfota_start(&cellular_context, &dfota_settings);
        }
        break;

        case ANALYSIS_CASE_WRITE_DFOTA:
        {
            data_block.id   = sqc_interval(0, 2000);
            data_block.len  = sqc_interval_split(990, 1023);
            data_block.data = dfota_data;

            cell_dfota_write(&cellular_context, &data_block);
        }
        break;

        case ANALYSIS_CASE_ABORT_DFOTA:
        {
            cell_dfota_abort(&cellular_context);
        }
        break;

        case ANALYSIS_CASE_ACTIVATE_DFOTA:
        {
            cell_dfota_activate(&cellular_context);
        }
        break;

        case ANALYSIS_CASE_CONFIG_LOG:
        {
            cell_log_settings_t log_settings
                = {.level = sqc_interval(CELL_LOG_LEVEL_DEBUG,
                                         CELL_LOG_LEVEL_VERBOSE)};
            cell_log_config(&log_settings);
            cell_log_debug("");
            cell_log_normal("");
            cell_log_verbose("");
        }
        break;

        case ANALYSIS_CASE_CONFIG_TCP:
        {
            cell_tcp_config_t tcp_config;
            sqc_make_unknown(&tcp_config, sizeof(cell_tcp_config_t));
            cell_tcp_config(&cellular_context, &tcp_config);
        }
        break;

        case ANALYSIS_CASE_TEST_CELL:
        {
            sqc_make_unknown(&test_request, sizeof(cell_test_request_t));
            // data field is a NULL-terminated string
            test_request.data[sizeof(test_request.data) - 1] = '\0';
            // prefix field is a NULL-terminated string
            test_request.prefix[sizeof(test_request.prefix) - 1] = '\0';
            sqc_make_unknown(&test_response, sizeof(cell_test_response_t));
            cell_test(&cellular_context, &test_request, &test_response);
        }
        break;

        case ANALYSIS_CASE_CONFIG_PDP:
        {
            cell_pdp_settings_t pdp_settings;
            sqc_make_unknown(&pdp_settings, sizeof(cell_pdp_settings_t));
            cell_PDP_config(&cellular_context, &pdp_settings);
        }
        break;

        case ANALYSIS_CASE_CONFIG_DNS:
        {
            cell_dns_settings_t dns_settings;
            sqc_make_unknown(&dns_settings, sizeof(cell_dns_settings_t));
            cell_DNS_config(&cellular_context, &dns_settings);
        }
        break;

        case ANALYSIS_CASE_INCOMING_DATA:
        {
            cell_driver_uart_irq_handler();
        }
        break;

        default:
            break;
    }

    // Call all messages multiple times
    for (int i = 0; i < 3; i++)
    {
        cell_task(NULL);
    }

    cell_deinit();

    return 0;
}
