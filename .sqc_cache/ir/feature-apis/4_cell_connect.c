#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_connect.c"
/**
 * @file cell_connect.c
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
 * @brief Connection procedure source file
 * @date 2025-07-11
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/

#include "cell_connect.h"
#include "cell_internal.h"
#include "cell_log.h"
#include "cell_types.h"
#include "cell_os.h"
#include "cell_com.h"
#include "cell_at.h"
#include "cell_modem_common.h"
#include "cell_dfota.h"

/*******************************************************
 * Third-party Header Files                            *
 *******************************************************/

#if (CONFIG_USE_EXTERNAL_RANDOMIZATION == 1)
#include "random_generator_itf.h"
#endif

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

/*******************************************************
 * Standard Header Files                               *
 *******************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define CHECK_MODEM_SLOW_RETRY_TIMES                 (20)
#define CHECK_MODEM_QUICK_RETRY_TIMES                (5)
#define CHECK_MODEM_INTERVAL_MS                      (3000)

#define COMMAND_TIMEOUT_RETRIES_MAX                  (5)
#define COMMAND_TIMEOUT_RANDOM_START_WINDOW_SEC      (2)
#define COMMAND_TIMEOUT_RANDOM_MULTIPLICATION_FACTOR (1)

#define FAST_REBOOT_MAXIMUM_SEC                      (300)

#define UE_FULL_FUNCTIONALITY_WAIT_MS                (5000)

#define URC_SIM_READY_WAIT_TIME_SEC                  (60)
#define URC_REGISTRATION_WAIT_TIME_SEC               (60 * 10)

/**
 * @brief External events
 */
typedef enum
{
    EVENT_INVALID = 0,   /**< Invalid event */
    EVENT_CONNECT,       /**< Connection procedure is requested */
    EVENT_URC,           /**< Received related URC */
    EVENT_TIMER_EXPIRED, /**< Timer expired */
} event_t;

/**
 * @brief Status of the procedure
 */
typedef enum
{
    STATUS_UNINIT = 0,            /**< Modem is un-initialized */
    STATUS_WAITING_FOR_SIM_READY, /**< Modem is waiting for SIM ready URC */
    STATUS_REGISTERING,           /**< Modem is waiting for registration URC */
    STATUS_CONNECTED,             /**< Modem is connected  */
} status_t;

/**
 * @brief Action to be performed
 */
typedef enum
{
    ACTION_INVALID = 0,         /**< Invalid action */
    ACTION_INIT,                /**< Init modem */
    ACTION_SEND_CUSTOM_AT_LIST, /**< Send the custom AT list */
    ACTION_LOAD_MODEM_DRIVER,   /**< Load modem driver */
    ACTION_SETUP_MODEM,         /**< Setup modem */
    ACTION_CHECK_CONFIG,       /**< Check if settings currently used is valid */
    ACTION_CONFIG,             /**< Config modem using application settings */
    ACTION_PREPARE_SIM,        /**< Prepare SIM */
    ACTION_START_REGISTRATION, /**< Start the registration */
    ACTION_ACTIVATE_PDP,       /**< Activate the PDP context */
    ACTION_READ_NETWORK_STATUS, /**< Get the network status */
    ACTION_READ_APPLIED_CONFIG, /**< Get the applied settings */
    ACTION_REBOOT,              /**< Reboot the modem */
    ACTION_HANDLE_REJECTION,    /**< Handle rejection */
    ACTION_LEAVE_AND_WAIT,      /**< Leave the loop */
} action_t;

/**
 * @brief Result of actions
 */
typedef enum
{
    RESULT_NOT_PERFORMED = 0, /**< Action is not performed yet */
    RESULT_SUCCESS,           /**< Action succeeded */
    RESULT_REBOOT_TRIGGERED,  /**< Action succeeded and triggered a modem reboot
                               */
    RESULT_URC_PENDING, /**< Action succeeded and is awaiting a completion URC
                         */
    RESULT_ERR_TIMEOUT, /**< Action timed-out  */
    RESULT_ERR_CME, /**< Action involves AT command returned "+CME:" error  */
    RESULT_ERR_CMS, /**< Action involves AT command returned "+CMS:" error  */
    RESULT_ERR_AT_OTHER_ERROR, /**< Action involves AT command returned "ERROR"
                                  error*/
    RESULT_ERR_UNSUPPORTED,    /**< Action was unsupported */
} result_t;

typedef struct
{
    bool trigger_reboot;   /**< Determine whether the modem reboot is triggered
                              itself by AT command */
    bool need_to_wait_urc; /**< Determine whether the modem needs to wait for
                              URC after the command is sent */
} side_effect_t;

/**
 * @brief Flags indicating which configuration categories require
 * reconfiguration
 */
typedef struct
{
    bool pdp;
    bool rat;
    bool service;
    bool psm;
} need_config_t;

/*******************************************************
 * Static Data Declarations                            *
 *******************************************************/

/**
 * @brief Connection timer ID
 */
static uint32_t g_timer_id = 0;

/**
 * @brief Timeout settings used by AT command timeout retries
 */
static cell_phy_randomisation_cfg_t g_command_retry_timeout_settings
    = {.randomisation_start_window = COMMAND_TIMEOUT_RANDOM_START_WINDOW_SEC,
       .multiplication_factor = COMMAND_TIMEOUT_RANDOM_MULTIPLICATION_FACTOR,
       .number_of_retries     = COMMAND_TIMEOUT_RETRIES_MAX};

/**
 * @brief Connection reboot count
 */
static uint16_t g_reboot_count = 0;

/**
 * @brief Connection process status
 */
static status_t g_status = STATUS_UNINIT;

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

/* Event Process Functions */
static action_t check_event_CONNECT(message_t * msg, status_t status,
                                    cell_settings_t * current_settings);
static action_t check_event_URC(message_t * msg, status_t status);
static action_t check_event_TIMER(message_t * msg, status_t status);
static status_t perform_action(action_t          start_action,
                               cell_settings_t * settings);
static action_t check_reset_request(cell_settings_t * settings);

/* Action Functions */
static result_t initialize(cell_settings_t * settings, uint16_t * error_code);
static action_t next_action_INIT(result_t result, uint16_t error_code,
                                 cell_settings_t * settings);

static void     send_custom_at_list(cell_custom_at_list_t * at_list);

static result_t load_modem_driver(uint16_t * error_code);
static action_t next_action_LOAD_MODEM_DRIVER(result_t result,
                                              uint16_t error_code);

static result_t setup_modem(cell_settings_t * settings, uint16_t * error_code);
static action_t next_action_SETUP_MODEM(result_t result, uint16_t error_code);

static result_t prepare_simcard(cell_settings_t * settings,
                                uint16_t *        error_code);
static action_t next_action_PREPARE_SIMCARD(result_t result,
                                            uint16_t error_code);

static result_t read_config(uint32_t          context_id,
                            cell_settings_t * current_settings,
                            uint16_t *        error_code);
static action_t next_action_CHECK_CONFIG(result_t result, uint16_t error_code,
                                         cell_settings_t * current,
                                         cell_settings_t * expected,
                                         need_config_t *   reconfig);
static bool     is_band_different(const cell_rat_settings_t * exp,
                                  const cell_rat_settings_t * cur,
                                  cell_rat_type_t             rat);
static bool     is_rat_different(const cell_rat_settings_t * exp,
                                 const cell_rat_settings_t * cur);
static bool     need_to_re_config(cell_settings_t * expected,
                                  cell_settings_t * current,
                                  need_config_t *   reconfig);
static result_t config(cell_settings_t * settings, need_config_t * reconfig,
                       uint16_t * error_code);
static action_t next_action_CONFIG(result_t result, uint16_t error_code);

static result_t start_registration(cell_settings_t *           settings,
                                   cell_network_reg_status_t * reg_stat,
                                   uint16_t *                  error_code);
static bool     is_registered(cell_network_reg_status_t reg_stat);
static cell_network_reg_status_t aggregate_ps_registration_status(
    cell_network_reg_status_t eps_reg_stat,
    cell_network_reg_status_t gprs_reg_stat);
static result_t get_registration_status(const cell_rat_settings_t * rat,
                                        cell_network_reg_status_t * reg_stat,
                                        uint16_t *                  error_code);
static action_t next_action_START_REGISTRATION(
    result_t result, cell_network_reg_status_t reg_status, uint16_t error_code);

static result_t activate_pdp_context(uint8_t context_id, uint16_t * error_code);
static action_t next_action_ACTIVATE_PDP(result_t result, uint16_t error_code);

static result_t read_network_status(uint8_t context_id, uint16_t * error_code);
static action_t next_action_READ_NETWORK_STATUS(result_t result,
                                                uint16_t error_code);

static void     reboot_modem(cell_phy_randomisation_cfg_t * random_cfg);
static bool     is_rat_supports(const cell_rat_settings_t * rat,
                                cell_rat_type_t             target);
static bool     is_lte_rat(const cell_rat_settings_t * rat);
static bool     is_gsm_rat(const cell_rat_settings_t * rat);

static result_t handle_rejection(uint16_t * error_code);
static action_t next_action_HANDLE_REJECTION(result_t result,
                                             uint16_t error_code);

static action_t next_action_DEFAULT(result_t result, uint16_t error_code);

static action_t handle_CME_error(cme_error_t cme_err);
static action_t handle_CMS_error(cms_error_t cms_err);

/* Timer Functions */
static void start_timer(uint32_t milliseconds);
static void stop_timer(void);
static void connection_timer_callback(void * arg);

/* Multiple-step Functions */
static bool     is_settings_valid(cell_settings_t * settings);
static result_t check_operator_status(uint16_t * error_code);
static result_t enable_functionality(uint16_t * error_code);
static result_t disable_functionality(uint16_t * error_code);
static bool     is_fast_reboot(uint32_t timeout_ms);
static void     slow_reboot(void);
static void     fast_reboot(void);
static void     power_modem_up_down_gpio(void);
static void     reset_modem_gpio(void);
static bool     is_modem_ready(int retry_times);
static result_t check_again_with_other_baudrates(void);

/* Command Execution Template */
typedef at_req_status_t (*modem_command_wrapper_func_t)(void *, uint16_t *);
static result_t execute_command(modem_command_wrapper_func_t command,
                                void * arg, uint16_t * error_code);
typedef at_req_status_t (*modem_command_wrapper_side_effect_func_t)(
    void *, side_effect_t *, uint16_t *);
static result_t execute_command_with_side_effect(
    modem_command_wrapper_side_effect_func_t command, void * arg,
    uint16_t * error_code);
static result_t check_at_status(at_req_status_t at_status,
                                side_effect_t * side_effect,
                                uint16_t *      retry_count);

/* Timeout Functions */
static uint32_t update_reboot_timeout(
    cell_phy_randomisation_cfg_t * random_settings);
static uint32_t get_next_reboot_timeout(
    cell_phy_randomisation_cfg_t * random_settings);
static void     reset_reboot_counter(void);
static uint32_t get_timeout_ms(cell_phy_randomisation_cfg_t * random_settings,
                               uint16_t                       retry_count);

/* AT Command Wrappers */
static at_req_status_t turn_off_echo_mode_wrapper(void *     arg,
                                                  uint16_t * error_code);
static at_req_status_t get_sim_readiness_wrapper(void *     arg,
                                                 uint16_t * error_code);
static at_req_status_t sim_enter_pin_wrapper(void *          arg,
                                             side_effect_t * side_effect_arg,
                                             uint16_t *      error_code);
static at_req_status_t get_manufacturer_id_wrapper(void *     arg,
                                                   uint16_t * error_code);
static at_req_status_t get_model_id_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t get_modem_version_wrapper(void *     arg,
                                                 uint16_t * error_code);
static at_req_status_t set_error_display_format_wrapper(void *     arg,
                                                        uint16_t * error_code);
static at_req_status_t set_registration_urcs_format_wrapper(
    void * arg, uint16_t * error_code);
static at_req_status_t setup_modem_specific_feature_wrapper(
    void * arg, uint16_t * error_code);
static at_req_status_t query_pdp_settings_wrapper(void *     arg,
                                                  uint16_t * error_code);
static at_req_status_t query_rat_settings_wrapper(void *     arg,
                                                  uint16_t * error_code);
static at_req_status_t query_service_domain_wrapper(void *     arg,
                                                    uint16_t * error_code);
static at_req_status_t config_pdp_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t config_operator_wrapper(void *     arg,
                                               uint16_t * error_code);
static at_req_status_t config_rat_wrapper(void *          arg,
                                          side_effect_t * side_effect_arg,
                                          uint16_t *      error_code);
static at_req_status_t config_service_domain_wrapper(void *     arg,
                                                     uint16_t * error_code);
static at_req_status_t config_psm_wrapper(void * arg, uint16_t * error_code);
static at_req_status_t activate_pdp_context_wrapper(void *     arg,
                                                    uint16_t * error_code);
static at_req_status_t deactivate_pdp_context_wrapper(void *     arg,
                                                      uint16_t * error_code);
static at_req_status_t get_pdp_status_wrapper(void *     arg,
                                              uint16_t * error_code);
static at_req_status_t query_operator_info_wrapper(void *     arg,
                                                   uint16_t * error_code);
static at_req_status_t query_psm_settings_wrapper(void *     arg,
                                                  uint16_t * error_code);
static at_req_status_t check_ue_functionality_wrapper(void *     arg,
                                                      uint16_t * error_code);
static at_req_status_t set_ue_functionality_wrapper(void *     arg,
                                                    uint16_t * error_code);
static at_req_status_t modem_power_off_software_wrapper(void *     arg,
                                                        uint16_t * error_code);
static at_req_status_t reset_modem_software_wrapper(void *     arg,
                                                    uint16_t * error_code);
static at_req_status_t get_eps_registration_status_wrapper(
    void * arg, uint16_t * error_code);

static at_req_status_t get_gprs_registration_status_wrapper(
    void * arg, uint16_t * error_code);

/* External Library Wrappers */
static uint32_t random_int_wrapper(uint32_t max_val);

/*******************************************************
 * Public Function Bodies                              *
 *******************************************************/

bool
connection_init (void)
{
    cell_os_err_t ret
        = cell_os_timer_create(&g_timer_id, connection_timer_callback);
    if (CELL_OS_SUCCESS != ret)
    {
        cell_log_verbose("connection -- create connection timer failed");
    }
    else
    {
        cell_log_verbose("connection -- connection timer created successfully");
    }
    return (CELL_OS_SUCCESS == ret);
}

void
connection_process (message_t * msg)
{
    static cell_settings_t settings = {
        .valid = false,
    };

    action_t action = ACTION_INVALID;

    // check incoming event
    switch (msg->sub_type)
    {
        case EVENT_CONNECT:
        {
            action = check_event_CONNECT(msg, g_status, &settings);
        }
        break;

        case EVENT_URC:
        {
            action = check_event_URC(msg, g_status);
        }
        break;

        case EVENT_TIMER_EXPIRED:
        {
            action = check_event_TIMER(msg, g_status);
        }
        break;

        default:
            // TIS will raise an error if reaches this case
            //@ assert UB: \false;
            return;
    }

    if (ACTION_INVALID != action)
    {
        g_status = perform_action(action, &settings);
    }
}

void
connection_notify_action (cell_context_t * const context,
                          cell_settings_t *      cell_settings)
{
    send_msg_with_data(MESSAGE_CONNECT,
                       EVENT_CONNECT,
                       cell_settings,
                       sizeof(cell_settings_t),
                       context,
                       false);
}

void
connection_notify_urc (connection_urc_type_t urc_type)
{
    bool need_to_process = true;

    switch (urc_type)
    {
        case CONNECTION_URC_NOT_REGISTERING_IDLE:
        case CONNECTION_URC_NOT_REGISTERING_UNKNOWN:
            if (g_status < STATUS_REGISTERING)
            {
                need_to_process = false;
            }
            break;

        case CONNECTION_URC_PDP_DEACTIVATED:
            notify_all_context_on_modem_event(
                CELL_MODEM_EVENT_PDP_CONTEXT_DESTROYED);
            if (g_status < STATUS_CONNECTED)
            {
                need_to_process = false;
            }
            break;

        default:
            break;
    }

    /**
     * @note If the DFOTA procedure is running, we should bypass the URC
     * notification because the DFOTA procedure needs to run until it returns
     * the exit code (SUCCESS or FAILURE).
     */
    if (need_to_process && !dfota_running_state_check())
    {
        cell_log_verbose(
            "connection -- notify urc = %d, state = %d", urc_type, g_status);
        send_msg_with_value(MESSAGE_CONNECT, EVENT_URC, urc_type, NULL, false);
    }
}

void
connection_notify_timer_expired (void)
{
    send_msg_with_value(MESSAGE_CONNECT, EVENT_TIMER_EXPIRED, 0, NULL, false);
}

bool
connection_is_ready (void)
{
    return (g_status == STATUS_CONNECTED);
}

/*******************************************************
 * Private Function Bodies                             *
 *******************************************************/

/**
 * @brief Choose the starting step of the connection procedure:
 *  - ACTION_REBOOT if requested in the settings
 *  - ACTION_INIT if requested in the settings, or if initialization hasn't been
 * performed yet
 *  - ACTION_CHECK_CONFIG otherwise (i.e. the initialization has already been
 * performed and cell_connect was called again)
 *
 * @param msg Message containing request information
 * @param status Status of the connection procedure
 * @param current_settings Settings currenly used by the procedure
 * @return Action to be handled by `perform_action`
 */
static action_t
check_event_CONNECT (message_t * msg, status_t status,
                     cell_settings_t * current_settings)
{
    action_t action = ACTION_INVALID;

    if ((NULL != msg->data.ptr) && (is_settings_valid(msg->data.ptr)))
    {
        cell_settings_t * new_settings = (cell_settings_t *) msg->data.ptr;

        // new request for the connection procedure
        action = check_reset_request(new_settings);
        if (ACTION_INVALID == action)
        {
            need_config_t reconfig = {0};

            // no need to forcefully reset
            if (need_to_re_config(new_settings, current_settings, &reconfig))
            {
                // new settings need to be applied
                cell_log_verbose("connection -- new settings available");
                memcpy(current_settings, new_settings, sizeof(cell_settings_t));

                if (STATUS_UNINIT == status)
                {
                    action = ACTION_INIT;
                }
                else
                {
                    action = ACTION_CHECK_CONFIG;
                }
            }
        }
        else
        {
            cell_log_verbose("connection -- reset requested");
        }

        if (ACTION_INIT == action || ACTION_CONFIG == action)
        {
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_SW_RESET);
        }
    }

    return action;
}

/**
 * @brief Choose the action to perform depending on the received URC and the
 * connection status
 *
 * @param msg Message containing URC information
 * @param status Current status of modem
 * @return Action to be handled by `perform_action`
 */
static action_t
check_event_URC (message_t * msg, status_t status)
{
    action_t              action   = ACTION_INVALID;

    connection_urc_type_t urc_type = (connection_urc_type_t) msg->data.value;
    cell_log_verbose(
        "connection -- received urc = %d, status = %d", urc_type, status);

    switch (urc_type)
    {
        case CONNECTION_URC_REGISTERED:
            if (STATUS_REGISTERING == status)
            {
                action = ACTION_ACTIVATE_PDP;
                notify_all_context_on_modem_event(
                    CELL_MODEM_EVENT_NETWORK_REGISTERED);
                stop_timer();
            }
            break;

        case CONNECTION_URC_NOT_REGISTERING_UNKNOWN:
            if (STATUS_REGISTERING <= status)
            {
                action = ACTION_INIT;
            }
            break;

        case CONNECTION_URC_SIM_NOT_INSERTED:
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_SIMCARD_FAILED);
            if (STATUS_REGISTERING <= status)
            {
                action = ACTION_REBOOT;
            }
            break;

        case CONNECTION_URC_SIM_NOT_READY:
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_SIMCARD_FAILED);
            if (STATUS_REGISTERING <= status)
            {
                action = ACTION_INIT;
            }
            break;

        case CONNECTION_URC_SIM_READY:
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_SIMCARD_SUCCESS);
            if (STATUS_WAITING_FOR_SIM_READY == status)
            {
                action = ACTION_PREPARE_SIM;
                stop_timer();
            }
            break;

        case CONNECTION_URC_REJECTED:
            if (STATUS_REGISTERING == status)
            {
                action = ACTION_HANDLE_REJECTION;
            }
            break;

        case CONNECTION_URC_PDP_DEACTIVATED:
            if (STATUS_CONNECTED == status)
            {
                action = ACTION_ACTIVATE_PDP;
            }
            break;

        default:
            break;
    }

    return action;
}

/**
 * @brief Get the next action up on the timer expired event
 *
 * @param msg Message containing timer expiration information
 * @param status Current status of the connection procedure
 * @return Action to be handled by `perform_action`
 */
static action_t
check_event_TIMER (message_t * msg, status_t status)
{
    action_t action = ACTION_INVALID;

    switch (status)
    {
        case STATUS_WAITING_FOR_SIM_READY:
            cell_log_verbose("connection -- waiting for SIM ready "
                             "timer expired");
            action = ACTION_PREPARE_SIM;
            break;

        case STATUS_REGISTERING:
            cell_log_verbose("connection -- registering "
                             "timer expired");
            action = ACTION_CONFIG;
            break;

        default:
            break;
    }

    return action;
}

/**
 * @brief Perform a sequence of action starting from `start_action`. The next
 * action in the sequence is determined by the following principle: (status,
 * next_action) = (previous_action + result).
 *
 * @note The sequence will only stop under some specific cases, when `status`
 * will be updated to:
 *  - `STATUS_WAITING_FOR_SIM_READY`: when modem is waiting for SIM ready URC.
 * This behaviour depends on specific modem; not all modems require waiting for
 * a URC for SIM Ready.
 *  - `STATUS_REGISTERING`: when modem need to wait for registration URCs
 *  - `STATUS_CONNECTED`: when modem is successfully connected to the network
 *
 * @param start_action Starting action to be performed
 * @param settings Current settings used by the procedure
 * @return Status of the connection procedure
 */
static status_t
perform_action (action_t start_action, cell_settings_t * settings)
{
    action_t      action     = start_action;
    status_t      status     = STATUS_UNINIT;
    result_t      result     = RESULT_NOT_PERFORMED;
    uint16_t      error_code = 0;

    need_config_t reconfig;
    memset(&reconfig, 0, sizeof(reconfig));

#ifdef __TRUSTINSOFT_ANALYZER__
    // Using loop to store and use the local variable reconfig in
    // ACTION_CHECK_CONFIG and ACTION_CONFIG
    uint8_t loop_count = 2;
    action             = tis_interval(ACTION_INVALID, ACTION_LEAVE_AND_WAIT);
    while ((ACTION_LEAVE_AND_WAIT != action) && (ACTION_INVALID != action)
           && loop_count--)
#else
    while ((ACTION_LEAVE_AND_WAIT != action) && (ACTION_INVALID != action))
#endif /* ifdef __TRUSTINSOFT_ANALYZER__ */
    {
        cell_log_verbose("connection -- action = %d", action);

        switch (action)
        {
            case ACTION_INIT:
            {
                result = initialize(settings, &error_code);
                action = next_action_INIT(result, error_code, settings);
            }
            break;

            case ACTION_LOAD_MODEM_DRIVER:
            {
                result = load_modem_driver(&error_code);
                action = next_action_LOAD_MODEM_DRIVER(result, error_code);
            }
            break;

            case ACTION_SETUP_MODEM:
            {
                result = setup_modem(settings, &error_code);
                action = next_action_SETUP_MODEM(result, error_code);
            }
            break;

            case ACTION_CHECK_CONFIG:
            {
                cell_settings_t current_settings;
                memset(&current_settings, 0, sizeof(current_settings));
                result = read_config(
                    settings->pdp.context_id, &current_settings, &error_code);
                action = next_action_CHECK_CONFIG(
                    result, error_code, &current_settings, settings, &reconfig);
            }
            break;

            case ACTION_CONFIG:
            {
                result = config(settings, &reconfig, &error_code);
                action = next_action_CONFIG(result, error_code);
            }
            break;

            case ACTION_PREPARE_SIM:
            {
                result = prepare_simcard(settings, &error_code);
                action = next_action_PREPARE_SIMCARD(result, error_code);
                if (action == ACTION_LEAVE_AND_WAIT)
                {
                    stop_timer();
                    start_timer(URC_SIM_READY_WAIT_TIME_SEC * 1000);

                    status = STATUS_WAITING_FOR_SIM_READY;
                }
            }
            break;

            case ACTION_START_REGISTRATION:
            {
                cell_network_reg_status_t reg_status
                    = CELL_REGISTRATION_STATUS_MAX;
                result = start_registration(settings, &reg_status, &error_code);
                action = next_action_START_REGISTRATION(
                    result, reg_status, error_code);
                if (action == ACTION_LEAVE_AND_WAIT)
                {
                    stop_timer();
                    start_timer(URC_REGISTRATION_WAIT_TIME_SEC * 1000);

                    status = STATUS_REGISTERING;
                }
            }
            break;

            case ACTION_ACTIVATE_PDP:
            {
                result = activate_pdp_context(settings->pdp.context_id,
                                              &error_code);

                action = next_action_ACTIVATE_PDP(result, error_code);
            }
            break;

            case ACTION_READ_NETWORK_STATUS:
            {
                result = read_network_status(settings->pdp.context_id,
                                             &error_code);

                action = next_action_READ_NETWORK_STATUS(result, error_code);
            }
            break;

            case ACTION_READ_APPLIED_CONFIG:
            {
                cell_settings_t applied_settings;
                memset(&applied_settings, 0, sizeof(applied_settings));

                result = read_config(
                    settings->pdp.context_id, &applied_settings, &error_code);

                if (RESULT_SUCCESS == result)
                {
                    action = ACTION_LEAVE_AND_WAIT;
                    status = STATUS_CONNECTED;
                    reset_reboot_counter();
                    notify_all_context_on_modem_event(
                        CELL_MODEM_EVENT_PDP_CONTEXT_OPENED);
                }
                else
                {
                    action = next_action_DEFAULT(result, error_code);
                }
            }
            break;

            case ACTION_REBOOT:
            {
                notify_all_context_on_modem_event(CELL_MODEM_EVENT_HW_RESET);
                reboot_modem(&settings->phy_randomisations);
                action = ACTION_INIT;
            }
            break;

            case ACTION_HANDLE_REJECTION:
            {
                result = handle_rejection(&error_code);
                action = next_action_HANDLE_REJECTION(result, error_code);
            }
            break;

            case ACTION_SEND_CUSTOM_AT_LIST:
            {
                send_custom_at_list(&settings->custom_at_list);
                status = STATUS_CONNECTED;
                reset_reboot_counter();
                notify_all_context_on_modem_event(
                    CELL_MODEM_EVENT_PDP_CONTEXT_OPENED);
            }
            break;

            default:
                break;
        }
    }

    return status;
}

/**
 * @brief Choose the first action of 'perform_action', matching the reset
 * request in 'settings'
 *
 * @param settings Pointer to the setting structure
 * @return Action to be handled by `perform_action`
 */
static action_t
check_reset_request (cell_settings_t * settings)
{
    action_t action = ACTION_INVALID;

    switch (settings->reset_type)
    {
        case CELL_MODEM_RESET_SOFT:
            cell_log_normal("connection -- modem soft reset requested");
            action = ACTION_INIT;
            break;

        case CELL_MODEM_RESET_HARD:
            cell_log_normal("connection -- modem hard reset requested");
            action = ACTION_REBOOT;
            break;

        default:
            break;
    }

    return action;
}

/**
 * @brief Checks & prepares the key components of an LTE stack:
 *   - Powering up modem if needed
 *   - Checking modem ready status
 *   - Setting AT echo mode
 *
 * @param settings Settings given by the application
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
initialize (cell_settings_t * settings, uint16_t * error_code)
{
    result_t result = RESULT_NOT_PERFORMED;

    // delay before initialize
    uint32_t timeout = update_reboot_timeout(&settings->phy_randomisations);
    cell_log_verbose("connection -- initialize after %d ms, retry %d",
                     timeout,
                     g_reboot_count);
    cell_os_sleep_ms(timeout);

    if (!is_modem_ready(CHECK_MODEM_QUICK_RETRY_TIMES))
    {
        power_modem_up_down_gpio();
        result = check_again_with_other_baudrates();
    }
    else
    {
        result = RESULT_SUCCESS;
    }

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(turn_off_echo_mode_wrapper, NULL, error_code);
    }

    return result;
}

static action_t
next_action_INIT (result_t result, uint16_t error_code,
                  cell_settings_t * settings)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        if (settings->custom_at_list.enable)
        {
            next_action = ACTION_SEND_CUSTOM_AT_LIST;
        }
        else
        {
            next_action = ACTION_LOAD_MODEM_DRIVER;
        }
    }
    return next_action;
}

/**
 * @brief Send a custom AT list
 *
 * @param at_list Customized AT list
 */
static void
send_custom_at_list (cell_custom_at_list_t * at_list)
{
}

/**
 * @brief Load the modem driver based on the modem information
 *
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
load_modem_driver (uint16_t * error_code)
{
    result_t          result     = RESULT_NOT_PERFORMED;

    cell_modem_info_t modem_info = {0};

    result                       = execute_command(
        get_manufacturer_id_wrapper, &modem_info.manufacturer, error_code);

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(
            get_model_id_wrapper, &modem_info.model_id, error_code);
    }

    modem_type_t modem_type = common_get_modem_type(&modem_info);

    if (modem_type != MODEM_TYPE_INVALID)
    {
        cell_log_verbose("connection -- modem type = %d", modem_type);
        gp_modem_driver = common_get_modem_driver(modem_type);
    }
    else
    {
        cell_log_normal("connection -- unable to detect modem type");
        result = RESULT_ERR_UNSUPPORTED;
    }

    return result;
}

static action_t
next_action_LOAD_MODEM_DRIVER (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    switch (result)
    {
        case RESULT_SUCCESS:
            next_action = ACTION_SETUP_MODEM;
            break;

        case RESULT_ERR_UNSUPPORTED:
            next_action = ACTION_LEAVE_AND_WAIT;
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_INIT_FAILED);
            break;

        default:
            break;
    }
    return next_action;
}

/**
 * @brief Setup modem features:
 *   - Gets modem general information
 *   - Sets the error display format
 *   - Sets the registraion URC format
 *   - Sets modem-specific features
 *
 * @param settings Settings given by the application
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
setup_modem (cell_settings_t * settings, uint16_t * error_code)
{
    result_t          result     = RESULT_NOT_PERFORMED;

    cell_modem_info_t modem_info = {0};
    result                       = execute_command(
        get_modem_version_wrapper, &modem_info.fw_version, error_code);

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(
            set_error_display_format_wrapper, NULL, error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(
            set_registration_urcs_format_wrapper, &settings->rat, error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(
            setup_modem_specific_feature_wrapper, NULL, error_code);
    }

    return result;
}

static action_t
next_action_SETUP_MODEM (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        next_action = ACTION_CHECK_CONFIG;
    }
    return next_action;
}

/**
 * @brief Reads all the corresponding configuration related in the configuration
 * phase
 *
 * @param context_id PDP context ID given by the application
 * @param current_settings Currently applied modem settings
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
read_config (uint32_t context_id, cell_settings_t * current_settings,
             uint16_t * error_code)
{
    result_t result                  = RESULT_NOT_PERFORMED;
    current_settings->pdp.context_id = context_id;

    result                           = execute_command(
        query_pdp_settings_wrapper, &current_settings->pdp, error_code);

    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- current pdp settings: context = %d, "
                         "type = %d, apn = %s",
                         current_settings->pdp.context_id,
                         current_settings->pdp.type,
                         current_settings->pdp.apn_name);
        result = execute_command(
            query_rat_settings_wrapper, &current_settings->rat, error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- current rat settings:\n"
                         "sequence = %d -> %d -> %d\n",
                         current_settings->rat.sequence[0],
                         current_settings->rat.sequence[1],
                         current_settings->rat.sequence[2]);
        result = execute_command(query_service_domain_wrapper,
                                 &current_settings->service,
                                 error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- current service domain settings: %d",
                         current_settings->service);
    }

    return result;
}

static action_t
next_action_CHECK_CONFIG (result_t result, uint16_t error_code,
                          cell_settings_t * current, cell_settings_t * expected,
                          need_config_t * reconfig)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        if (need_to_re_config(expected, current, reconfig))
        {
            next_action = ACTION_CONFIG;
        }
        else
        {
            next_action = ACTION_PREPARE_SIM;
        }
    }
    return next_action;
}

/**
 * @brief Compare band settings for a specific RAT.
 *
 * Compares the band configuration associated with the given RAT type
 * between the expected and current configurations.
 *
 * @param exp Expected RAT configuration.
 * @param cur Current RAT configuration.
 * @param rat RAT type whose band configuration is to be compared.
 *
 * @return true if the band settings differ.
 * @return false if the band settings are identical or the RAT type is
 *         not supported for band comparison.
 */
static bool
is_band_different (const cell_rat_settings_t * exp,
                   const cell_rat_settings_t * cur, cell_rat_type_t rat)
{
    switch (rat)
    {
        case CELL_RAT_CATM1:
            return memcmp(exp->catm1_band,
                          cur->catm1_band,
                          sizeof(exp->catm1_band))
                   != 0;

        case CELL_RAT_NBIOT:
            return memcmp(exp->nbiot_band,
                          cur->nbiot_band,
                          sizeof(exp->nbiot_band))
                   != 0;

        case CELL_RAT_GSM:
            return memcmp(exp->gsm_band, cur->gsm_band, sizeof(exp->gsm_band))
                   != 0;

        case CELL_RAT_LTE:
            return memcmp(exp->lte_band, cur->lte_band, sizeof(exp->lte_band))
                   != 0;

        default:
            return false;
    }
}

/**
 * @brief Compare two RAT configurations.
 *
 * Comparison behavior depends on whether the modem supports RAT priority.
 *
 * If RAT priority is not supported:
 * - Only the first RAT entry (sequence[0]) is compared.
 * - The corresponding band configuration is compared.
 *
 * If RAT priority is supported:
 * - The RAT sequence is compared entry by entry.
 * - For each valid RAT, the corresponding band configuration is compared.
 * - Comparison stops when CELL_RAT_INVALID is reached.
 *
 * @param exp Expected RAT configuration.
 * @param cur Current RAT configuration.
 *
 * @return true if any RAT sequence or band configuration mismatch is
 *         detected.
 * @return false if both configurations are identical.
 */
static bool
is_rat_different (const cell_rat_settings_t * exp,
                  const cell_rat_settings_t * cur)
{
    if (gp_modem_driver != NULL)
    {
        /* Modem does not support RAT priority */
        if (!gp_modem_driver->rat_priority_supported)
        {
            if (exp->sequence[0] != cur->sequence[0])
            {
                return true;
            }

            return is_band_different(exp, cur, exp->sequence[0]);
        }

        /* Modem supports RAT priority */
        for (int i = 0; i < CELL_RAT_MAX; i++)
        {
            cell_rat_type_t rat = exp->sequence[i];

            if (rat == CELL_RAT_INVALID)
            {
                return false;
            }

            if (rat != cur->sequence[i])
            {
                return true;
            }

            if (is_band_different(exp, cur, rat))
            {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Determine whether modem reconfiguration is required
 *
 * Compares expected settings with current settings and updates
 * the corresponding flags for:
 * - RAT
 * - PDP
 * - Service domain
 * - Power Saving Mode (PSM)
 *
 * @param[in] expected Settings given by the application
 * @param[in] current Currently applied settings
 * @param[out] reconfig pointer to structure containing reconfiguration
 * flags
 * @return true At least one configuration differs, false No difference
 * detected
 */
static bool
need_to_re_config (cell_settings_t * expected, cell_settings_t * current,
                   need_config_t * reconfig)
{
    memset(reconfig, 0, sizeof(*reconfig));

    // RAT
    reconfig->rat = is_rat_different(&expected->rat, &current->rat);

    // PDP
    if ((strcmp(expected->pdp.apn_name, current->pdp.apn_name) != 0)
        || (strncmp(expected->pdp.username,
                    current->pdp.username,
                    CELL_PDP_USERNAME_MAX_SIZE)
            != 0)
        || (strncmp(expected->pdp.password,
                    current->pdp.password,
                    CELL_PDP_PASSWORD_MAX_SIZE)
            != 0)
        || (expected->pdp.auth_type != current->pdp.auth_type))
    {
        reconfig->pdp = true;
    }

    // Service
    if (expected->service != current->service)
    {
        reconfig->service = true;
    }
    // PSM
    if (CELL_PSM_MODE_ON == expected->psm.mode)
    {
        reconfig->psm = true;
    }

    cell_log_verbose(
        "check reconfig: pdp = %d, rat = %d, service = %d, psm = %d",
        reconfig->pdp,
        reconfig->rat,
        reconfig->service,
        reconfig->psm);

    return (reconfig->rat || reconfig->pdp || reconfig->service
            || reconfig->psm);
}

/**
 * @brief Apply modem configuration based on detected differences
 * Each configuration step is executed only if:
 * - Previous step succeeded
 * - The corresponding diff flag is set
 *
 * @param[in] settings    Desired configuration settings
 * @param[in] reconfig      Pointer to detected differences
 * @param[out] side_effect   Side effects generated during configuration
 * execution. In particular, @p side_effect.trigger_reboot is set when a
 * successful RAT configuration causes the modem to reboot immediately after the
 * configuration command completes. In such a case, no further configuration
 * steps are applied.
 * @param[out] error_code Error code parsed from the response of AT command
 *
 * @return Result of this action. RESULT_SUCCESS if configuration completed
 * successfully or if a reboot is required after RAT configuration. Otherwise,
 * an error result describing the failure.
 */
static result_t
config (cell_settings_t * settings, need_config_t * reconfig,
        uint16_t * error_code)
{
    result_t result = RESULT_NOT_PERFORMED;

    result          = disable_functionality(error_code);

    if (RESULT_SUCCESS == result && reconfig->pdp)
    {
        result
            = execute_command(config_pdp_wrapper, &settings->pdp, error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        result = execute_command(
            config_operator_wrapper, &settings->carrier, error_code);
    }

    if (RESULT_SUCCESS == result && reconfig->rat)
    {
        result = execute_command_with_side_effect(
            config_rat_wrapper, &settings->rat, error_code);
    }

    if (RESULT_SUCCESS == result && reconfig->service)
    {
        result = execute_command(
            config_service_domain_wrapper, &settings->service, error_code);
    }

    if (RESULT_SUCCESS == result && reconfig->psm)
    {
        result
            = execute_command(config_psm_wrapper, &settings->psm, error_code);
    }

    return result;
}

static action_t
next_action_CONFIG (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);
    if (RESULT_SUCCESS == result)
    {
        next_action = ACTION_PREPARE_SIM;
    }
    return next_action;
}

/**
 * @brief Prepare simcard: Check whether SIM is ready and enter PIN if
 * needed
 *
 * @param settings Settings given by the application
 * @param side_effect Output parameter to retrieve the side_effect value
 * from the modem-specific command.
 * @param error_code Error code parsed from the response of AT command
 * @return result_t Result of this action
 */
static result_t
prepare_simcard (cell_settings_t * settings, uint16_t * error_code)
{
    result_t               result          = RESULT_NOT_PERFORMED;
    cell_sim_lock_status_t sim_lock_status = CELL_SIM_CARD_READY;

    result                                 = execute_command(
        get_sim_readiness_wrapper, &sim_lock_status, error_code);

    if (RESULT_SUCCESS == result)
    {
        switch (sim_lock_status)
        {
            case CELL_SIM_CARD_READY:
                result = RESULT_SUCCESS;
                break;

            case CELL_SIM_CARD_PIN:
                result = execute_command_with_side_effect(
                    sim_enter_pin_wrapper, &settings->simcard, error_code);
                break;

            default:
                result = RESULT_ERR_UNSUPPORTED;
                break;
        }
    }

    return result;
}

static action_t
next_action_PREPARE_SIMCARD (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);
    if (RESULT_SUCCESS == result)
    {
        {
            next_action = ACTION_START_REGISTRATION;
        }
    }
    return next_action;
}

static bool
is_rat_supports (const cell_rat_settings_t * rat, cell_rat_type_t target)
{

    for (uint32_t i = 0; i < CELL_RAT_MAX; i++)
    {
        if (rat->sequence[i] == target)
        {
            return true;
        }
    }

    return false;
}

static bool
is_lte_rat (const cell_rat_settings_t * rat)
{
    return is_rat_supports(rat, CELL_RAT_CATM1)
           || is_rat_supports(rat, CELL_RAT_NBIOT)
           || is_rat_supports(rat, CELL_RAT_LTE);
}

static bool
is_gsm_rat (const cell_rat_settings_t * rat)
{
    return is_rat_supports(rat, CELL_RAT_GSM);
}

/**
 * @brief Start the registration process
 *
 * @param settings Settings of the connection procedure
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
start_registration (cell_settings_t *           settings,
                    cell_network_reg_status_t * reg_stat, uint16_t * error_code)
{
    result_t result = RESULT_NOT_PERFORMED;

    result          = enable_functionality(error_code);

    if (RESULT_SUCCESS == result)
    {
        result = check_operator_status(error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        result = get_registration_status(&settings->rat, reg_stat, error_code);
    }

    return result;
}

static bool
is_registered (cell_network_reg_status_t reg_stat)
{
    return ((reg_stat == CELL_REGISTRATION_STATUS_REGISTERED_HOME)
            || (reg_stat == CELL_REGISTRATION_STATUS_REGISTERED_ROAMING));
}

/**
 * @brief Aggregate EPS and GPRS registration states into a single registration
 * state. This registration state aims to expose to the cell_connect.
 *
 * When multiple RATs are configured, the modem may report independent
 * registration states for EPS (CEREG) and GPRS (CGREG). This function
 * converts those states into one common registration state for the
 * application layer.
 *
 * The aggregation follows the priority below:
 *   1. REGISTERED / ROAMING (is_registered() returns true)
 *   2. SEARCHING
 *   3. NOT_REGISTERED
 *   4. DENIED
 *   5. UNKNOWN
 *
 * @param[in]  eps_reg_stat
 *     EPS registration state obtained from CEREG?.
 *
 * @param[in]  gprs_reg_stat
 *     GPRS registration state obtained from CGREG?.
 *
 * @return Aggregated registration state exposed to the cell_connect.
 */
static cell_network_reg_status_t
aggregate_ps_registration_status (cell_network_reg_status_t eps_reg_stat,
                                  cell_network_reg_status_t gprs_reg_stat)
{
    /**
     * 1st priority: REGISTERED / ROAMING
     */
    if (is_registered(eps_reg_stat))
    {
        return eps_reg_stat;
    }

    if (is_registered(gprs_reg_stat))
    {
        return gprs_reg_stat;
    }

    /**
     * 2nd priority: SEARCHING
     */
    if (eps_reg_stat == CELL_REGISTRATION_STATUS_SEARCHING
        || gprs_reg_stat == CELL_REGISTRATION_STATUS_SEARCHING)
    {
        return CELL_REGISTRATION_STATUS_SEARCHING;
    }

    /**
     * 3rd priority: NOT_REGISTERED
     */
    if (eps_reg_stat == CELL_REGISTRATION_STATUS_NO_REGISTERED_SEARCHING
        || gprs_reg_stat == CELL_REGISTRATION_STATUS_NO_REGISTERED_SEARCHING)
    {
        return CELL_REGISTRATION_STATUS_NO_REGISTERED_SEARCHING;
    }

    /**
     * 4th priority: DENIED
     */
    if (eps_reg_stat == CELL_REGISTRATION_STATUS_DENIED
        || gprs_reg_stat == CELL_REGISTRATION_STATUS_DENIED)
    {
        return CELL_REGISTRATION_STATUS_DENIED;
    }

    /**
     * 5th priority: UNKNOWN
     */
    return CELL_REGISTRATION_STATUS_UNKNOWN;
}

static result_t
get_registration_status (const cell_rat_settings_t * rat,
                         cell_network_reg_status_t * reg_stat,
                         uint16_t *                  error_code)
{
    bool use_lte = is_lte_rat(rat);
    bool use_gsm = is_gsm_rat(rat);

    if (!use_lte && !use_gsm)
    {
        return RESULT_NOT_PERFORMED;
    }

    result_t                  result_eps    = RESULT_NOT_PERFORMED;
    result_t                  result_gprs   = RESULT_NOT_PERFORMED;
    cell_network_reg_status_t eps_reg_stat  = CELL_REGISTRATION_STATUS_MAX;
    cell_network_reg_status_t gprs_reg_stat = CELL_REGISTRATION_STATUS_MAX;

    // if use LTE -> get status
    if (use_lte)
    {
        result_eps = execute_command(
            get_eps_registration_status_wrapper, &eps_reg_stat, error_code);
        if (result_eps == RESULT_SUCCESS)
        {
            *reg_stat = eps_reg_stat;
        }
    }

    // if use GPRS -> get status
    if (use_gsm)
    {
        result_gprs = execute_command(
            get_gprs_registration_status_wrapper, &gprs_reg_stat, error_code);
        if (result_gprs == RESULT_SUCCESS)
        {
            *reg_stat = gprs_reg_stat;
        }
    }

    // if use both RATs -> aggregate succesful results
    if (use_lte && use_gsm && (result_eps == RESULT_SUCCESS)
        && (result_gprs == RESULT_SUCCESS))
    {
        *reg_stat
            = aggregate_ps_registration_status(eps_reg_stat, gprs_reg_stat);
    }

    return ((result_eps == RESULT_SUCCESS) || (result_gprs == RESULT_SUCCESS))
               ? RESULT_SUCCESS
               : RESULT_ERR_TIMEOUT;
}

static action_t
next_action_START_REGISTRATION (result_t                  result,
                                cell_network_reg_status_t reg_status,
                                uint16_t                  error_code)
{
    action_t next_act = next_action_DEFAULT(result, error_code);
    if (RESULT_SUCCESS == result)
    {
        if (is_registered(reg_status))
        {
            notify_all_context_on_modem_event(
                CELL_MODEM_EVENT_NETWORK_REGISTERED);
            next_act = ACTION_ACTIVATE_PDP;
        }
        else
        {
            next_act = ACTION_LEAVE_AND_WAIT;
        }
    }
    return next_act;
}

/**
 * @brief Check whether the PDP context is already activated. If not,
 * activate the PDP context.
 *
 * @param context_id ID of the PDP context to be activated
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
activate_pdp_context (uint8_t context_id, uint16_t * error_code)
{
    result_t            result = RESULT_NOT_PERFORMED;

    cell_network_info_t network_info;
    memset(&network_info, 0, sizeof(network_info));

    result = execute_command(get_pdp_status_wrapper, &network_info, error_code);

    if ((RESULT_SUCCESS == result)
        && (CELL_PDP_CONTEXT_STATE_DEACTIVATED == network_info.state))
    {
        cell_log_verbose("connection -- activate context id %d", context_id);
        result = execute_command(
            activate_pdp_context_wrapper, &context_id, error_code);
    }

    // in case of "ERROR" response, deactive the PDP context and reboot
    if (RESULT_ERR_AT_OTHER_ERROR == result)
    {
        result = execute_command(
            deactivate_pdp_context_wrapper, &context_id, error_code);

        if (RESULT_SUCCESS == result)
        {
            result = RESULT_ERR_AT_OTHER_ERROR;
        }
    }

    return result;
}

static action_t
next_action_ACTIVATE_PDP (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        next_action = ACTION_READ_NETWORK_STATUS;
    }
    return next_action;
}

/**
 * @brief After modem is registered on the network:
 *   - Opens the PDP context to the Internet.
 *   - Read the IP, operator and PSM settings.
 *
 * @param context_id IP of the activated PDP Context
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
read_network_status (uint8_t context_id, uint16_t * error_code)
{
    result_t            result = RESULT_NOT_PERFORMED;

    cell_network_info_t network_info;
    memset(&network_info, 0, sizeof(network_info));
    result = execute_command(get_pdp_status_wrapper, &network_info, error_code);

    cell_operator_info_t operator;
    memset(&operator, 0, sizeof(operator));
    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- IP = %d.%d.%d.%d",
                         network_info.ipv4.addr[0],
                         network_info.ipv4.addr[1],
                         network_info.ipv4.addr[2],
                         network_info.ipv4.addr[3]);
        result = execute_command(
            query_operator_info_wrapper, &operator, error_code);
    }

    cell_psm_settings_t applied_psm_settings;
    memset(&applied_psm_settings, 0, sizeof(applied_psm_settings));
    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- operator = %s", operator.name);
        result = execute_command(
            query_psm_settings_wrapper, &applied_psm_settings, error_code);
    }

    if (RESULT_SUCCESS == result)
    {
        cell_log_verbose("connection -- PSM applied = %d",
                         applied_psm_settings.mode);
    }

    return result;
}

static action_t
next_action_READ_NETWORK_STATUS (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        next_action = ACTION_READ_APPLIED_CONFIG;
    }
    return next_action;
}

/**
 * @brief Reboot modem reboots or power off/on the modem based on the
 * requested waiting time between reboot cycles
 *   - Fast reboot will wait first then reboot using software & reset
 * command
 *   - Slow reboot will first power down the modem, wait for longer time and
 * power it up again (in initialize phase)
 *
 * @param random_cfg Randomisation config for the init/reboot action (given
 * by the application)
 */
static void
reboot_modem (cell_phy_randomisation_cfg_t * random_cfg)
{
    uint32_t timeout_ms = get_next_reboot_timeout(random_cfg);

    if (is_fast_reboot(timeout_ms))
    {
        cell_log_verbose("connection -- fast rebooting after %d ms",
                         timeout_ms);
        cell_os_sleep_ms(timeout_ms);
        fast_reboot();
    }
    else
    {
        cell_log_verbose("connection -- slow rebooting after %d ms",
                         timeout_ms);
        slow_reboot();
    }
}

/**
 * @brief Handle rejection
 *
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
handle_rejection (uint16_t * error_code)
{
    result_t result = RESULT_NOT_PERFORMED;

    return result;
}

static action_t
next_action_HANDLE_REJECTION (result_t result, uint16_t error_code)
{
    action_t next_action = next_action_DEFAULT(result, error_code);

    if (RESULT_SUCCESS == result)
    {
        next_action = ACTION_CONFIG;
    }

    return next_action;
}

/**
 * @brief Get the next action for some default results
 *
 * @param result Result to be processed
 * @param error_code Error code parsed from the response of AT command
 * @return Next action to perform
 */
static action_t
next_action_DEFAULT (result_t result, uint16_t error_code)
{
    action_t next_action = ACTION_INVALID;

    switch (result)
    {
        case RESULT_REBOOT_TRIGGERED:
            next_action = ACTION_INIT;
            break;
        case RESULT_URC_PENDING:
            next_action = ACTION_LEAVE_AND_WAIT;
            break;
        case RESULT_ERR_TIMEOUT:
            next_action = ACTION_REBOOT;
            break;

        case RESULT_ERR_CME:
        {
            cme_error_t cme_err = CME_ERR_INVALID;
            if ((NULL != gp_modem_driver)
                && (NULL != gp_modem_driver->map_cme_error))
            {
                //@ assert gp_modem_driver->map_cme_error != \null;
                cme_err = gp_modem_driver->map_cme_error(error_code);
            }
            next_action = handle_CME_error(cme_err);
        }
        break;

        case RESULT_ERR_CMS:
        {
            cms_error_t cms_err = CMS_ERR_INVALID;
            if ((NULL != gp_modem_driver)
                && (NULL != gp_modem_driver->map_cms_error))
            {
                //@ assert gp_modem_driver->map_cms_error != \null;
                cms_err = gp_modem_driver->map_cms_error(error_code);
            }
            next_action = handle_CMS_error(cms_err);
        }
        break;

        case RESULT_ERR_AT_OTHER_ERROR:
            next_action = ACTION_REBOOT;
            break;

        default:
            break;
    }

    return next_action;
}

/**
 * @brief Handle the CME errors
 *
 * @param cme_err CME error to be handled
 * @return Next action to perform
 */
static action_t
handle_CME_error (cme_error_t cme_err)
{
    action_t action = ACTION_INIT;
    switch (cme_err)
    {
        case CME_ERR_SIM_NOT_READY:
        case CME_ERR_SIM_NOT_INSERTED:
        {
            notify_all_context_on_modem_event(CELL_MODEM_EVENT_SIMCARD_FAILED);
            action = ACTION_REBOOT;
        }
        break;

        default:
            break;
    }
    return action;
}

/**
 * @brief Handle the CMS errors
 *
 * @param cms_err CMS error to be handled
 * @return Next action to perform
 */
static action_t
handle_CMS_error (cms_error_t cms_err)
{
    action_t action = ACTION_INIT;
    return action;
}

/**
 * @brief Start the one-shot timer
 *
 * @param milliseconds Timeout duration in milliseconds
 */
static void
start_timer (uint32_t milliseconds)
{
    cell_os_timer_start(g_timer_id, milliseconds);
}

/**
 * @brief Stop the one-shot timer
 *
 * @param milliseconds Timeout duration in milliseconds
 */
static void
stop_timer (void)
{
    cell_os_timer_stop(g_timer_id);
}

/**
 * @brief One-shot timer callback for the connection process
 *
 * @param timer_id Timer ID created in `connection_init` function
 */
static void
connection_timer_callback (void * arg)
{
    connection_notify_timer_expired();
}

/**
 * @brief Check if settings is valid
 *
 * @param settings Pointer to settings to be checked
 * @return `true` if valid, `false` if invalid
 */
static bool
is_settings_valid (cell_settings_t * settings)
{
    return (settings->valid);
}

/**
 * @brief Check the operator status
 *     - if the status is de-register, force register the operator using
 * AUTO mode
 *
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
check_operator_status (uint16_t * error_code)
{
    result_t             result = RESULT_NOT_PERFORMED;

    cell_operator_info_t operator_info;
    memset(&operator_info, 0, sizeof(operator_info));

    result = execute_command(
        query_operator_info_wrapper, &operator_info, error_code);

    if ((RESULT_SUCCESS == result)
        && (CELL_OPERATOR_SELECTION_MODE_DEREGISTER_MANUAL
            == operator_info.mode))
    {
        cell_operator_settings_t operator_settings;
        memset(&operator_settings, 0, sizeof(operator_settings));
        operator_settings.selection_mode = CELL_OPERATOR_SELECTION_MODE_AUTO;
        operator_settings.format_type    = CELL_OPERATOR_NAME_FORMAT_LONG;

        result                           = execute_command(
            config_operator_wrapper, &operator_settings, error_code);
    }

    return result;
}

/**
 * @brief Enable modem full functionality if not
 *
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
enable_functionality (uint16_t * error_code)
{
    result_t                result = RESULT_NOT_PERFORMED;
    cell_ue_functionality_t state  = CELL_UE_MINIMUM_FUNCTIONALITY;

    result
        = execute_command(check_ue_functionality_wrapper, &state, error_code);

    if (RESULT_SUCCESS == result && CELL_UE_FULL_FUNCTIONALITY != state)
    {
        state = CELL_UE_FULL_FUNCTIONALITY;
        result
            = execute_command(set_ue_functionality_wrapper, &state, error_code);

        if (RESULT_SUCCESS == result)
        {
            // wait for modem to be fully
            // enabled
            cell_os_sleep_ms(UE_FULL_FUNCTIONALITY_WAIT_MS);
        }
    }

    return result;
}

/**
 * @brief Disable modem full functionality (to minimum functionality)
 *
 * @param error_code Error code parsed from the response of AT command
 */
static result_t
disable_functionality (uint16_t * error_code)
{
    cell_ue_functionality_t state = CELL_UE_MINIMUM_FUNCTIONALITY;
    return execute_command(set_ue_functionality_wrapper, &state, error_code);
}

/**
 * @brief Check the condition for a fast reboot
 *
 * @param timeout_ms The timeout needed to wait for this reboot
 * @return TRUE if yes, FALSE if no
 */
static bool
is_fast_reboot (uint32_t timeout_ms)
{
    return (timeout_ms <= (FAST_REBOOT_MAXIMUM_SEC * 1000));
}

/**
 * @brief Perform slow reboot
 */
static void
slow_reboot (void)
{
    result_t result     = RESULT_NOT_PERFORMED;

    uint16_t error_code = 0;
    result
        = execute_command(modem_power_off_software_wrapper, NULL, &error_code);

    if (RESULT_SUCCESS != result)
    {
        power_modem_up_down_gpio();
    }
}

/**
 * @brief Perform fast reboot
 */
static void
fast_reboot (void)
{
    result_t result     = RESULT_NOT_PERFORMED;

    uint16_t error_code = 0;
    result = execute_command(reset_modem_software_wrapper, NULL, &error_code);

    if (RESULT_SUCCESS != result)
    {
        reset_modem_gpio();
    }
}

/**
 * @brief Power up (down) modem using GPIO
 */
static void
power_modem_up_down_gpio (void)
{
    cell_com_set_power_pin(false);
    cell_os_sleep_ms(1000);
    cell_com_set_power_pin(true);
}

/**
 * @brief Reset modem using GPIO
 */
static void
reset_modem_gpio (void)
{
    cell_com_set_power_pin(true);
    cell_os_sleep_ms(1000);
    cell_com_set_power_pin(false);
}

/**
 * @brief Check if modem was ready
 *     - Send "AT" command to check modem readiness
 *
 * @param retry_times Number of retries in sending `AT` commands
 * @return `true` if modem is ready, `false` otherwise
 */
static bool
is_modem_ready (int retry_times)
{
    bool ret = false;

    for (int index = 0; index < retry_times; index++)
    {
        uint16_t        error_code = 0;
        at_req_status_t at_status  = AT_PENDING;
        at_status                  = common_at_readiness_check(&error_code);
        if (AT_SUCCESS == at_status)
        {
            cell_log_verbose("connection -- modem is ready");
            ret = true;
            break;
        }
        cell_log_verbose("connection -- modem is not ready");
        cell_os_sleep_ms(CHECK_MODEM_INTERVAL_MS);
    }

    return ret;
}

/**
 * @brief Check if modem was ready
 *     - Send "AT" command to check modem readiness
 *     - If modem is not ready, change the baudrate and try again
 *     - If fail after trying all the baudrates, then reboot the modem
 *
 * @return Result of this action
 */
static result_t
check_again_with_other_baudrates (void)
{
    result_t  result      = RESULT_ERR_TIMEOUT;
    const int baudrates[] = {9600, 115200};

    for (int index = 0; index < sizeof(baudrates) / sizeof(baudrates[0]);
         index++)
    {
        if (is_modem_ready(CHECK_MODEM_SLOW_RETRY_TIMES))
        {
            result = RESULT_SUCCESS;
            break;
        }
        cell_com_set_baudrate(baudrates[index]);
    }

    return result;
}

/**
 * @brief Execute AT commands and handle errors
 *
 * @notes This function provides a default process to execute AT commands
 * and handle errors:
 *   - For timeout error: retry COMMAND_TIMEOUT_RETRIES_MAX times before
 * rebooting
 *   - For generic error:  can be handled specifically
 *
 * @param command Modem command wrapper function
 * @param arg Input argument for the modem command
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
execute_command (modem_command_wrapper_func_t command, void * arg,
                 uint16_t * error_code)
{
    result_t result      = RESULT_NOT_PERFORMED;
    uint16_t retry_count = 0;

    do
    {
        at_req_status_t at_status = command(arg, error_code);

        result = check_at_status(at_status, NULL, &retry_count);
    }
    while (RESULT_NOT_PERFORMED == result);

    return result;
}

/**
 * @brief Execute AT commands and handle errors with additional side effect.
 *
 * @details This function is similar to `execute_command`, but it allows
 * passing an additional argument (@p side_effect_arg) to the modem command
 * wrapper function. This can be useful for commands that require retrieving
 * side effects from the modem during execution.
 *
 * @notes This function provides a default process to execute AT commands
 * and handle errors:
 *   - For timeout error: retry COMMAND_TIMEOUT_RETRIES_MAX times before
 * rebooting
 *   - For generic error:  can be handled specifically
 *
 * @param command Modem command wrapper function
 * @param arg Input argument for the modem command
 * @param side_effect_arg Side effect argument for the modem command,
 * retrieving additional information from the modem during command execution
 * @param error_code Error code parsed from the response of AT command
 * @return Result of this action
 */
static result_t
execute_command_with_side_effect (
    modem_command_wrapper_side_effect_func_t command, void * arg,
    uint16_t * error_code)
{
    result_t      result      = RESULT_NOT_PERFORMED;
    uint16_t      retry_count = 0;
    side_effect_t side_effect = {0};

    do
    {
        at_req_status_t at_status = command(arg, &side_effect, error_code);

        result = check_at_status(at_status, &side_effect, &retry_count);
    }
    while (RESULT_NOT_PERFORMED == result);

    return result;
}

/**
 * @brief Check the AT command status and return the corresponding result.
 *
 * @details In the special case that the @p at_status indicates a timeout
 * (AT_TIMEOUT), this function will perform the following actions if the
 * maximum number of retries is NOT reached:
 * - Sleep for a calculated timeout
 * - Increment the @p retry_count retry counter .
 * - Return `RESULT_NOT_PERFORMED` to indicate that the command should be
 * retried. Caller should check the result and decide whether to retry.
 * If the maximum number of retries is reached, this function will return
 * `RESULT_ERR_TIMEOUT`.
 *
 * @param at_status AT command status
 * @param retry_count Current retry counter
 * @return Result of this action
 */
static result_t
check_at_status (at_req_status_t at_status, side_effect_t * side_effect,
                 uint16_t * retry_count)
{
    result_t result = RESULT_NOT_PERFORMED;
    switch (at_status)
    {
        case AT_ERROR_CME:
            result = RESULT_ERR_CME;
            break;

        case AT_ERROR_CMS:
            result = RESULT_ERR_CMS;
            break;

        case AT_ERROR_GENERIC:
            result = RESULT_ERR_AT_OTHER_ERROR;
            break;

        case AT_TIMEOUT:
        {
            uint32_t timeout = get_timeout_ms(&g_command_retry_timeout_settings,
                                              *retry_count);

            (*retry_count)++;
            if (g_command_retry_timeout_settings.number_of_retries
                == *retry_count)
            {
                cell_log_verbose("connection -- AT command timeout, "
                                 "exceeded maximum retries");
                result = RESULT_ERR_TIMEOUT;
            }
            else
            {
                cell_log_verbose(
                    "connection -- AT command timeout, retry after %d ms",
                    timeout);
                cell_os_sleep_ms(timeout);
                result = RESULT_NOT_PERFORMED;
            }
        }
        break;

        case AT_SUCCESS:
            result = RESULT_SUCCESS;
            if (side_effect != NULL)
            {
                if (side_effect->trigger_reboot)
                {
                    result = RESULT_REBOOT_TRIGGERED;
                }
                else if (side_effect->need_to_wait_urc)
                {
                    result = RESULT_URC_PENDING;
                }
            }
            break;

        default:
            // TIS will raise an error if this line is reachable
            // @assert UB: false;
            break;
    }

    return result;
}

/**
 * @brief Calculate the reboot timeout and update the counter
 *
 * @param random_settings Random settings
 * @return `0` if exceeds maximum retries, timeout value if valid
 */
static uint32_t
update_reboot_timeout (cell_phy_randomisation_cfg_t * random_settings)
{
    uint32_t random_time_ms = 0;
    random_time_ms          = get_next_reboot_timeout(random_settings);
    if (g_reboot_count < random_settings->number_of_retries)
    {
        g_reboot_count++;
    }
    return random_time_ms;
}

/**
 * @brief Calculate the next reboot timeout
 *
 * @param random_settings Random settings
 * @return `0` if exceeds maximum retries, timeout value if valid
 */
static uint32_t
get_next_reboot_timeout (cell_phy_randomisation_cfg_t * random_settings)
{
    uint32_t random_time_ms = 0;
    random_time_ms          = get_timeout_ms(random_settings, g_reboot_count);
    return random_time_ms;
}

/**
 * @brief Reset the reboot counter
 */
static void
reset_reboot_counter (void)
{
    g_reboot_count = 0;
}

/**
 * @brief Calculate the retry timeout
 *
 * @param random_settings Random settings
 * @param retry_count Current retry counter
 * @return `0` if exceeds maximum retries, timeout value if valid
 */
static uint32_t
get_timeout_ms (cell_phy_randomisation_cfg_t * random_settings,
                uint16_t                       retry_count)
{
    uint32_t random_time_ms = 0;
    random_time_ms
        = random_int_wrapper((uint32_t) 1000
                             * random_settings->randomisation_start_window)
          + (uint32_t) 1000 * random_settings->randomisation_start_window
                * random_settings->multiplication_factor * retry_count;
    return random_time_ms;
}

static at_req_status_t
turn_off_echo_mode_wrapper (void * arg, uint16_t * error_code)
{
    return common_echo_turn_off(error_code);
}

static at_req_status_t
get_sim_readiness_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;

    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->sim_readiness_get))
    {
        ret = gp_modem_driver->sim_readiness_get(arg, error_code);
    }

    return ret;
}

static at_req_status_t
sim_enter_pin_wrapper (void * arg, side_effect_t * side_effect_arg,
                       uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;

    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->sim_pin_enter))
    {
        cell_simcard_settings_t * sim_settings
            = (cell_simcard_settings_t *) arg;
        bool * is_sim_ready_requires_urc = &side_effect_arg->need_to_wait_urc;

        ret                              = gp_modem_driver->sim_pin_enter(
            *sim_settings, is_sim_ready_requires_urc, error_code);
    }

    return ret;
}

static at_req_status_t
get_manufacturer_id_wrapper (void * arg, uint16_t * error_code)
{
    return common_manufacturer_info_get(arg, error_code);
}

static at_req_status_t
get_model_id_wrapper (void * arg, uint16_t * error_code)
{
    return common_model_id_get(arg, error_code);
}

static at_req_status_t
get_modem_version_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL == gp_modem_driver)
        || (NULL == gp_modem_driver->firmware_version_get))
    {
        ret = common_firmware_version_get(arg, error_code);
    }
    else
    {
        //@ assert gp_modem_driver->firmware_version_get != \null;
        ret = gp_modem_driver->firmware_version_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
set_error_display_format_wrapper (void * arg, uint16_t * error_code)
{
    return common_reporting_cme_error_enable(error_code);
}

static at_req_status_t
set_registration_urcs_format_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t             ret = AT_ERROR_GENERIC;
    const cell_rat_settings_t * rat = (const cell_rat_settings_t *) arg;
    if (is_lte_rat(rat))
    {
        ret = common_eps_network_registration_urc_enable(error_code);
    }
    if (is_gsm_rat(rat))
    {
        ret = common_gprs_network_registration_urc_enable(error_code);
    }
    return ret;
}

static at_req_status_t
setup_modem_specific_feature_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->modem_specific_features_set))
    {
        ret = gp_modem_driver->modem_specific_features_set(error_code);
    }
    return ret;
}

static at_req_status_t
query_pdp_settings_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->pdp_settings_get))
    {
        ret = gp_modem_driver->pdp_settings_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
query_rat_settings_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->rat_settings_get))
    {
        ret = gp_modem_driver->rat_settings_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
query_service_domain_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->service_domain_settings_get))
    {
        ret = gp_modem_driver->service_domain_settings_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
config_pdp_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->pdp_config))
    {
        ret = gp_modem_driver->pdp_config(arg, error_code);
    }
    return ret;
}

static at_req_status_t
config_operator_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->operator_config))
    {
        ret = gp_modem_driver->operator_config(arg, error_code);
    }
    return ret;
}

static at_req_status_t
config_rat_wrapper (void * arg, side_effect_t * side_effect_arg,
                    uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->rat_config))
    {
        bool * is_trigger_reboot = &side_effect_arg->trigger_reboot;
        ret = gp_modem_driver->rat_config(arg, is_trigger_reboot, error_code);
    }
    return ret;
}

static at_req_status_t
config_service_domain_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->service_domain_config))
    {
        ret = gp_modem_driver->service_domain_config(arg, error_code);
    }
    return ret;
}

static at_req_status_t
config_psm_wrapper (void * arg, uint16_t * error_code)
{
    return common_psm_config_set(arg, error_code);
}

static at_req_status_t
activate_pdp_context_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->pdp_context_activate))
    {
        ret = gp_modem_driver->pdp_context_activate(*(uint8_t *) arg,
                                                    error_code);
    }
    return ret;
}

static at_req_status_t
deactivate_pdp_context_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->pdp_context_deactivate))
    {
        ret = gp_modem_driver->pdp_context_deactivate(*(uint8_t *) arg,
                                                      error_code);
    }
    return ret;
}

static at_req_status_t
get_pdp_status_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->pdp_status_get))
    {
        ret = gp_modem_driver->pdp_status_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
query_operator_info_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->operator_info_get))
    {
        ret = gp_modem_driver->operator_info_get(arg, error_code);
    }
    return ret;
}

static at_req_status_t
query_psm_settings_wrapper (void * arg, uint16_t * error_code)
{
    return common_psm_config_get(arg, error_code);
}

static at_req_status_t
check_ue_functionality_wrapper (void * arg, uint16_t * error_code)
{
    return common_ue_functionality_get(arg, error_code);
}

static at_req_status_t
set_ue_functionality_wrapper (void * arg, uint16_t * error_code)
{
    return common_ue_functionality_set(*(cell_ue_functionality_t *) arg,
                                       error_code);
}

static at_req_status_t
modem_power_off_software_wrapper (void * arg, uint16_t * error_code)
{
    at_req_status_t ret = AT_ERROR_GENERIC;
    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->power_off_software))
    {
        ret = gp_modem_driver->power_off_software(error_code);
    }
    return ret;
}

static at_req_status_t
reset_modem_software_wrapper (void * arg, uint16_t * error_code)
{
    return common_ue_reset(error_code);
}

static at_req_status_t
get_eps_registration_status_wrapper (void * arg, uint16_t * error_code)
{
    return common_eps_network_reg_status_get((cell_network_reg_status_t *) arg,
                                             error_code);
}

static at_req_status_t
get_gprs_registration_status_wrapper (void * arg, uint16_t * error_code)
{
    return common_gprs_network_reg_status_get((cell_network_reg_status_t *) arg,
                                              error_code);
}

static uint32_t
random_int_wrapper (uint32_t max_val)
{
    uint32_t random_value = 0;
#if (CONFIG_USE_EXTERNAL_RANDOMIZATION == 1)
    random_value = randomRandInt(max_val);
#else
    random_value = max_val;
#endif
    return random_value;
}
