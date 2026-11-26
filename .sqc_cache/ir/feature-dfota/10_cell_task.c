#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_task.c"
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
 * @brief Implementation of the main task for the cellular module.
 * @date 2025-03-31
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/

#include "cell_task.h"
#include "cell_os.h"
#include "cell_types.h"
#include "cell_connect.h"
#include "cell_internal.h"
#include "cell_log.h"
#include "cell_monitoring.h"
#include "cell_dfota.h"
#include "cell_socket.h"
#include "cell_test.h"
#include "cell_at.h"
#include "cell_com.h"

/*******************************************************
 * Third-party Header Files                            *
 *******************************************************/

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

/*******************************************************
 * Standard Header Files                               *
 *******************************************************/

#include <stdint.h>
#include <string.h>

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define CELL_TASK_MSG_QUEUE_LEN (32)

/*******************************************************
 * Static Data Declarations                            *
 *******************************************************/

/**
 * @brief  Cell queue handler
 */
static cell_os_queue_handle_t gh_cell_task_queue                  = NULL;
static cell_os_task_handle_t  gh_cell_task_handle                 = NULL;
static cell_context_t *       gp_context[MAXIMUM_SUPPORT_CONTEXT] = {NULL};
static uint32_t               g_current_context_idx               = 0;

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

static void read_and_parse_urc(void);
static void process_message(message_t * msg);
static void create_cellular_context(message_t * msg);

/*******************************************************
 * Public Function Bodies                              *
 *******************************************************/

void
cell_task (void * arg)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));

#ifndef __TRUSTINSOFT_ANALYZER__
    // remove loop here since tis_features.c will control this
    for (;;)
#endif
    {
        if (CELL_OS_SUCCESS
            == cell_os_msg_read(gh_cell_task_queue, &msg, QUEUE_TIMEOUT_MAX))
        {
            // Process the received message
            process_message(&msg);
        }

        cell_os_feed_wdg();
    }
}

cell_err_t
cell_startup (void)
{
    cell_err_t ret = CELL_SUCCESS;

    gh_cell_task_queue
        = cell_os_msg_create(CELL_TASK_MSG_QUEUE_LEN, sizeof(message_t));

    if (NULL == gh_cell_task_queue)
    {
        ret = CELL_ERR_GENERIC;
    }
    else
    {
        cell_log_verbose("Cell task queue created successfully");
    }

    gh_cell_task_handle = cell_os_task_create("cell_task",
                                              cell_task,
                                              NULL,
                                              TASK_CMCELL_STACK_SIZE,
                                              TASK_CMCELL_PRIORITY);

    if (NULL == gh_cell_task_handle)
    {
        ret = CELL_ERR_GENERIC;
    }
    else
    {
        cell_log_verbose("Cell task created successfully");
    }

    if (CELL_SUCCESS == ret)
    {
        ret = connection_init() == true ? CELL_SUCCESS : CELL_ERR_GENERIC;
    }

    if (CELL_SUCCESS == ret)
    {
        ret = dfota_init() == true ? CELL_SUCCESS : CELL_ERR_GENERIC;
    }

    if (CELL_SUCCESS == ret)
    {
        ret = socket_init() == true ? CELL_SUCCESS : CELL_ERR_GENERIC;
    }

    return ret;
}

void
cell_com_notify_task_on_rxne (void)
{
    send_msg_with_data(MESSAGE_DATA_INCOMING, 0, NULL, 0, NULL, true);
}

void
notify_all_context_on_modem_event (cell_modem_event_t modem_event)
{
    if (0 == g_current_context_idx)
    {
        return;
    }

    if (g_current_context_idx >= MAXIMUM_SUPPORT_CONTEXT)
    {
        g_current_context_idx = MAXIMUM_SUPPORT_CONTEXT - 1;
    }

    for (uint32_t index = 0; index < g_current_context_idx; index++)
    {
        if ((gp_context[index] != NULL)
            && (gp_context[index]->modem_cb != NULL))
        {
            cell_log_verbose("notifing context %d", index);
            gp_context[index]->modem_cb(modem_event);
        }
    }
}

void
send_msg_with_data (message_type_t msg_type, uint8_t sub_type, void * data,
                    uint16_t size, void * ctx, bool is_in_isr)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.context  = ctx;
    msg.size     = size;
    msg.data.ptr = data;
    msg.type     = msg_type;
    msg.sub_type = sub_type;

    if (!is_in_isr)
    {
        cell_os_msg_write(gh_cell_task_queue, &msg, sizeof(message_t), 0);
    }
    else
    {
        cell_os_msg_write_from_isr(gh_cell_task_queue, &msg, sizeof(message_t));
    }
}

void
send_msg_with_value (message_type_t msg_type, uint8_t sub_type, uint32_t value,
                     cell_context_t * ctx, bool is_in_isr)
{
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.context    = ctx;
    msg.size       = sizeof(value);
    msg.data.value = value;
    msg.type       = msg_type;
    msg.sub_type   = sub_type;

    if (!is_in_isr)
    {
        cell_os_msg_write(gh_cell_task_queue, &msg, sizeof(message_t), 0);
    }
    else
    {
        cell_os_msg_write_from_isr(gh_cell_task_queue, &msg, sizeof(message_t));
    }
}

/*******************************************************
 * Private Function Bodies                             *
 *******************************************************/

/**
 * @brief Reads and parses an unsolicited result code (URC) from the cellular
 * module.
 *
 * This function is responsible for reading data from the cellular module and
 * parsing it to identify and handle any unsolicited result codes (URCs) that
 * may have been received. URCs are typically sent by the module to notify the
 * host of events such as network status changes, incoming messages, or other
 * asynchronous events.
 */
static void
read_and_parse_urc (void)
{
    process_remaining_data();
}

/**
 * @brief Processes a given cellular message.
 *
 * This function handles the processing of a cellular message
 * passed to it. The specific behavior and processing logic
 * depend on the implementation details of the function.
 *
 * @param[in] msg Pointer to the cell_os_msg_t structure containing
 *                the message to be processed.
 */
static void
process_message (message_t * msg)
{
    // Disable RXNE interrupt before processing the message
    cell_com_enable_rxne_irq(false);

    // Process the message based on its type
    switch (msg->type)
    {
        case MESSAGE_DATA_INCOMING:
            read_and_parse_urc();
            break;

        case MESSAGE_CONTEXT_CREATION:
            create_cellular_context(msg);
            break;

        case MESSAGE_CONNECT:
            connection_process(msg);
            break;

        case MESSAGE_MONITORING:
            monitoring_process(msg);
            break;

        case MESSAGE_DFOTA:
            dfota_process(msg);
            break;

        case MESSAGE_SOCKET:
            socket_process(msg);
            break;

        case MESSAGE_CUSTOM_AT_CMD:
            cell_test_process(msg);
            break;

        default:
            // TIS will raise issue if this case is reached
            //@ assert UB: \false;
            break;
    }

    if (cell_com_is_char_available())
    {
        process_remaining_data();
    }

    // Re-enable RXNE interrupt after completing message processing
    cell_com_enable_rxne_irq(true);
}

/**
 * @brief Create the cell context from the application's request.
 *
 * @param msg Message sent from the application to request opening the context.
 */
static void
create_cellular_context (message_t * msg)
{
    cell_context_event_t      result           = CELL_CONTEXT_FAILURE;
    cell_context_settings_t * context_settings = NULL;
    cell_context_t *          context          = NULL;

    if ((NULL == msg->data.ptr) || (NULL == msg->context)
        || (msg->size != sizeof(cell_context_settings_t)))
    {
        cell_log_normal("Fail to create context");
        return;
    }

    context_settings = (cell_context_settings_t *) msg->data.ptr;
    context          = (cell_context_t *) msg->context;

    if (context_settings->create_cb == NULL)
    {
        cell_log_normal("Callback missing in async phase");
        return;
    }

    if (g_current_context_idx < MAXIMUM_SUPPORT_CONTEXT)
    {
        context->modem_cb                 = context_settings->modem_cb;
        context->get_cb                   = context_settings->get_cb;
        context->dfota_cb                 = context_settings->dfota_cb;
        context->test_cb                  = context_settings->test_cb;
        context->id                       = g_current_context_idx;

        gp_context[g_current_context_idx] = context;

        cell_log_normal("Context (%u) was created successfully",
                        g_current_context_idx);

        g_current_context_idx++;

        result = CELL_CONTEXT_SUCCESS;
    }
    else
    {
        result = CELL_CONTEXT_ERR_OUT_OF_CONTEXT;
    }

    /*
     * Notify the application whether the context was created successfully.
     */
    context_settings->create_cb(result);
}
