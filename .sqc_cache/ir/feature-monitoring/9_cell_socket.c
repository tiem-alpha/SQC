#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/cellular/src/cell_socket.c"
/**
 * @file cell_socket.c
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
 * @brief Socket related management source file for cell common
 * @date 2025-24-06
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/

#include "cell_socket.h"
#include "cell_internal.h"
#include "cell_modem_common.h"
#include "cell_log.h"
#include "cell_os.h"

/*******************************************************
 * Third-party Header Files                            *
 *******************************************************/

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

/*******************************************************
 * Standard Header Files                               *
 *******************************************************/

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define INVALID_INCOMING_ID 0xCAFE

/**
 * @brief Socket Events
 */
typedef enum
{
    EVENT_CREATE = 0,   /** Create and open socket request */
    EVENT_SEND_DATA,    /** Socket send data request */
    EVENT_READ_DATA,    /** Socket read data request */
    EVENT_CLOSE,        /** Close socket request */
    EVENT_VERIFY_DATA,  /** Verify socket data sent request */
    EVENT_REMOTE_CLOSE, /** Close remote socket */
} event_t;

/*******************************************************
 * Static Data Declarations                            *
 *******************************************************/

static cell_socket_t * gp_socket[MAXIMUM_SUPPORT_SOCKET] = {NULL};

/**
 * @brief Socket timer ID
 */
static uint32_t g_timer_id = 0;

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

/**
 * @brief Create socket
 *
 * @param[in] socket_settings The configuration for the desired socket
 * @param[out] socket The socket to be created
 * @return `CELL_SUCCESS` if the operation is successful, otherwise an error
 * code indicating the cause of the error.
 */
static cell_err_t socket_create(const cell_socket_settings_t * socket_settings,
                                cell_socket_t *                socket);

/**
 * @brief Send data through socket
 *
 * @param[in] socket the socket context to be used
 * @param[in] data Input buffer containing data to be send
 * @param[in/out] length Length of data to be send / Length of sent data
 *                If length = 0, query the sent data
 * @return `CELL_SUCCESS` if the operation is successful, otherwise an error
 * code indicating the cause of the error.
 */
static cell_err_t socket_send(cell_socket_t * socket, const uint8_t * data,
                              size_t * length);

/**
 * @brief Receive data through socket
 *
 * @param[in] socket the socket context to be used
 * @param[out] data Output buffer to hold received data
 * @param[in/out] length Maximum length of output buffer / Length of received
 * data
 * @return `CELL_SUCCESS` if the operation is successful, otherwise an error
 * code indicating the cause of the error.
 */
static cell_err_t socket_receive(cell_socket_t * socket, uint8_t * data,
                                 size_t * length);

/**
 * @brief Close socket
 *
 * @param[in] socket the socket context to be closed
 *
 * @return `CELL_SUCCESS` if the operation is successful, otherwise an error
 * code indicating the cause of the error.
 */
static cell_err_t socket_close(cell_socket_t * socket);

/**
 * @brief In TCP service, verify data of socket send is reach server or not by
 * comparing total sent bytes and total ACK bytes
 *
 * @param[in] socket the socket context to be verified
 * @param[out] data_verify_info Struct stores information for verifying the sent
 * data
 *
 * @return `CELL_SUCCESS` if the operation is successful, otherwise an error
 * code indicating the cause of the error.
 */
static cell_err_t socket_verify_data(
    cell_socket_t * socket, cell_socket_data_verify_info_t * data_verify_info);

/**
 * @brief Find first available socket in the specific pdp context
 *
 * @param[in] context_id the pdp context id
 * @param[out] socket_id the id of available socket
 *
 * @return true if there is a socket available, otherwise false
 */
static bool find_first_available_socket(uint8_t    context_id,
                                        uint32_t * socket_id);

/**
 * @brief Get server socket handle ID based on incoming connection ID
 *
 * @param[in] connect_id the incoming connection ID
 * @param[out] server_id the server ID that incomming ID connect to
 *
 * @return true if server ID is found, otherwise false
 */
static bool get_server_id_from_connect_id(uint32_t   connect_id,
                                          uint32_t * server_id);

/**
 * @brief Close socket remote
 *
 * @param[in] connect_id the remote socket ID to be closed
 *
 */
static void socket_remote_close(uint32_t connect_id);

/**
 * @brief Notify socket remote close event to the modem task
 *
 * @param[in] connect_id the remote socket ID to be closed
 */
static void notify_remote_close(uint32_t connect_id);

/**
 * @brief Close socket incoming
 *
 * @param[in] incoming_id the incoming socket ID to be closed
 * @param[in] type  Type of socket service associated with the socket ID. This
 * value specifies whether the socket is a TCP client, UDP client, TCP server,
 * or UDP service.
 *
 */
static void socket_close_incoming(uint32_t                   incoming_id,
                                  cell_socket_service_type_t type);

/**
 * @brief Check whether a socket service type is a server type.
 *
 * @param[in] service_type Socket service type to be checked.
 *
 * @return true if the service type is a server socket
 * (TCP server or UDP service), otherwise false.
 */
static bool is_socket_service_server(cell_socket_service_type_t service_type);

/**
 * @brief Get socket from modem connection ID.
 *
 * @param[in]  connect_id Modem connection identifier.
 * @param[out] is_server  Set to true if the connection belongs to a server,
 *                        false for a client socket.
 *
 * @return Pointer to the socket structure, or NULL if not found.
 */
static cell_socket_t * get_socket_from_socket_id(uint32_t connect_id,
                                                 bool *   is_server);

/* Inactive Timer Functions */
static void start_inactive_timer(uint32_t milliseconds);
static void stop_inactive_timer(void);
static void reset_inactive_timer(uint32_t milliseconds);
static void inactive_timer_callback(void * arg);

/* Wrapper functions */
static at_req_status_t socket_create_wrapper(
    const cell_socket_settings_t * socket, uint32_t socket_id,
    uint16_t * err_code);
static at_req_status_t socket_send_wrapper(cell_socket_t * socket,
                                           const uint8_t * data,
                                           size_t *        length,
                                           uint16_t *      err_code);
static at_req_status_t socket_sent_info_get_wrapper(
    cell_socket_t * socket, cell_socket_data_verify_info_t * data_verify_info,
    uint16_t * error_code);
static at_req_status_t socket_receive_wrapper(cell_socket_t * socket,
                                              const uint8_t * data,
                                              size_t *        length,
                                              uint16_t *      err_code);
static at_req_status_t socket_close_wrapper(uint32_t socket_id,
                                            cell_socket_service_type_t type,
                                            uint16_t * err_code);
static at_req_status_t socket_check_usability_wrapper(
    all_socket_status_t * socket_state_handle, uint8_t context_id,
    uint16_t * err_code);
/**
 * @brief Wrapper function for notify application about socket event
 */
static void notify_event_wrapper(cell_socket_t *     socket,
                                 cell_socket_event_t event);

/*******************************************************
 * Public Function Bodies                              *
 *******************************************************/
bool
socket_init (void)
{
    cell_os_err_t ret
        = cell_os_timer_create(&g_timer_id, inactive_timer_callback);
    if (CELL_OS_SUCCESS != ret)
    {
        cell_log_verbose("socket -- create socket timer failed");
    }
    else
    {
        cell_log_verbose("socket -- socket timer created successfully");
    }
    return (CELL_OS_SUCCESS == ret);
}

void
socket_notify_create (cell_socket_settings_t * socket_settings,
                      cell_socket_t *          socket)
{
    send_msg_with_data(MESSAGE_SOCKET,
                       EVENT_CREATE,
                       socket_settings,
                       sizeof(cell_socket_settings_t),
                       socket,
                       false);
}

void
socket_notify_send (cell_socket_t * socket, cell_socket_data_info_t * data_info)
{
    send_msg_with_data(MESSAGE_SOCKET,
                       EVENT_SEND_DATA,
                       data_info,
                       sizeof(cell_socket_data_info_t),
                       socket,
                       false);
}

void
socket_notify_recv (cell_socket_t * socket, cell_socket_data_info_t * data_info)
{
    send_msg_with_data(MESSAGE_SOCKET,
                       EVENT_READ_DATA,
                       data_info,
                       sizeof(cell_socket_data_info_t),
                       socket,
                       false);
}

void
socket_notify_close (cell_socket_t * socket)
{
    send_msg_with_data(MESSAGE_SOCKET, EVENT_CLOSE, NULL, 0, socket, false);
}

void
socket_notify_verify_data (cell_socket_t *                  socket,
                           cell_socket_data_verify_info_t * data_verify_info)
{
    send_msg_with_data(MESSAGE_SOCKET,
                       EVENT_VERIFY_DATA,
                       data_verify_info,
                       sizeof(cell_socket_data_verify_info_t),
                       socket,
                       false);
}

void
socket_process (message_t * msg)
{
    // remote close event does not have context, ignore check in this case
    if ((NULL == msg->context) && (EVENT_REMOTE_CLOSE != msg->sub_type))
    {
        cell_log_debug("Invalid socket context");
        return;
    }
    switch (msg->sub_type)
    {
        case EVENT_CREATE:
        {
            if (NULL != msg->data.ptr)
            {
                socket_create((cell_socket_settings_t *) msg->data.ptr,
                              (cell_socket_t *) msg->context);
            }
        }
        break;

        case EVENT_SEND_DATA:
        {
            if (NULL != msg->data.ptr)
            {
                cell_socket_data_info_t * data_info
                    = (cell_socket_data_info_t *) msg->data.ptr;
                socket_send((cell_socket_t *) msg->context,
                            data_info->data,
                            &data_info->length);
            }
        }
        break;

        case EVENT_READ_DATA:
        {
            if (NULL != msg->data.ptr)
            {
                cell_socket_data_info_t * data_info
                    = (cell_socket_data_info_t *) msg->data.ptr;
                socket_receive((cell_socket_t *) msg->context,
                               data_info->data,
                               &data_info->length);
            }
        }
        break;

        case EVENT_CLOSE:
        {
            socket_close((cell_socket_t *) msg->context);
        }
        break;

        case EVENT_VERIFY_DATA:
        {
            cell_socket_data_verify_info_t * data_verify_info
                = (cell_socket_data_verify_info_t *) msg->data.ptr;
            socket_verify_data((cell_socket_t *) msg->context,
                               data_verify_info);
        }
        break;

        case EVENT_REMOTE_CLOSE:
        {
            socket_remote_close(msg->data.value);
        }
        break;

        default:
            break;
    }
}

void
socket_process_urc_opened_success (uint32_t socket_id)
{
#ifdef __TRUSTINSOFT_ANALYZER__
    // Force spliting states to avoid false alarms
    tis_variable_split(&socket_id, sizeof(socket_id), 10000);
#endif
    if (gp_socket[socket_id] != NULL)
    {
        if (is_socket_service_server(gp_socket[socket_id]->service_type))
        {
            gp_socket[socket_id]->incoming_id = INVALID_INCOMING_ID;
            notify_event_wrapper(gp_socket[socket_id],
                                 CELL_SOCKET_EVENT_LISTENING);
        }
        else
        {
            notify_event_wrapper(gp_socket[socket_id],
                                 CELL_SOCKET_EVENT_CONNECTED);
        }
    }
}

void
socket_process_urc_open_failed (uint32_t socket_id)
{
    notify_event_wrapper(gp_socket[socket_id],
                         CELL_SOCKET_EVENT_OPENING_FAILED);
}

void
socket_process_urc_incoming_connection (uint32_t connect_id, uint32_t server_id)
{
#ifdef __TRUSTINSOFT_ANALYZER__
    // Force spliting states to avoid false alarms
    tis_variable_split(&server_id, sizeof(server_id), 10000);
#endif
    if (gp_socket[server_id] != NULL)
    {
        /* There are 2 cases an incoming connection will be acceppted:
           - 1st: There are no incoming connection before
           - 2nd: First incoming connection has been closed by outside factor
           which is not handled by socket context, e.g: Custom AT command send
           AT+QICLOSE
          */
        if ((INVALID_INCOMING_ID == gp_socket[server_id]->incoming_id)
            || (connect_id == gp_socket[server_id]->incoming_id))
        {
            gp_socket[server_id]->incoming_id = connect_id;
            start_inactive_timer(gp_socket[server_id]->timeout * 1000);
            notify_event_wrapper(gp_socket[server_id],
                                 CELL_SOCKET_EVENT_INCOMING_ACCEPTED);
        }
        else
        {
            uint16_t err_code = 0;
            // Reject 2nd incoming connection and close this socket also
            if (AT_SUCCESS
                != socket_close_wrapper(
                    connect_id, gp_socket[server_id]->service_type, &err_code))
            {
                cell_log_debug("Close socket fail err_code = %d", err_code);
            }
        }
    }
}

void
socket_process_urc_incoming_data (uint32_t connect_id)
{
    // If the socket handle of connect_id is existing, it's a TCP/UDP client
    if (gp_socket[connect_id] != NULL)
    {
        notify_event_wrapper(gp_socket[connect_id],
                             CELL_SOCKET_EVENT_DATA_RECEIVED);
    }
    // It's an incomming socket to TCP/UDP server
    else
    {
        uint32_t server_id = 0;

        if (get_server_id_from_connect_id(connect_id, &server_id))
        {
            notify_event_wrapper(gp_socket[server_id],
                                 CELL_SOCKET_EVENT_DATA_RECEIVED);
        }
    }
}

void
socket_process_urc_closed (uint32_t connect_id)
{
    // notify modem task about socket remote close
    notify_remote_close(connect_id);
}

/*******************************************************
 * Private Function Bodies                             *
 *******************************************************/

static cell_err_t
socket_create (const cell_socket_settings_t * socket_settings,
               cell_socket_t *                socket)
{
    cell_err_t status    = CELL_SUCCESS;
    uint32_t   socket_id = 0;

    if (find_first_available_socket(socket_settings->context_id, &socket_id))
    {
        uint16_t err_code = 0;

        if (AT_SUCCESS
            == socket_create_wrapper(socket_settings, socket_id, &err_code))
        {
            socket->context_id   = socket_settings->context_id;
            socket->socket_id    = socket_id;
            socket->type         = socket_settings->type;
            socket->service_type = socket_settings->service_type;
            socket->local        = socket_settings->local;
            socket->remote       = socket_settings->remote;
            socket->timeout      = socket_settings->timeout;
            socket->event_cb     = socket_settings->event_cb;

            gp_socket[socket_id] = socket;
        }
        else
        {
            status = CELL_ERR_GENERIC;
        }
    }
    else
    {
        cell_log_normal("No socket available, fail to create socket");
        status = CELL_ERR_GENERIC;
    }

    return status;
}

static cell_err_t
socket_send (cell_socket_t * socket, const uint8_t * data, size_t * length)
{
    cell_err_t status   = CELL_SUCCESS;
    uint16_t   err_code = 0;

    // For the association, the inactive timer will be reset whenever socket
    // send/reveice data
    if (is_socket_service_server(socket->service_type))
    {
        reset_inactive_timer(socket->timeout * 1000);
    }

    if (AT_SUCCESS != socket_send_wrapper(socket, data, length, &err_code))
    {
        status = CELL_ERR_GENERIC;
        notify_event_wrapper(socket, CELL_SOCKET_EVENT_SENDING_FAILED);
    }
    else
    {
        notify_event_wrapper(socket, CELL_SOCKET_EVENT_SENDING_OK);
    }

    return status;
}

static cell_err_t
socket_receive (cell_socket_t * socket, uint8_t * data, size_t * length)
{
    cell_err_t status   = CELL_SUCCESS;
    uint16_t   err_code = 0;

    // For the association, the inactive timer will be reset whenever socket
    // send/reveice data
    if (is_socket_service_server(socket->service_type))
    {
        reset_inactive_timer(socket->timeout * 1000);
    }

    if (AT_SUCCESS != socket_receive_wrapper(socket, data, length, &err_code))
    {
        status = CELL_ERR_GENERIC;
        notify_event_wrapper(socket, CELL_SOCKET_EVENT_DATA_READING_FAILED);
    }
    else
    {
        notify_event_wrapper(socket, CELL_SOCKET_EVENT_DATA_READING_OK);
    }

    return status;
}

static cell_err_t
socket_close (cell_socket_t * socket)
{
    cell_err_t status   = CELL_SUCCESS;
    uint16_t   err_code = 0;

#ifdef __TRUSTINSOFT_ANALYZER__
    // Force spliting states to avoid false alarms
    tis_variable_split(&socket->socket_id, sizeof(socket->socket_id), 10000);
#endif
    if (gp_socket[socket->socket_id] != NULL)
    {
        // Close the TCP/UDP incomming socket before closing TCP/UDP listener
        if ((gp_socket[socket->socket_id]->incoming_id != INVALID_INCOMING_ID)
            && is_socket_service_server(
                gp_socket[socket->socket_id]->service_type))
        {
            if (gp_socket[socket->socket_id]->incoming_id
                < MAXIMUM_SUPPORT_SOCKET)
            {
                socket_close_incoming(
                    gp_socket[socket->socket_id]->incoming_id,
                    gp_socket[socket->socket_id]->service_type);
            }
        }

        if (AT_SUCCESS
            == socket_close_wrapper(socket->socket_id,
                                    gp_socket[socket->socket_id]->service_type,
                                    &err_code))
        {
            notify_event_wrapper(gp_socket[socket->socket_id],
                                 CELL_SOCKET_EVENT_HOST_CLOSED);
        }
    }
    else
    {
        status = CELL_ERR_GENERIC;
    }

    return status;
}

static void
socket_close_incoming (uint32_t incoming_id, cell_socket_service_type_t type)
{
    uint16_t err_code = 0;
    if (AT_SUCCESS == socket_close_wrapper(incoming_id, type, &err_code))
    {
        stop_inactive_timer();
    }
    else
    {
        cell_log_debug("Failed to close incoming socket with error = %u",
                       err_code);
    }
}

static cell_err_t
socket_verify_data (cell_socket_t *                  socket,
                    cell_socket_data_verify_info_t * data_verify_info)
{
    cell_err_t status   = CELL_SUCCESS;
    uint16_t   err_code = 0;

    if (AT_SUCCESS
        == socket_sent_info_get_wrapper(socket, data_verify_info, &err_code))
    {
        notify_event_wrapper(socket, CELL_SOCKET_EVENT_DATA_VERIFIED);
    }
    else
    {
        status = CELL_ERR_GENERIC;
    }

    return status;
}

static bool
find_first_available_socket (uint8_t context_id, uint32_t * socket_id)
{
    uint16_t            err_code            = 0;
    bool                is_found            = false;

    all_socket_status_t socket_state_handle = {0};

    for (uint8_t i = 0; i < MAXIMUM_SUPPORT_SOCKET; i++)
    {
        socket_state_handle.sockets[i] = false;
    }

    if (AT_SUCCESS
        == socket_check_usability_wrapper(
            &socket_state_handle, context_id, &err_code))
    {
        for (uint8_t i = 0; i < socket_state_handle.no_supported_socket; i++)
        {
            if (socket_state_handle.no_supported_socket
                <= MAXIMUM_SUPPORT_SOCKET)
            {
                if (false == socket_state_handle.sockets[i])
                {
                    *socket_id = i;
                    is_found   = true;
                    break;
                }
            }
        }
    }
    else
    {
        cell_log_debug("Query all socket service status failed");
    }

    return is_found;
}

static bool
get_server_id_from_connect_id (uint32_t connect_id, uint32_t * server_id)
{
    bool found_server_id = false;

    // Seeking incoming connection ID inside TCP/UDP LISTENING socket
    for (int id = 0; id < MAXIMUM_SUPPORT_SOCKET; id++)
    {
        if ((gp_socket[id] != NULL)
            && is_socket_service_server(gp_socket[id]->service_type)
            && (gp_socket[id]->incoming_id == connect_id))
        {
            found_server_id = true;
            *server_id      = id;
            break;
        }
    }

    return found_server_id;
}

static void
socket_remote_close (uint32_t connect_id)
{
    uint16_t        err_code  = 0;
    cell_socket_t * socket    = NULL;
    bool            is_server = false;

    socket = get_socket_from_socket_id(connect_id, &is_server);

    if (socket == NULL)
    {
        return;
    }
    // Close the socket to change socket state to "initial"
    if (AT_SUCCESS
        == socket_close_wrapper(connect_id, socket->service_type, &err_code))
    {
        if (is_server)
        {
            // reset incoming connection ID for accepting next
            // connection
            socket->incoming_id = INVALID_INCOMING_ID;

            stop_inactive_timer();
        }

        notify_event_wrapper(socket, CELL_SOCKET_EVENT_REMOTE_CLOSED);
    }
}

static bool
is_socket_service_server (cell_socket_service_type_t service_type)
{
    return ((CELL_SOCKET_SERVICE_TYPE_TCP_SERVER == service_type)
            || (CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE == service_type));
}

static cell_socket_t *
get_socket_from_socket_id (uint32_t connect_id, bool * is_server)
{
    cell_socket_t * socket = NULL;

    // If the socket handle of connect_id is existing, it's a TCP/UDP client
    if (gp_socket[connect_id] != NULL)
    {
        socket     = gp_socket[connect_id];
        *is_server = false;
    }
    // It's an incomming socket to TCP/UDP server
    else
    {
        uint32_t server_id = 0;
        if (get_server_id_from_connect_id(connect_id, &server_id))
        {
#ifdef __TRUSTINSOFT_ANALYZER__
            // Force spliting states to avoid false alarms
            tis_variable_split(&server_id, sizeof(server_id), 10000);
#endif
            if (gp_socket[server_id] != NULL)
            {
                socket     = gp_socket[server_id];
                *is_server = true;
            }
        }
    }

    return socket;
}

/**
 * @brief Start the one-shot timer
 *
 * @param milliseconds Timeout duration in milliseconds
 */
static void
start_inactive_timer (uint32_t milliseconds)
{
    cell_os_timer_start(g_timer_id, milliseconds);
}

/**
 * @brief Stop the one-shot timer
 */
static void
stop_inactive_timer (void)
{
    cell_os_timer_stop(g_timer_id);
}

/**
 * @brief Reset the inactive timer
 *
 * @param milliseconds Timeout duration in milliseconds
 */
static void
reset_inactive_timer (uint32_t milliseconds)
{
    stop_inactive_timer();
    start_inactive_timer(milliseconds);
}

/**
 * @brief One-shot timer callback for the socket inactive timer
 *
 * @param timer_id Timer ID created in `socket_init` function
 */
static void
inactive_timer_callback (void * arg)
{
    // Seeking incoming connection ID inside TCP/UDP LISTENING socket
    for (int id = 0; id < MAXIMUM_SUPPORT_SOCKET; id++)
    {
        if ((gp_socket[id] != NULL)
            && is_socket_service_server(gp_socket[id]->service_type))
        {
            if (gp_socket[id]->incoming_id != INVALID_INCOMING_ID)
            {
                // notify modem task about socket remote close
                notify_remote_close(gp_socket[id]->incoming_id);
                notify_event_wrapper(gp_socket[id],
                                     CELL_SOCKET_EVENT_INACTIVITY_TIMEOUT);
            }
            break;
        }
    }
}

static void
notify_remote_close (uint32_t connect_id)
{
    send_msg_with_value(
        MESSAGE_SOCKET, EVENT_REMOTE_CLOSE, connect_id, NULL, false);
}

static at_req_status_t
socket_create_wrapper (const cell_socket_settings_t * socket,
                       uint32_t socket_id, uint16_t * err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->socket_create)
        && (NULL != socket->event_cb))
    {
        req_status
            = gp_modem_driver->socket_create(socket, socket_id, err_code);
    }

    return req_status;
}

static at_req_status_t
socket_send_wrapper (cell_socket_t * socket, const uint8_t * data,
                     size_t * length, uint16_t * err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->socket_send))
    {
        req_status
            = gp_modem_driver->socket_send(socket, data, length, err_code);
    }

    return req_status;
}

static at_req_status_t
socket_sent_info_get_wrapper (cell_socket_t *                  socket,
                              cell_socket_data_verify_info_t * data_verify_info,
                              uint16_t *                       err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->socket_sent_info_get))
    {
        req_status = gp_modem_driver->socket_sent_info_get(
            socket, data_verify_info, err_code);
    }

    return req_status;
}

static at_req_status_t
socket_receive_wrapper (cell_socket_t * socket, const uint8_t * data,
                        size_t * length, uint16_t * err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->socket_receive))
    {
        req_status
            = gp_modem_driver->socket_receive(socket, data, length, err_code);
    }

    return req_status;
}

static at_req_status_t
socket_close_wrapper (uint32_t socket_id, cell_socket_service_type_t type,
                      uint16_t * err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver) && (NULL != gp_modem_driver->socket_close))
    {
        req_status = gp_modem_driver->socket_close(socket_id, type, err_code);
    }

    return req_status;
}

static at_req_status_t
socket_check_usability_wrapper (all_socket_status_t * socket_state_handle,
                                uint8_t context_id, uint16_t * err_code)
{
    at_req_status_t req_status = AT_PENDING;

    if ((NULL != gp_modem_driver)
        && (NULL != gp_modem_driver->socket_check_usability))
    {
        req_status = gp_modem_driver->socket_check_usability(
            socket_state_handle, context_id, err_code);
    }

    return req_status;
}

static void
notify_event_wrapper (cell_socket_t * socket, cell_socket_event_t event)
{
    if ((socket != NULL) && (socket->event_cb != NULL))
    {
        socket->event_cb(event);
    }
}
