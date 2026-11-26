#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_dfota.c"
/**
 * @file cell_dfota.h
 * @copyright Copyright (C) 2025 Sagemcom. All rights reserved.
 * @par Licence
 * The source code contained herein contains proprietary
 * information which is the property of Sagemcom.
 * The source code and program may be used and/or copied
 * only with the written permission of Sagemcom.
 * Or in accordance with the terms and conditions
 * stipulated in the agreement/contract under which the programs
 * have been supplied.
 * @author HOANG THANH Long
 * @brief Modem DFOTA implementation
 * @date 2025-06-12
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/
#include "cell_dfota.h"
#include "cell_internal.h"
#include "cell_os.h"
#include "cell_modem_common.h"
#include "cell_log.h"
#include "cell_connect.h"

/*******************************************************
 * Standard Header Files                               *
 *******************************************************/
#include <string.h>

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define DFOTA_LOCAL_ACTIVATION_WAIT_MS (600000U)

/**
 * @brief DFOTA events
 *
 */
typedef enum
{
    DFOTA_EVENT_INVALID = 0,     /**< Invalid event */
    DFOTA_EVENT_START,           /**< Request to start the dfota procedure */
    DFOTA_EVENT_WRITE,           /**< Request to write a data block */
    DFOTA_EVENT_ACTIVATE,        /**< Request to activate new firmware */
    DFOTA_EVENT_DOWNLOAD_CANCEL, /**< Cancel the current DFOTA package download
                                  */
    DFOTA_EVENT_URC,             /**< Received related URC */
    DFOTA_EVENT_TIMER_EXPIRED    /**< Timer expired */
} event_t;

/*******************************************************
 * Static Data Declarations                            *
 *******************************************************/
static bool gb_is_dfota_running = false; /**< This is used to verify whether the
                                           dfota procedure is running */
static bool g_is_timer_activated
    = false; /**< This is to ensure the dfota timer is activated */
static uint32_t         g_timer_id = 0; /**< DFOTA timer ID */
static cell_context_t * gp_ctx
    = NULL; /**< The context that starts the DFOTA procedure successfully. */
static cell_dfota_settings_t * gp_settings
    = NULL; /**< The valid dfota setting received from application. */

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

typedef at_req_status_t (*command_wrapper_funct_t)(void *, uint16_t *);

static bool at_command_execute(command_wrapper_funct_t command, void * arg);
static bool local_init(cell_dfota_settings_t * settings);
static bool wireless_init(cell_dfota_settings_t * settings);
static bool fota_write(cell_dfota_block_t * block);
static bool activate_local(void);
static bool activate_wireless(cell_dfota_settings_t * settings);
static bool initialize(message_t * msg);
static bool download_cancel(void);
static void check_timer_event(void);
static void check_urc_event(dfota_urc_type_t urc_type);
static void network_reconnect(void);
static void refresh(void);
static void success_handler(void);
static void generic_error_handler(void);
static void event_notify(cell_dfota_event_t event);
static void timer_callback(void * arg);
static void timer_start(uint32_t time_ms);
static void timer_stop(void);
static void notify_START_reject(cell_context_t * ctx);
static void notify_START_result(bool result);
static void notify_WRITE_result(bool result);
static void notify_ACTIVATE_result(bool result);

static at_req_status_t local_init_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t wireless_init_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t local_write_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t local_trigger_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t wireless_start_wrapper(void *     arg,
                                              uint16_t * error_code);
static at_req_status_t download_cancel_wrapper(void *     arg,
                                               uint16_t * error_code);

/*******************************************************
 * Public Function Definition                          *
 *******************************************************/

bool
dfota_init (void)
{
    cell_os_err_t ret = cell_os_timer_create(&g_timer_id, timer_callback);
    if (CELL_OS_SUCCESS != ret)
    {
        cell_log_verbose("dfota -- Create DFOTA timer failed");
    }
    else
    {
        cell_log_verbose("dfota -- DFOTA timer created successfully");
    }
    return (CELL_OS_SUCCESS == ret);
}

void
dfota_process (message_t * msg)
{
    cell_log_verbose("dfota -- process status = %d, event = %d",
                     gb_is_dfota_running,
                     (event_t) msg->sub_type);
    switch ((event_t) msg->sub_type)
    {
        case DFOTA_EVENT_START:
        {
            if ((NULL != msg->data.ptr) && (!dfota_running_state_check()))
            {
                gp_ctx              = msg->context;
                gp_settings         = (cell_dfota_settings_t *) msg->data.ptr;
                gb_is_dfota_running = initialize(msg);
                notify_START_result(gb_is_dfota_running);
            }
            else
            {
                notify_START_reject(msg->context);
            }
        }
        break;

        case DFOTA_EVENT_WRITE:
        {
            if ((NULL != msg->data.ptr) && dfota_running_state_check())
            {
                bool result = fota_write((cell_dfota_block_t *) msg->data.ptr);
                notify_WRITE_result(result);
            }
            else
            {
                generic_error_handler();
            }
        }
        break;

        case DFOTA_EVENT_URC:
        {
            check_urc_event((dfota_urc_type_t) msg->data.value);
        }
        break;

        case DFOTA_EVENT_ACTIVATE:
            /**
             * The DFOTA process may unintentionally fail midway due to
             * factors such as network conditions or URCs. Therefore, a check is
             * required before performing the activation request.
             */
            if (dfota_running_state_check() && (NULL != gp_settings))
            {
                if (CELL_DFOTA_TYPE_LOCAL == gp_settings->type)
                {
                    bool result = activate_local();
                    if (result)
                    {
                        /**
                         * We will start the timer because the local
                         * activation action might take so long. If it exceeds
                         * the time duration, it is considered a failure.
                         */
                        timer_start(DFOTA_LOCAL_ACTIVATION_WAIT_MS);
                    }
                    notify_ACTIVATE_result(result);
                }
                else
                {
                    bool result = activate_wireless(gp_settings);
                    notify_ACTIVATE_result(result);
                }
            }
            else
            {
                generic_error_handler();
            }
            break;

        case DFOTA_EVENT_DOWNLOAD_CANCEL:
        {
            /**
             * Only the context that starts the dfota procedure at first
             * can send and perform the cancel request.
             */
            if ((gp_ctx == msg->context) && (NULL != gp_settings))
            {
                if (CELL_DFOTA_TYPE_LOCAL == gp_settings->type)
                {
                    refresh();
                }
                else
                {
                    (void) download_cancel();
                }
            }
        }
        break;

        case DFOTA_EVENT_TIMER_EXPIRED:
        {
            check_timer_event();
        }
        break;

        default:
            // TIS will raise an error if reaches this case
            //@ assert UB: \false;
            break;
    }
}

void
dfota_notify_start (cell_context_t * const        context,
                    const cell_dfota_settings_t * dfota_settings)
{
    send_msg_with_data(MESSAGE_DFOTA,
                       (uint8_t) DFOTA_EVENT_START,
                       (void *) dfota_settings,
                       sizeof(cell_dfota_settings_t),
                       context,
                       false);
}

void
dfota_notify_write (cell_context_t * const     context,
                    const cell_dfota_block_t * block)
{
    send_msg_with_data(MESSAGE_DFOTA,
                       (uint8_t) DFOTA_EVENT_WRITE,
                       (void *) block,
                       sizeof(cell_dfota_block_t),
                       context,
                       false);
}

void
dfota_notify_activate (cell_context_t * const context)
{
    send_msg_with_value(
        MESSAGE_DFOTA, (uint8_t) DFOTA_EVENT_ACTIVATE, 0, context, false);
}

void
dfota_notify_cancel (cell_context_t * const context)
{
    send_msg_with_value(MESSAGE_DFOTA,
                        (uint8_t) DFOTA_EVENT_DOWNLOAD_CANCEL,
                        0,
                        context,
                        false);
}

void
dfota_notify_urc (dfota_urc_type_t urc_type)
{
    cell_log_verbose(
        "dfota -- notify urc = %d, status = %d", urc_type, gb_is_dfota_running);
    send_msg_with_value(MESSAGE_DFOTA,
                        (uint8_t) DFOTA_EVENT_URC,
                        (uint32_t) urc_type,
                        NULL,
                        false);
}

void
dfota_notify_timer_expired (void)
{
    send_msg_with_value(
        MESSAGE_DFOTA, (uint8_t) DFOTA_EVENT_TIMER_EXPIRED, 0, NULL, false);
}

bool
dfota_running_state_check ()
{
    return gb_is_dfota_running;
}

/*******************************************************
 * Private Function Definition                         *
 *******************************************************/

/**
 * @brief Execute AT commands.
 *
 * @param command Modem command wrapper function.
 * @param arg Input argument for the modem command.
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
at_command_execute (command_wrapper_funct_t command, void * arg)
{
    uint16_t        err_code  = 0;
    at_req_status_t at_status = command(arg, &err_code);
    return (AT_SUCCESS == at_status) ? true : false;
}

/**
 * @brief Initialize local dfota procedure.
 *
 * @param settings Local dfota configuration.
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
local_init (cell_dfota_settings_t * settings)
{
    return at_command_execute(local_init_wrapper, settings);
}

/**
 * @brief Initialize wireless dfota procedure.
 *
 * @param settings Wireless dfota configuration.
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
wireless_init (cell_dfota_settings_t * settings)
{
    return at_command_execute(wireless_init_wrapper, settings);
}

/**
 * @brief Write the data with the number of bytes length to a file.
 *
 * @param block Block of data contains content and length.
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
fota_write (cell_dfota_block_t * block)
{
    return at_command_execute(local_write_wrapper, block);
}

/**
 * @brief Activate the new local firmware file.
 *
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
activate_local (void)
{
    return at_command_execute(local_trigger_wrapper, NULL);
}

/**
 * @brief Start the wireless DFOTA.
 *
 * @param settings dfota configuration.
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
activate_wireless (cell_dfota_settings_t * settings)
{
    return at_command_execute(wireless_start_wrapper, settings);
}

/**
 * @brief Cancel the current DFOTA package download.
 *
 * @return true if at command is executed successfully - false if at command is
 * executed unsuccessfully.
 */
static bool
download_cancel (void)
{
    return at_command_execute(download_cancel_wrapper, NULL);
}

/**
 * @brief Refresh all the public variables of the DFOTA procedure.
 *
 */
static void
refresh (void)
{
    gp_ctx              = NULL;
    gp_settings         = NULL;
    gb_is_dfota_running = false;
}

/**
 * @brief Action after successful dfota update.
 *
 */
static void
success_handler (void)
{
    timer_stop();
    event_notify(CELL_DFOTA_EVENT_OK);
    cell_log_verbose("Module Firmware Update Done");
    refresh();
    network_reconnect();
}

/**
 * @brief If every DFOTA actions is not performed successfully, this will be
 * called to handle the error. For the DFOTA procedure, every type of error will
 * be treated as failure.
 *
 */
static void
generic_error_handler (void)
{
    event_notify(CELL_DFOTA_EVENT_FAILED);
    refresh();
}

/**
 * @brief Action to notify request rejection to application.
 *
 */
static void
notify_START_reject (cell_context_t * ctx)
{
    if (NULL != ctx)
    {
        if (NULL != ctx->dfota_cb)
        {
            ctx->dfota_cb(CELL_DFOTA_EVENT_FAILED);
        }
    }
}

/**
 * @brief Initialize the DFOTA procedure following its type.
 *
 * @param settings DFOTA configuration.
 * @return true if at command is executed successfully - false if at commands
 * are executed unsuccessfully.
 */
static bool
initialize (message_t * msg)
{
    bool                    ret     = false;
    cell_dfota_settings_t * p_dfota = (cell_dfota_settings_t *) msg->data.ptr;
    switch (p_dfota->type)
    {
        case CELL_DFOTA_TYPE_LOCAL:
        {
            ret = local_init(p_dfota);
        }
        break;

        case CELL_DFOTA_TYPE_HTTP:
        case CELL_DFOTA_TYPE_COAP:
        case CELL_DFOTA_TYPE_FTP:
        {
            ret = wireless_init(p_dfota);
        }
        break;

        default:
            break;
    }
    return ret;
}

/**
 * @brief This function is used to handle the DFOTA URC returned by the module.
 *
 * @param urc Type of URC and relevant information (if have).
 */
static void
check_urc_event (dfota_urc_type_t urc_type)
{
    cell_log_verbose("connection -- received urc = %d, status = %d",
                     urc_type,
                     gb_is_dfota_running);
    switch (urc_type)
    {
        case DFOTA_URC_TYPE_DOWNLOADING:
            cell_log_verbose("dfota -- Firmware is being downloaded.");
            break;

        case DFOTA_URC_TYPE_DOWNLOAD_SUCCESSFUL:
            cell_log_verbose("dfota -- Firmware is downloaded successfully.");
            break;

        case DFOTA_URC_TYPE_DOWNLOAD_FAILED:
        {
            cell_log_verbose("dfota -- Fail to download firmware.");
            generic_error_handler();
        }
        break;

        case DFOTA_URC_TYPE_UPDATING:
        {
            cell_log_verbose("dfota -- DFOTA is in updating progress.");
        }
        break;

        case DFOTA_URC_TYPE_SUCCESSFUL:
        {
            success_handler();
        }
        break;

        case DFOTA_URC_TYPE_FAILED:
        {
            generic_error_handler();
        }
        break;

        default:
            break;
    }
}

/**
 * @brief Handle timer events. The DFOTA procedure is failed whenever this is
 * reached.
 */
static void
check_timer_event (void)
{
    if (g_is_timer_activated)
    {
        g_is_timer_activated = false;
        generic_error_handler();
    }
}

/**
 * @brief Notify event to that application. This is used for the context that
 * starts the DFOTA procedure successfully.
 *
 * @param event The event needs notifying.
 */
static void
event_notify (cell_dfota_event_t event)
{
    if ((NULL != gp_ctx) && (NULL != gp_ctx->dfota_cb))
    {
        gp_ctx->dfota_cb(event);
    }
}

/**
 * @brief One-shot timer callback for the connection process.
 *
 * @param timer_id
 */
static void
timer_callback (void * arg)
{
    dfota_notify_timer_expired();
}

/**
 * @brief Start timer with specific time duration.
 *
 * @param time_ms Time duration in millisecond.
 */
static void
timer_start (uint32_t time_ms)
{
    if (CELL_OS_SUCCESS == cell_os_timer_start(g_timer_id, time_ms))
    {
        g_is_timer_activated = true;
    }
}

/**
 * @brief Stop timer.
 *
 */
static void
timer_stop (void)
{
    if (CELL_OS_SUCCESS == cell_os_timer_stop(g_timer_id))
    {
        g_is_timer_activated = false;
    }
}

/**
 * @brief Handle START result.
 *
 * @param result
 */
static void
notify_START_result (bool result)
{
    if (result)
    {
        event_notify(CELL_DFOTA_EVENT_INIT_OK);
    }
    else
    {
        event_notify(CELL_DFOTA_EVENT_INIT_FAILED);
    }
}

/**
 * @brief Handle WRITE result.
 *
 * @param result
 */
static void
notify_WRITE_result (bool result)
{
    if (result)
    {
        event_notify(CELL_DFOTA_EVENT_BLOCK_TRANSFER_OK);
    }
    else
    {
        event_notify(CELL_DFOTA_EVENT_BLOCK_TRANSFER_FAILED);
        generic_error_handler();
    }
}

/**
 * @brief Handle ACTIVATE result.
 *
 * @param result
 */
static void
notify_ACTIVATE_result (bool result)
{
    if (result)
    {
        event_notify(CELL_DFOTA_EVENT_ACTIVATION_OK);
    }
    else
    {
        event_notify(CELL_DFOTA_EVENT_ACTIVATION_FAILED);
        generic_error_handler();
    }
}

/**
 * @brief Request to start the connection procedure.
 *
 */
static void
network_reconnect (void)
{
    cell_log_verbose("dfota -- Request to reconnect to the network");
    static cell_settings_t reconnect;
    reconnect.reset_type = CELL_MODEM_RESET_SOFT;
    reconnect.valid      = true;
    connection_notify_action(NULL, &reconnect);
}

static at_req_status_t
local_init_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->dfota_local_init))
    {
        ret = gp_modem_driver->dfota_local_init((cell_dfota_settings_t *) arg,
                                                error_code);
    }
    return ret;
}

static at_req_status_t
wireless_init_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->dfota_wireless_init))
    {
        ret = gp_modem_driver->dfota_wireless_init(
            (cell_dfota_settings_t *) arg, error_code);
    }
    return ret;
}

static at_req_status_t
local_write_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->dfota_write))
    {
        ret = gp_modem_driver->dfota_write((cell_dfota_block_t *) arg,
                                           error_code);
    }
    return ret;
}

static at_req_status_t
local_trigger_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->dfota_local_update))
    {
        ret = gp_modem_driver->dfota_local_update(NULL, error_code);
    }
    return ret;
}

static at_req_status_t
wireless_start_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->dfota_wireless_start))
    {
        ret = gp_modem_driver->dfota_wireless_start(
            (cell_dfota_settings_t *) arg, error_code);
    }
    return ret;
}

static at_req_status_t
download_cancel_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->dfota_download_cancel))
    {
        ret = gp_modem_driver->dfota_download_cancel(NULL, error_code);
    }
    return ret;
}
