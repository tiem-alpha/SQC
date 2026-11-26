#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/sqc/benchmarks/cmcell_features/stub_commands_helper.c"
/**
 * @file stub_commands_helper.c
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
 * @brief Stubbing for modem helper functions
 * @date 2026-05-12
 */

#include "cell_modem_helper.h"
#include "cell_types.h"
#include "cell_log.h"

#include <sqc_builtin.h>
#include <limits.h>

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 * ============================================================
 * helper_binary_string_to_uint8()
 * ============================================================
 */

uint8_t
helper_binary_string_to_uint8 (const char * bin_str)
    __attribute__((sqc_override))
{
    (void) bin_str;
    /*
     * Stub result: any uint8_t value.
     * This models all possible conversions without parsing.
     */
    return (uint8_t) sqc_interval(0, UINT8_MAX);
}

/*
 * ============================================================
 * helper_uint8_to_binary()
 * ============================================================
 */

void
helper_uint8_to_binary (uint8_t value, char * output)
    __attribute__((sqc_override))
{
    (void) value;

    sqc_make_unknown(output, 9);
    output[8] = '\0';
}

/*
 * ============================================================
 * helper_parse_gsm_band()
 * ============================================================
 */

void
helper_parse_gsm_band (uint8_t gsm_band_val, cell_rat_settings_t * rat_settings,
                       const gsm_band_map_t * gsm_band_map,
                       uint8_t gsm_band_map_size, uint8_t gsm_band_all_val)
    __attribute__((sqc_override))
{

    (void) gsm_band_val;
    (void) gsm_band_map;
    (void) gsm_band_map_size;
    (void) gsm_band_all_val;

    sqc_make_unknown(rat_settings, sizeof(*rat_settings));
}

/*
 * ============================================================
 * helper_parse_lte_band()
 * ============================================================
 */

void
helper_parse_lte_band (cell_lte_band_t * lte_band_support, const char * hex_str,
                       const band_mapping_t * band_map, size_t band_map_size)
    __attribute__((sqc_override))
{
    /*
     * Do not require hex_str to be a valid format.
     * The harness already gives either NULL or valid bounded buffers.
     */
    (void) hex_str;

    if (lte_band_support != NULL)
    {
        /*
         * Model arbitrary parsed LTE band result.
         */
        sqc_make_unknown(lte_band_support,
                         CELL_LTE_BAND_MAX * sizeof(cell_lte_band_t));
    }

    if (band_map != NULL)
    {
        /*
         * In your harness, band_map_size is at most 8.
         */
        for (size_t i = 0U; i < band_map_size; i++)
        {
            (void) band_map[i].bit_index;
            (void) band_map[i].band_type;
        }
    }
}

/*
 * ============================================================
 * helper_calculate_band_mask()
 * ============================================================
 */

void
helper_calculate_band_mask (cell_lte_band_t * band_array, char * band_mask_out,
                            uint16_t out_size, uint8_t * lte_band_index_map)
    __attribute__((sqc_override))
{
    (void) band_array;
    (void) lte_band_index_map;

    if (out_size > 0U)
    {
        sqc_make_unknown(band_mask_out, out_size);
        band_mask_out[out_size - 1U] = '\0';
    }
}

/*
 * ============================================================
 * helper_ip_to_str()
 * ============================================================
 */

void
helper_ip_to_str (cell_socket_address_t socket_addr, char * ip_addr)
    __attribute__((sqc_override))
{
    (void) socket_addr;

    for (size_t i = 0U; i < CELL_IP_ADDRESS_MAX_SIZE - 1U; i++)
    {
        ip_addr[i] = (char) sqc_interval(0, 255);
    }

    ip_addr[CELL_IP_ADDRESS_MAX_SIZE] = '\0';
}

/*
 * ============================================================
 * helper_str_to_ip()
 * ============================================================
 */

bool
helper_str_to_ip (cell_socket_address_t * socket_addr, char * ip_addr)
    __attribute__((sqc_override))
{

    (void) ip_addr;

    bool ret = (bool) sqc_interval(0, 1);

    if (ret)
    {
        sqc_make_unknown(socket_addr, sizeof(*socket_addr));
    }

    return ret;
}

/*
 * ============================================================
 * helper_ip_convert()
 * ============================================================
 */

bool
helper_ip_convert (char * ip, cell_network_info_t * net_info)
    __attribute__((sqc_override))
{
    (void) ip;
    /*
     * Model arbitrary conversion result in net_info.
     */
    sqc_make_unknown(net_info, sizeof(*net_info));

    bool result;
    sqc_make_unknown(&result, sizeof(result));

    return result;
}

/*
 * ============================================================
 * helper_extract_quoted_string()
 * ============================================================
 */

int
helper_extract_quoted_string (const char * input, size_t input_quoted_pos,
                              char * output, size_t output_max_len)
    __attribute__((sqc_override))
{

    (void) input;
    (void) input_quoted_pos;

    if (output_max_len > 0U)
    {
        sqc_make_unknown(output, output_max_len);
        output[output_max_len - 1U] = '\0';
    }

    if (sqc_interval(0, 1))
    {
        return -1;
    }

    return sqc_interval(2, INT_MAX);
}
