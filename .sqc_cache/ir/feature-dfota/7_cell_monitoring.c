#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_monitoring.c"
/**
 * @file cell_monitoring.c
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
 * @brief Implementations of monitoring feature.
 * @date 2025-06-12
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/

#include "cell_monitoring.h"
#include "cell_os.h"
#include "cell_types.h"
#include "cell_log.h"
#include "cell_modem_common.h"
#include "cell_dfota.h"
#include "cell_connect.h"

/*******************************************************
 * Standard Header Files                               *
 *******************************************************/

#include <string.h>

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define AT_COMMAND_RETRIES_MAX        (5)
#define AT_COMMAND_RETRIES_TIMEOUT_MS (3000)
#define MCC_LENGTH                    3 // Mobile Country Code is 3 digits
#define MNC_LENGTH                    2 // Mobile Network Code is 2 digits
#define MCC_OFFSET                    0
#define MNC_OFFSET                    3

/**
 * @brief Monitoring events
 */
typedef enum
{
    MONITORING_EVENT_GET_SIMCARD_INFO = 0, /** Get simcard info event */
    MONITORING_EVENT_GET_MODEM_INFO,       /** Get modem info event */
    MONITORING_EVENT_GET_PSM_SETTING,      /** Get psm settings event */
    MONITORING_EVENT_GET_CELLULAR_INFO,    /** Get cellular info event */
    MONITORING_EVENT_GET_SERVICE_STATUS,   /** Get service status event */
    MONITORING_EVENT_GET_NETWORK_INFO      /** Get network info event */
} monitoring_event_t;

typedef at_req_status_t (*modem_command_wrapper_func_t)(void *, uint16_t *);

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

static bool execute(modem_command_wrapper_func_t command, void * arg);

static at_req_status_t get_manufacturer_info_wrapper(void *     arg,
                                                     uint16_t * err_code);

static at_req_status_t get_model_id_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_fw_ver_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_imei_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_imsi_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_iccid_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_psm_config_wrapper(void * arg, uint16_t * err_code);

static at_req_status_t get_cellular_info_wrapper(void *     arg,
                                                 uint16_t * err_code);

static at_req_status_t get_signal_quality_report_wrapper(void *     arg,
                                                         uint16_t * err_code);

static at_req_status_t get_cs_network_reg_status_wrapper(void *     arg,
                                                         uint16_t * err_code);

static at_req_status_t get_eps_network_reg_status_wrapper(void *     arg,
                                                          uint16_t * err_code);

static at_req_status_t get_gprs_network_reg_status_wrapper(void *     arg,
                                                           uint16_t * err_code);

static at_req_status_t get_operator_info_wrapper(void *     arg,
                                                 uint16_t * err_code);

static at_req_status_t get_pdp_status_wrapper(void * arg, uint16_t * err_code);

static void            get_simcard_info(cell_simcard_info_t * simcard_info,
                                        cell_context_t *      p_context);

static void            get_modem_info(cell_modem_info_t * modem_info,
                                      cell_context_t *    p_context);

static void            get_psm_setting(cell_psm_settings_t * psm_settings,
                                       cell_context_t *      p_context);

static void            get_cellular_info(cell_cellular_info_t * cellular_info,
                                         cell_context_t *       p_context);

static void get_service_status(cell_service_status_t * service_status,
                               cell_context_t *        p_context);

static void get_network_info(cell_network_info_t * network_info,
                             cell_context_t *      p_context);

static void notify_application(cell_get_event_t event,
                               cell_context_t * p_context, bool result);

static bool is_dfota_not_running(void);
static bool is_connected(void);

/*******************************************************
 * Public Function Bodies                              *
 *******************************************************/

void
monitoring_process (message_t * msg)
{
    if (NULL == msg->data.ptr)
    {
        return;
    }

    switch ((monitoring_event_t) (msg->sub_type))
    {
        case MONITORING_EVENT_GET_SIMCARD_INFO:
            get_simcard_info((cell_simcard_info_t *) msg->data.ptr,
                             msg->context);
            break;

        case MONITORING_EVENT_GET_MODEM_INFO:
            get_modem_info((cell_modem_info_t *) msg->data.ptr, msg->context);
            break;

        case MONITORING_EVENT_GET_PSM_SETTING:
            get_psm_setting((cell_psm_settings_t *) msg->data.ptr,
                            msg->context);
            break;

        case MONITORING_EVENT_GET_CELLULAR_INFO:
            get_cellular_info((cell_cellular_info_t *) msg->data.ptr,
                              msg->context);
            break;

        case MONITORING_EVENT_GET_SERVICE_STATUS:
            get_service_status((cell_service_status_t *) msg->data.ptr,
                               msg->context);
            break;

        case MONITORING_EVENT_GET_NETWORK_INFO:
            get_network_info((cell_network_info_t *) msg->data.ptr,
                             msg->context);
            break;

        default:
            // TIS will raise an error if reaches this case
            //@ assert UB: \false;
            break;
    }
}

void
monitoring_notify_get_modem_info (cell_context_t * const context,
                                  cell_modem_info_t *    modem_info)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_MODEM_INFO,
                       modem_info,
                       sizeof(cell_modem_info_t),
                       context,
                       false);
}

void
monitoring_notify_get_simcard_info (cell_context_t * const context,
                                    cell_simcard_info_t *  simcard_info)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_SIMCARD_INFO,
                       simcard_info,
                       sizeof(cell_simcard_info_t),
                       context,
                       false);
}

void
monitoring_notify_get_psm_setting (cell_context_t * const context,
                                   cell_psm_settings_t *  psm_setting)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_PSM_SETTING,
                       psm_setting,
                       sizeof(cell_psm_settings_t),
                       context,
                       false);
}

void
monitoring_notify_get_cellular_info (cell_context_t * const context,
                                     cell_cellular_info_t * cellular_info)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_CELLULAR_INFO,
                       cellular_info,
                       sizeof(cell_cellular_info_t),
                       context,
                       false);
}

void
monitoring_notify_get_service_status (cell_context_t * const  context,
                                      cell_service_status_t * service_status)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_SERVICE_STATUS,
                       service_status,
                       sizeof(cell_service_status_t),
                       context,
                       false);
}

void
monitoring_notify_get_network_info (cell_context_t * const context,
                                    cell_network_info_t *  network_info)
{
    send_msg_with_data(MESSAGE_MONITORING,
                       MONITORING_EVENT_GET_NETWORK_INFO,
                       network_info,
                       sizeof(cell_network_info_t),
                       context,
                       false);
}

/*******************************************************
 * Private Function Bodies                             *
 *******************************************************/

/**
 * @brief General function for executing at command and retrying
 *
 * @param[in] command Function pointer to the modem command wrapper.
 * @param[in] arg Argument passed to the command function.
 *
 * @return `true` if the command succeeds, `false` otherwise.
 */
static bool
execute (modem_command_wrapper_func_t command, void * arg)
{
    at_req_status_t at_status = AT_SUCCESS;
    bool            done      = false;

    do
    {
        uint16_t err_code = 0;
        at_status         = command(arg, &err_code);

        switch (at_status)
        {
            case AT_TIMEOUT:
                cell_log_verbose("AT Timeout");
                done = true;
                break;

            case AT_SUCCESS:
                cell_log_verbose("AT Success");
                done = true;
                break;

            default:
                done = true;
                break;
        }
    }
    while (!done);

    return (at_status == AT_SUCCESS);
}

static at_req_status_t
get_manufacturer_info_wrapper (void * arg, uint16_t * err_code)
{
    return common_manufacturer_info_get((char *) arg, err_code);
}

static at_req_status_t
get_model_id_wrapper (void * arg, uint16_t * err_code)
{
    return common_model_id_get((char *) arg, err_code);
}

static at_req_status_t
get_fw_ver_wrapper (void * arg, uint16_t * err_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->firmware_version_get))
    {
        //@ assert gp_modem_driver->firmware_version_get != \null;
        ret = gp_modem_driver->firmware_version_get((char *) arg, err_code);
    }
    else
    {
        ret = common_firmware_version_get((char *) arg, err_code);
    }

    return ret;
}

static at_req_status_t
get_imei_wrapper (void * arg, uint16_t * err_code)
{
    return common_imei_get((char *) arg, err_code);
}

static at_req_status_t
get_imsi_wrapper (void * arg, uint16_t * err_code)
{
    return common_imsi_get((char *) arg, err_code);
}

static at_req_status_t
get_iccid_wrapper (void * arg, uint16_t * err_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->iccid_get))
    {
        ret = gp_modem_driver->iccid_get(arg, err_code);
    }
    return ret;
}

static at_req_status_t
get_psm_config_wrapper (void * arg, uint16_t * err_code)
{
    return common_psm_config_get((cell_psm_settings_t *) arg, err_code);
}

static at_req_status_t
get_cellular_info_wrapper (void * arg, uint16_t * err_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if (gp_modem_driver && gp_modem_driver->cellular_info_get)
    {
        ret = gp_modem_driver->cellular_info_get((cell_cellular_info_t *) arg,
                                                 err_code);
    }
    return ret;
}

static at_req_status_t
get_signal_quality_report_wrapper (void * arg, uint16_t * err_code)
{
    return common_signal_quality_report((cell_signal_info_t *) arg, err_code);
}

static at_req_status_t
get_cs_network_reg_status_wrapper (void * arg, uint16_t * err_code)
{
    return common_cs_network_reg_status_get((cell_network_reg_status_t *) arg,
                                            err_code);
}

static at_req_status_t
get_eps_network_reg_status_wrapper (void * arg, uint16_t * error_code)
{
    return common_eps_network_reg_status_get((cell_network_reg_status_t *) arg,
                                             error_code);
}

static at_req_status_t
get_gprs_network_reg_status_wrapper (void * arg, uint16_t * error_code)
{
    return common_gprs_network_reg_status_get((cell_network_reg_status_t *) arg,
                                              error_code);
}

static at_req_status_t
get_operator_info_wrapper (void * arg, uint16_t * err_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if (gp_modem_driver && gp_modem_driver->operator_info_get)
    {
        ret = gp_modem_driver->operator_info_get((cell_operator_info_t *) arg,
                                                 err_code);
    }
    return ret;
}

static at_req_status_t
get_pdp_status_wrapper (void * arg, uint16_t * err_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if (gp_modem_driver && gp_modem_driver->pdp_status_get)
    {
        ret = gp_modem_driver->pdp_status_get((cell_network_info_t *) arg,
                                              err_code);
    }
    return ret;
}

static void
get_simcard_info (cell_simcard_info_t * simcard_info,
                  cell_context_t *      p_context)
{
    bool result = is_dfota_not_running();

    if (result)
    {
        result = execute(get_imsi_wrapper, simcard_info->imsi);
    }

    if (result)
    {
        result = execute(get_iccid_wrapper, simcard_info->iccid);
    }

    if (result)
    {
        // Extract mcc and mnc from IMSI
        simcard_info->plmn.mcc[CELL_MCC_MAX_SIZE] = '\0';
        simcard_info->plmn.mnc[CELL_MNC_MAX_SIZE] = '\0';
        memcpy(simcard_info->plmn.mcc,
               simcard_info->imsi + MCC_OFFSET,
               MCC_LENGTH);
        memcpy(simcard_info->plmn.mnc,
               simcard_info->imsi + MNC_OFFSET,
               MNC_LENGTH);
    }

    notify_application(CELL_GET_EVENT_SIMCARD_INFO, p_context, result);
}

static void
get_modem_info (cell_modem_info_t * modem_info, cell_context_t * p_context)
{
    bool result = is_dfota_not_running();

    if (result)
    {
        result
            = execute(get_manufacturer_info_wrapper, modem_info->manufacturer);
    }

    if (result)
    {
        result = execute(get_model_id_wrapper, modem_info->model_id);
    }

    if (result)
    {
        result = execute(get_fw_ver_wrapper, modem_info->fw_version);
    }

    if (result)
    {
        result = execute(get_imei_wrapper, modem_info->imei);
    }

    notify_application(CELL_GET_EVENT_MODEM_INFO, p_context, result);
}

static void
get_psm_setting (cell_psm_settings_t * psm_settings, cell_context_t * p_context)
{
    bool result = is_dfota_not_running() && is_connected();

    if (result)
    {
        result = execute(get_psm_config_wrapper, psm_settings);
    }

    notify_application(CELL_GET_EVENT_PSM_SETTINGS, p_context, result);
}

static void
get_cellular_info (cell_cellular_info_t * cellular_info,
                   cell_context_t *       p_context)
{
    bool result = is_dfota_not_running() && is_connected();

    if (result)
    {
        result = execute(get_cellular_info_wrapper, cellular_info);
    }

    if (result)
    {
        result = execute(get_signal_quality_report_wrapper,
                         &cellular_info->serving_cell.signal);
    }

    notify_application(CELL_GET_EVENT_CELLULAR_INFO, p_context, result);
}

static void
get_service_status (cell_service_status_t * service_status,
                    cell_context_t *        p_context)
{
    bool result = is_dfota_not_running() && is_connected();

    if (result)
    {
        result = execute(get_cs_network_reg_status_wrapper,
                         &service_status->cs_reg_status);
    }

    if (result)
    {
        result = execute(get_operator_info_wrapper,
                         &service_status->operator_info);
    }

    if (result)
    {
        switch (service_status->operator_info.rat)
        {
            case CELL_RAT_CATM1:
            case CELL_RAT_NBIOT:
            case CELL_RAT_LTE:
                result = execute(get_eps_network_reg_status_wrapper,
                                 &service_status->ps_reg_status);
                break;

            case CELL_RAT_GSM:
                result = execute(get_gprs_network_reg_status_wrapper,
                                 &service_status->ps_reg_status);
                break;

            default:
                result = false;
                break;
        }
    }

    notify_application(CELL_GET_EVENT_SERVICE_STATUS, p_context, result);
}

static void
get_network_info (cell_network_info_t * network_info,
                  cell_context_t *      p_context)
{
    bool result = is_dfota_not_running() && is_connected();

    if (result)
    {
        result = execute(get_pdp_status_wrapper, network_info);
    }

    notify_application(CELL_GET_EVENT_NETWORK_INFO, p_context, result);
}

static void
notify_application (cell_get_event_t event, cell_context_t * p_context,
                    bool result)
{
    if ((NULL != p_context) && (NULL != p_context->get_cb))
    {
        p_context->get_cb(event, result);
    }
}

/**
 * @brief Checks if the DFOTA procedure is currently running
 *
 * @note The monitoring features may be called while the DFOTA procedure is
 * running. This is to ensure there is no monitoring AT command executed.
 *
 * @return `true` if DFOTA is not running, `false` otherwise.
 */
static bool
is_dfota_not_running ()
{
    return (!dfota_running_state_check());
}

/**
 * @brief Checks if the modem is connected to network.
 *
 * @note Some monitoring features should only be executed after
 * the modem is connected to network.
 *
 * @return `true` if connection has been established - `false` otherwise.
 */
static bool
is_connected ()
{
    return connection_is_ready();
}
