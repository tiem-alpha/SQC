#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_helper.c"
/**
 * @file cell_modem_helper.c
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
 * @brief Helper functions for cellular modem
 * @date 2026-05-12
 */
#include "cell_modem_helper.h"
#include "cell_com.h"
#include "cell_urc.h"

#include "cell_log.h"
#include "cell_os.h"

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#define LTE_BAND_MASK_MAX_BITS (LTE_BAND_MASK_HEX_LENGTH_MAX * 4U)

static uint8_t hex_char_to_val(char c);

static bool    helper_is_valid_gsm_band(cell_gsm_band_t band);
static bool    helper_is_hex_digit(char c, unsigned int * value);
static bool    helper_is_valid_lte_band(cell_lte_band_t band);
static bool helper_bytes_to_hex_string(const uint8_t * data, size_t data_size,
                                       char * out, size_t out_size);

uint8_t
helper_binary_string_to_uint8 (const char * bin_str)
{
    uint8_t value = 0U;

    if (bin_str == NULL)
    {
        return (uint8_t) 0xFFU;
    }

    for (int i = 0; i < 8; ++i)
    {
        value = (uint8_t) (value << 1U);

        if (bin_str[i] == '1')
        {
            value = (uint8_t) (value | 1U);
        }
        else if (bin_str[i] == '0')
        {
            /*
             * Valid bit 0, nothing to add.
             */
        }
        else
        {
            /*
             * Any other byte is invalid:
             * '\0', 'x', 0xff, non-printable, etc.
             */
            return (uint8_t) 0xFFU;
        }
    }

    return value;
}

void
helper_uint8_to_binary (uint8_t value, char * output)
{

    if (output == NULL)
    {
        return;
    }

    for (int i = 7; i >= 0; i--)
    {
        output[7 - i] = ((value >> i) & 1) ? '1' : '0';
    }
    output[8] = '\0'; // Null-terminate
}

static bool
helper_is_valid_gsm_band (cell_gsm_band_t band)
{
    return ((band < CELL_GSM_BAND_MAX));
}

void
helper_parse_gsm_band (uint8_t gsm_band_val, cell_rat_settings_t * rat_settings,
                       const gsm_band_map_t * gsm_band_map,
                       uint8_t gsm_band_map_size, uint8_t gsm_band_all_val)
{
    if ((rat_settings == NULL) || (gsm_band_map == NULL))
    {
        return;
    }

    for (uint8_t i = 0U; i < CELL_GSM_BAND_MAX; i++)
    {
        rat_settings->gsm_band[i] = (cell_gsm_band_t) CELL_GSM_BAND_INVALID;
    }

    uint8_t index = 0;

    if (gsm_band_all_val == gsm_band_val)
    {
        for (uint8_t i = 0; i < gsm_band_map_size; i++)
        {
            cell_gsm_band_t band = gsm_band_map[i].band_type;
            if (helper_is_valid_gsm_band(band))
            {
                if (index < CELL_GSM_BAND_MAX)
                {
                    rat_settings->gsm_band[index++] = band;
                }
            }
        }
    }
    else
    {
        for (uint8_t i = 0; i < gsm_band_map_size; i++)
        {
            cell_gsm_band_t band = gsm_band_map[i].band_type;
            if (gsm_band_val & gsm_band_map[i].band_val)
            {
                if (helper_is_valid_gsm_band(band))
                {
                    if (index < CELL_GSM_BAND_MAX)
                    {
                        rat_settings->gsm_band[index++] = band;
                    }
                }
            }
        }
    }
}

static uint8_t
hex_char_to_val (char c)
{
    if ((c >= '0') && (c <= '9'))
    {
        return (uint8_t) (c - '0');
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        return (uint8_t) (c - 'A' + 10);
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        return (uint8_t) (c - 'a' + 10);
    }

    return (uint8_t) 0xFFU;
}

static bool
helper_is_valid_lte_band (cell_lte_band_t band)
{
    return (band < CELL_LTE_BAND_MAX);
}

void
helper_parse_lte_band (cell_lte_band_t * lte_band_support, const char * hex_str,
                       const band_mapping_t * band_map, size_t band_map_size)
{
    if ((lte_band_support == NULL) || (hex_str == NULL) || (band_map == NULL))
    {
        return;
    }

    /* Initialize all entries in the output array as invalid bands. */
    for (size_t i = 0U; i < CELL_LTE_BAND_MAX; i++)
    {
        lte_band_support[i] = CELL_LTE_BAND_INVALID;
    }

    const char * hex = hex_str;
    size_t       len = 0U;

    /* Calculate the input string length. */
    while (hex[len] != '\0')
    {
        len++;
    }

    /*
     * Determine where the hexadecimal digits start:
     * - 2 if the input starts with "0x" or "0X"
     * - 0 if the input does not contain a prefix
     */
    size_t start_pos = 0U;

    if ((len >= 2U) && (hex[0] == '0') && ((hex[1] == 'x') || (hex[1] == 'X')))
    {
        start_pos = 2U;
    }

    /* Calculate the number of hexadecimal digits, excluding the prefix. */
    const size_t hex_len = len - start_pos;

    /* Reject an empty value or a value exceeding the maximum length. */
    if ((hex_len == 0U) || (hex_len > LTE_BAND_MASK_HEX_LENGTH_MAX))
    {
        return;
    }

    uint8_t bit_index = 0U;
    uint8_t idx       = 0U;

    /*
     * Parse the hexadecimal string from right to left.
     *
     * Using "pos > start_pos" prevents unsigned integer underflow.
     * The current character is located at hex[pos - 1U].
     */
    for (size_t pos = len; pos > start_pos; pos--)
    {
        uint8_t value = hex_char_to_val(hex[pos - 1U]);

        /* Reject the input if it contains a non-hexadecimal character. */
        if (value >= 16U)
        {
            return;
        }

        /* Each hexadecimal digit represents four bits. */
        for (uint8_t bit = 0U; bit < 4U; bit++)
        {
            if ((value & 0x01U) != 0U)
            {
                /*
                 * Find the LTE band corresponding to the current bit
                 * position.
                 */
                for (size_t map_idx = 0U; map_idx < band_map_size; map_idx++)
                {
                    if (band_map[map_idx].bit_index == bit_index)
                    {
                        const cell_lte_band_t band
                            = band_map[map_idx].band_type;

                        /*
                         * Add the band to the output array only if it is
                         * valid and there is enough space available.
                         */
                        if (helper_is_valid_lte_band(band))
                        {
                            if (idx < CELL_LTE_BAND_MAX)
                            {
                                lte_band_support[idx] = band;
                                idx++;
                            }
                        }

                        break;
                    }
                }
            }

            /* Move to the next bit in the current hexadecimal digit. */
            value >>= 1U;
            bit_index++;
        }
    }
}

// Convert the internal LTE band mask byte array into a compact hexadecimal
// string representation without the "0x" prefix.
static bool
helper_bytes_to_hex_string (const uint8_t * data, size_t data_size, char * out,
                            size_t out_size)
{
    static const char hex_lut[] = "0123456789abcdef";

    size_t            highest   = data_size;
    size_t            offset    = 0U;
    size_t            required_size;

    if ((data == NULL) || (out == NULL) || (out_size == 0U))
    {
        return false;
    }

    /*
     * Locate the most significant non-zero byte.
     *
     * 'highest' represents the number of significant bytes. Therefore,
     * the most significant byte is located at index highest - 1U.
     */
    while ((highest > 0U) && (data[highest - 1U] == 0U))
    {
        highest--;
    }

    /*
     * A zero mask is represented as "0".
     * Two bytes are required, including the null terminator.
     *
     * This case must be handled separately because the code below accesses
     * data[highest - 1U].
     */
    if (highest == 0U)
    {
        if (out_size < 2U)
        {
            return false;
        }

        out[0] = '0';
        out[1] = '\0';

        return true;
    }

    /*
     * Calculate the required output size:
     * - One or two characters for the most significant byte
     * - Two characters for each remaining byte
     * - Null terminator
     */
    required_size = 1U;

    /*
     * The most significant byte is written without an unnecessary
     * leading zero.
     */
    required_size += (data[highest - 1U] < 16U) ? 1U : 2U;

    /*
     * Each remaining byte requires exactly two hexadecimal characters.
     */
    required_size += (highest - 1U) * 2U;

    if (out_size < required_size)
    {
        return false;
    }

    /*
     * Write the most significant byte without an unnecessary leading zero.
     */
    if (data[highest - 1U] < 16U)
    {
        out[offset++] = hex_lut[data[highest - 1U]];
    }
    else
    {
        out[offset++] = hex_lut[(data[highest - 1U] >> 4U) & 0x0FU];

        out[offset++] = hex_lut[data[highest - 1U] & 0x0FU];
    }

    /*
     * Write the remaining bytes. Each remaining byte produces exactly two
     * hexadecimal characters, including leading zero nibbles.
     */
    for (size_t i = highest - 1U; i > 0U; --i)
    {
        const uint8_t value = data[i - 1U];

        out[offset++]       = hex_lut[(value >> 4U) & 0x0FU];
        out[offset++]       = hex_lut[value & 0x0FU];
    }

    out[offset] = '\0';

    return true;
}

void
helper_calculate_band_mask (cell_lte_band_t * band_array, char * band_mask_out,
                            uint16_t band_mask_out_size,
                            uint8_t  lte_band_index_map[])
{
    if ((band_array == NULL) || (band_mask_out == NULL)
        || (lte_band_index_map == NULL) || (band_mask_out_size == 0U))
    {
        return;
    }
    /*Internal binary representation of the final LTE band mask. All valid LTE
     * bands are accumulated into this buffer before being converted into the
     * output hexadecimal string.*/
    uint8_t band_mask_arr[(LTE_BAND_MASK_HEX_LENGTH_MAX + 1U) / 2U] = {0};
    /*
     * Logical capacity of the internal LTE band mask buffer.
     * Used as the authoritative boundary for validating all
     * bit-to-byte index translations before updating the mask.
     */
    const size_t band_mask_arr_size = sizeof(band_mask_arr);

    for (int i = 0; i < CELL_LTE_BAND_MAX; i++)
    {
        cell_lte_band_t band = band_array[i];

        if (!helper_is_valid_lte_band(band))
        {
            break;
        }

        if ((size_t) band >= CELL_LTE_BAND_MAX)
        {
            continue;
        }

        uint8_t band_index = lte_band_index_map[band];

        if ((band_index == CELL_LTE_BAND_INVALID)
            || (band_index >= LTE_BAND_MASK_MAX_BITS))
        {
            continue;
        }

        /* Convert a mapped LTE band to byte/bit location and set the
         * corresponding bit in the internal band mask.
         * Example: band_index = 10
         * ->byte_index = 10 / 8 = 1, bit_position = 10 % 8 = 2;
         * -> band_mask_arr[1] |= 0x04;
         */

        /*
         * Translate the global LTE band bit position into the
         * corresponding byte within the internal band mask.
         */
        size_t byte_index = band_index / 8U;

        /* Set the bit associated with the LTE band in the internal mask
         * representation. The bit position is determined from the mapped LTE
         * band index and set in the selected byte above.
         */
        band_mask_arr[byte_index] |= (uint8_t) (1U << (band_index % 8U));
    }

    if (!helper_bytes_to_hex_string(band_mask_arr,
                                    band_mask_arr_size,
                                    band_mask_out,
                                    band_mask_out_size))
    {
        /*
         * Return an empty string when the output buffer is too small to contain
         * the complete hexadecimal band mask.
         */
        band_mask_out[0] = '\0';
    }
}

bool
helper_ipv4_parse (char * input, uint8_t * ipv4)
{
    unsigned int b0    = 0U;
    unsigned int b1    = 0U;
    unsigned int b2    = 0U;
    unsigned int b3    = 0U;
    char         extra = '\0';

    if ((input == NULL) || (ipv4 == NULL))
    {
        return false;
    }

    if (sscanf(input, "%3u.%3u.%3u.%3u%c", &b0, &b1, &b2, &b3, &extra) != 4)
    {
        return false;
    }

    if ((b0 > 255U) || (b1 > 255U) || (b2 > 255U) || (b3 > 255U))
    {
        return false;
    }

    ipv4[0] = (uint8_t) b0;
    ipv4[1] = (uint8_t) b1;
    ipv4[2] = (uint8_t) b2;
    ipv4[3] = (uint8_t) b3;

    return true;
}

static bool
helper_is_hex_digit (char c, unsigned int * value)
{
    if ((c >= '0') && (c <= '9'))
    {
        *value = (unsigned int) (c - '0');
        return true;
    }

    if ((c >= 'a') && (c <= 'f'))
    {
        *value = (unsigned int) (c - 'a' + 10U);
        return true;
    }

    if ((c >= 'A') && (c <= 'F'))
    {
        *value = (unsigned int) (c - 'A' + 10U);
        return true;
    }

    return false;
}

bool
helper_ipv6_parse (const char * input, uint16_t * ipv6)
{
    const char * p = input;

    if ((input == NULL) || (ipv6 == NULL))
    {
        return false;
    }

    for (unsigned int group = 0U; group < 8U; group++)
    {
        unsigned int value  = 0U;
        unsigned int digits = 0U;

        while (digits < 4U)
        {
            unsigned int hex_value = 0U;

            if (!helper_is_hex_digit(*p, &hex_value))
            {
                break;
            }

            value = (value << 4U) | hex_value;
            digits++;
            p++;
        }

        if (digits == 0U)
        {
            return false;
        }

        ipv6[group] = (uint16_t) value;

        if (group < 7U)
        {
            if (*p != ':')
            {
                return false;
            }

            p++;
        }
    }

    if (*p != '\0')
    {
        return false;
    }

    return true;
}

void
helper_ip_to_str (cell_socket_address_t socket_addr, char * ip_addr)
{
    if (ip_addr == NULL)
    {
        return;
    }

    ip_addr[0] = '\0';

    if (CELL_IP_ADDRESS_V4 == socket_addr.ip_type)
    {
        (void) snprintf(ip_addr,
                        CELL_IP_ADDRESS_MAX_SIZE + 1U, // +1 for '\0' character
                        "%u.%u.%u.%u",
                        (unsigned int) socket_addr.ipv4.addr[0],
                        (unsigned int) socket_addr.ipv4.addr[1],
                        (unsigned int) socket_addr.ipv4.addr[2],
                        (unsigned int) socket_addr.ipv4.addr[3]);
    }
    else if (CELL_IP_ADDRESS_V6 == socket_addr.ip_type)
    {
        (void) snprintf(ip_addr,
                        CELL_IP_ADDRESS_MAX_SIZE + 1U,
                        "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
                        (unsigned int) socket_addr.ipv6.addr[0],
                        (unsigned int) socket_addr.ipv6.addr[1],
                        (unsigned int) socket_addr.ipv6.addr[2],
                        (unsigned int) socket_addr.ipv6.addr[3],
                        (unsigned int) socket_addr.ipv6.addr[4],
                        (unsigned int) socket_addr.ipv6.addr[5],
                        (unsigned int) socket_addr.ipv6.addr[6],
                        (unsigned int) socket_addr.ipv6.addr[7]);
    }
    else
    {
        cell_log_debug("Invalid IP type");
    }

    ip_addr[CELL_IP_ADDRESS_MAX_SIZE] = '\0';
}

bool
helper_str_to_ip (cell_socket_address_t * socket_addr, char * ip_addr)
{
    bool status = true;

    if ((socket_addr == NULL) || (ip_addr == NULL))
    {
        return false;
    }

    if (strchr(ip_addr, '.') != NULL)
    {
        // IPv4
        socket_addr->ip_type = CELL_IP_ADDRESS_V4;
        status = helper_ipv4_parse(ip_addr, socket_addr->ipv4.addr);
    }
    else if (strchr(ip_addr, ':') != NULL)
    {
        // IPv6
        socket_addr->ip_type = CELL_IP_ADDRESS_V6;
        status = helper_ipv6_parse(ip_addr, socket_addr->ipv6.addr);
    }
    else
    {
        status = false;
    }

    return status;
}

bool
helper_ip_convert (char * ip, cell_network_info_t * net_info)
{

    if ((ip == NULL) || (net_info == NULL))
    {
        return false;
    }

    switch (net_info->pdp_type)
    {
        case CELL_PDP_CONTEXT_IPV4:
            return helper_ipv4_parse(ip, net_info->ipv4.addr);

        case CELL_PDP_CONTEXT_IPV6:
            return helper_ipv6_parse(ip, net_info->ipv6.addr);

        default:
            return false;
    }
}

int
helper_extract_quoted_string (const char * input, size_t input_quoted_pos,
                              char * output, size_t output_max_len)
{
    if ((NULL == input) || (NULL == output) || (0U == output_max_len))
    {
        return -1;
    }

    output[0] = '\0';

    // Array boundary checking
    size_t input_len = strlen(input);
    if (input_quoted_pos >= input_len)
    {
        return -1;
    }

    if (input[input_quoted_pos] != '"')
    {
        return -1;
    }

    size_t start_content_pos = input_quoted_pos + 1; // after opening quote
    size_t copy_len          = 0;

    while (start_content_pos + copy_len < input_len
           && input[start_content_pos + copy_len] != '"')
    {
        if (copy_len + 1 >= output_max_len)
        {
            return -1; // Exceed capability, no room for NULL character
        }

        copy_len++;
    }

    if (input[start_content_pos + copy_len] != '"')
    {
        return -1; // missing quoted closing "
    }

    // Commit outputs only after all validation passes.
    (void) memmove(output, &input[start_content_pos], copy_len);
    output[copy_len]            = '\0';
    output[output_max_len - 1U] = '\0';

    return (int) (copy_len + 2U); // total consumed length including quotes
}
