#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_at.c"
/**
 * @file cell_at.c
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
 * @brief Standard AT commands for cellular modem
 * @date 2025-03-31
 */
#include "cell_at.h"
#include "cell_com.h"
#include "cell_urc.h"

#include "cell_log.h"
#include "cell_os.h"

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

#include <stdbool.h>
#include <string.h>

#define AT_CMD_RESPONSE_POLL_INTERVAL_MS 3

static const char *   success_tokens[] = {"OK", "CONNECT", "SEND OK", ">"};
static const uint32_t success_token_table_size
    = sizeof(success_tokens) / sizeof(success_tokens[0]);
static const char * error_tokens[]
    = {"ERROR", "+CMS ERROR", "+CME ERROR", "SEND FAIL"};
static const uint32_t error_token_table_size
    = sizeof(error_tokens) / sizeof(error_tokens[0]);

/**
 * @brief Determines the AT response type based on the provided line.
 *
 * This function analyzes the given line of text and determines the type
 * of AT response it represents. The response type is used to interpret
 * the modem's reply to AT commands.
 *
 * @param[in] at_req Pointer to the AT command request structure.
 * @param[in] p_line  Pointer to the line of text to be analyzed.
 *                This is typically a response line from the modem.
 *
 * @return The type of AT response as an enumeration value of type
 *         `at_resp_type_t`.
 */
static at_resp_type_t
cell_get_at_resp_type (at_request_t * at_req, const char * p_line)
{
    at_resp_type_t at_resp_type = AT_UNDEFINED;

    if (true == check_urc_token_without_prefix(p_line))
    {
        at_resp_type = AT_UNSOLICITED;
    }
    else if (true == is_prefix_present(p_line))
    {
        if ((true == does_prefix_match(p_line, at_req->resp_prefix)))
        {
            at_resp_type = AT_SOLICITED;
        }
        else
        {
            at_resp_type = AT_UNSOLICITED;
        }
    }
    else
    {
        at_resp_type = AT_SOLICITED;
    }

    return at_resp_type;
}

/**
 * @brief Parses the final response line and updates the request status and
 * error code.
 *
 * This function checks if the given line is a final response. If it is, it
 * updates the request status and error code based on the content of the line.
 *
 * @param[in] line The response line to be parsed.
 * @param[out] at_req_status Pointer to the request status to be updated.
 * @param[out] error_code Pointer to the error code to be updated if an error is
 * found.
 * @return true if the line is a final response, false otherwise.
 */
static bool
parse_final_resp (char * line, at_req_status_t * at_req_status,
                  uint16_t * error_code)
{
    bool is_final_resp     = false;
    bool final_resp_status = false;

    if (is_line_final_resp(line, &final_resp_status))
    {
        if (true == final_resp_status)
        {
            *at_req_status = AT_SUCCESS;
        }
        else
        {
            if (0 == memcmp(line, "+CMS ERROR:", 11))
            {
                *at_req_status = AT_ERROR_CMS;
                sscanf(line, "+CMS ERROR: %hu", error_code);
            }
            else if (0 == memcmp(line, "+CME ERROR:", 11))
            {
                *at_req_status = AT_ERROR_CME;
                sscanf(line, "+CME ERROR: %hu", error_code);
            }
            else if (0 == memcmp(line, "ERROR", 5))
            {
                *at_req_status = AT_ERROR_GENERIC;
            }
            else
            {
                cell_log_debug("Parser error line: %s is not supported\r\n",
                               line);
            }
        }

        is_final_resp = true;
    }
    else
    {
        is_final_resp = false;
    }

    return is_final_resp;
}

/**
 * @brief Retrieves the first solicited line from the AT command response.
 *
 * This function reads lines from the AT command response until it finds a
 * solicited response or the timeout is reached.
 *
 * @param[in] at_req Pointer to the AT request structure.
 * @param[out] line Buffer to store the first solicited line.
 * @param[in] length Buffer length store the first solicited line.
 * @param[in] start_time Start time to wait for an AT response.
 * @param[in] timeout Maximum time to wait for a solicited response, in
 * milliseconds.
 * @param[in] at_parser_handler Function pointer to the parser handler that
 *                   processes the specific AT response
 *
 * @return AT_PENDING: if line is an intermediate line or a success final
 *                     response line will be processed in at_parser_handler.
 *         AT_TIMEOUT: if the timeout is reached,
 *         Or other error status: AT_ERROR_GENERIC, AT_ERROR_CME, AT_ERROR_CMS
 */
static at_req_status_t
at_get_first_solicited_line (at_request_t * at_req, char * line,
                             uint16_t length, uint64_t start_time,
                             uint32_t            timeout,
                             at_parser_handler_t at_parser_handler)
{
    at_req_status_t ret = AT_TIMEOUT;

    while (cell_os_get_time_ms() - start_time < timeout)
    {
        if (false == at_read_line(line, length, start_time, timeout))
        {
            ret = AT_TIMEOUT;
            break;
        }

        // TIS: because at_read_line has been proven, the assertion below
        // is surely valid
        //@ assert line[length-1] == '\0';
        at_resp_type_t resp_type = cell_get_at_resp_type(at_req, line);

        if (AT_UNSOLICITED == resp_type)
        {
            urc_parse_unsolicited_response(line);
        }
        else if (AT_SOLICITED == resp_type)
        {
            if (false == parse_final_resp(line, &ret, at_req->p_error_code))
            {
                // If line is not final response
                ret = AT_PENDING; // to call at_parser_handler
            }
            else
            {
                // if line is success final response
                if (AT_SUCCESS == ret)
                {
                    // if AT command expecting process the response line
                    if (NULL != at_parser_handler)
                    {
                        ret = AT_PENDING; // to call at_parser_handler
                    }
                }
            }
            break;
        }
        else
        {
            // TIS will raise an error if this line is reachable
            //@ assert UB: \false;
        }

        cell_os_sleep_ms(AT_CMD_RESPONSE_POLL_INTERVAL_MS);
    }

    sqc_assert((ret == AT_TIMEOUT) || (strlen(line) >= 0));

    return ret;
}

at_req_status_t
at_command_request (at_request_t * at_req, uint32_t timeout,
                    at_parser_handler_t at_parser_handler)
{
    at_req_status_t ret                   = AT_SUCCESS;
    char            line[AT_MAX_LINE_LEN] = {0};
    uint64_t        start_time            = 0;

    // Process any remaining data in the buffer to avoid interference with the
    // upcoming AT command response
    process_remaining_data();

    cell_com_send_buffer((const char *) at_req->p_send_buff,
                         at_req->send_buff_len);

    // Get start time for waiting an AT response
    start_time = cell_os_get_time_ms();
    ret        = at_get_first_solicited_line(
        at_req, line, sizeof(line), start_time, timeout, at_parser_handler);

    if (AT_PENDING == ret)
    {
        if (NULL != at_parser_handler)
        {
            // TIS: because at_get_first_solicited_line returning a valid string
            // has been proven, the assertion below is surely valid
            //@ assert (line[AT_MAX_LINE_LEN-1] == '\0');
            ret = at_parser_handler(at_req, line, start_time, timeout);
        }
        else
        {
            // In this case AT request is not
            // expected any data response
            ret = seek_and_parse_final_resp(
                line, start_time, timeout, at_req->p_error_code);
        }
    }

    return ret;
}

bool
check_urc_token_without_prefix (const char * line)
{
    bool ret = false;
    ret      = urc_is_without_prefix(line);
    return ret;
}

bool
is_prefix_present (const char * line)
{
    const char * colon_pos = strchr(line, ':');
    if (colon_pos != NULL)
    {
        // Check if the prefix contains a space
        for (const char * ptr = line; ptr < colon_pos; ++ptr)
        {
            if (*ptr == ' ')
            {
                /* Example "+CME ERROR: 5" will not be considered as prefix
                   present
                */
                return false;
            }
        }
        return true;
    }
    return false;
}

bool
does_prefix_match (const char * line, const char * prefix)
{
    bool ret = false;

    if ((NULL != prefix) && (NULL != line))
    {
        uint16_t prefix_len = strlen(prefix);

        if (0 == strncmp(line, prefix, prefix_len))
        {
            char next_char = line[prefix_len];
            if ((':' == next_char) || (' ' == next_char) || ('\0' == next_char))
            {
                ret = true;
            }
        }
    }

    return ret;
}

bool
check_final_tokens (const char * p_line, const char * const * token_table,
                    uint32_t token_table_size)
{
    bool ret = false;

    for (uint32_t i = 0; i < token_table_size; i++)
    {
        if (0 == strncmp(p_line, token_table[i], strlen(token_table[i])))
        {
            ret = true;
            break;
        }
    }

    return ret;
}

bool
is_line_final_resp (const char * p_line, bool * final_resp_status)
{
    bool ret = false;

    // Check for success tokens
    bool is_success_token
        = check_final_tokens(p_line, success_tokens, success_token_table_size);

    if (true == is_success_token)
    {
        ret                = true;
        *final_resp_status = true;
    }
    else
    {
        // Check for error tokens
        bool is_error_token
            = check_final_tokens(p_line, error_tokens, error_token_table_size);

        if (true == is_error_token)
        {
            ret                = true;
            *final_resp_status = false;
        }
    }

    return ret;
}

bool
at_read_line (char * line, uint16_t max_len, uint64_t start_time,
              uint32_t timeout_ms)
{
    bool     line_ready       = false;
    uint32_t total_read_bytes = 0;

    while (cell_com_is_char_available()
           || (cell_os_get_time_ms() - start_time < timeout_ms))
    {
#ifdef __TRUSTINSOFT_ANALYZER__
        // Force spliting states to avoid false alarms
        tis_variable_split(&total_read_bytes, sizeof(total_read_bytes), 10000);
#endif
        uint32_t read_bytes = cell_com_read_last_bytes(
            &line[total_read_bytes], max_len - total_read_bytes, true, '\n');
#ifdef __TRUSTINSOFT_ANALYZER__
        // merge states to save resources
        //@ slevel merge;
#endif
        if (read_bytes)
        {
            total_read_bytes += read_bytes;

            if (total_read_bytes < max_len)
            {
                if ('\n' == line[total_read_bytes - 1])
                {
                    // received a complete line
                    line[total_read_bytes] = '\0';

                    // TIS: because line[total_read_bytes] = '\0' returning a
                    // valid string has been proven, the assertion below is
                    // surely valid
                    //@ assert line[AT_MAX_LINE_LEN - 1] == '\0';
                    if (trim_and_validate_line(line))
                    {
                        line_ready       = true;
                        total_read_bytes = 0;
                        cell_log_debug("<== ...RX(%d): %s", strlen(line), line);
                        break;
                    }
                    else
                    {
                        total_read_bytes = 0;
                    }
                }
                else
                {
                    // wait for incoming data
                    cell_os_sleep_ms(1);
                }
            }
            else
            {
                line_ready = false;
                break;
            }
        }
    }

    return line_ready;
}

at_req_status_t
seek_one_line_and_parse_final_resp (char * line, uint64_t start_time,
                                    uint32_t timeout_ms, uint16_t * error_code)
{
    at_req_status_t ret = AT_TIMEOUT;

    if (true == at_read_line(line, AT_MAX_LINE_LEN, start_time, timeout_ms))
    {
        // TIS: because at_read_line returning a string has been proven,
        // the assertion below is surely valid
        //@ assert line[AT_MAX_LINE_LEN -1] == '\0';
        parse_final_resp(line, &ret, error_code);
    }

    return ret;
}

at_req_status_t
seek_and_parse_final_resp (char * line, uint64_t start_time,
                           uint32_t timeout_ms, uint16_t * error_code)
{
    at_req_status_t ret = AT_PENDING;

    do
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, error_code);
    }
    while (ret == AT_PENDING);

    return ret;
}

at_req_status_t
seek_and_parse_error_resp (char * line, uint64_t start_time,
                           uint32_t timeout_ms, uint16_t * error_code)
{
    at_req_status_t ret = AT_PENDING;

    do
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, error_code);

        if (AT_SUCCESS == ret)
        {
            ret = AT_PENDING;
        }
    }
    while (AT_PENDING == ret);

    return ret;
}

void
process_remaining_data (void)
{
    char line[AT_MAX_LINE_LEN] = {0};

    while (at_read_line(line, AT_MAX_LINE_LEN, 0, 0) == true)
    {
        // TIS: because at_read_line returning a valid string has been proven,
        // the assertion below is surely valid
        //@ assert line[AT_MAX_LINE_LEN - 1] == '\0';
        urc_parse_unsolicited_response(line);
    }
}

bool
trim_and_validate_line (char * line)
{
    bool    line_valid = true;
    uint8_t len        = strlen(line);

    if ((0 == strcmp(line, "\r\n")) || (0 == strcmp(line, "\n")))
    {
        line_valid = false;
    }

    if (true == line_valid)
    {
        if ((len >= 2) && (line[len - 2] == '\r') && (line[len - 1] == '\n'))
        {
            line[len - 2] = '\0'; // Remove both \r and \n
        }
    }

    sqc_assert((strlen(line) >= 0));

    return line_valid;
}
