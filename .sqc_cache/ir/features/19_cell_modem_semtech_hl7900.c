#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_semtech_hl7900.c"
/**
 * @file cell_modem_semtech_hl7900.c
 * @copyright
 * Copyright (C) 2026 Sagemcom. All rights reserved.
 * @par Licence
 * The source code contained herein contains proprietary
 * information which is the property of Sagemcom.
 * The source code and program may be used and/or copied
 * only with the written permission of Sagemcom.
 * or in accordance with the terms and conditions
 * stipulated in the agreement/contract under which the programs
 * have been supplied.
 * @author Dev : VU QUANG Minh
 * @brief Modem driver implementation for HL7900 cellular module
 * @date 2026-04-02
 */
#include "cell_modem_semtech_hl7900.h"

modem_driver_t semtech_hl7900_driver
    = {.urc_handler                 = semtech_urc_handler,
       .map_cme_error               = semtech_map_cme_error,
       .map_cms_error               = semtech_map_cms_error,
       .power_off_software          = semtech_module_poweroff,
       .sim_readiness_get           = semtech_sim_readiness_info_get,
       .sim_pin_enter               = semtech_sim_enter_pin,
       .iccid_get                   = semtech_iccid_get,
       .modem_specific_features_set = semtech_modem_specific_features_set,
       .pdp_settings_get            = semtech_pdp_settings_get,
       .pdp_config                  = semtech_pdp_settings_set,
       .operator_info_get           = semtech_operator_info_get,
       .operator_config             = semtech_operator_settings_set,
       .rat_settings_get            = semtech_rat_settings_get,
       .rat_config                  = semtech_rat_settings_set,
       .service_domain_settings_get = semtech_service_domain_get,
       .service_domain_config       = semtech_service_domain_set,
       .pdp_context_activate        = semtech_pdp_activate,
       .pdp_context_deactivate      = semtech_pdp_deactivate,
       .pdp_status_get              = semtech_pdp_status_get,
       .firmware_version_get        = semtech_firmware_version_get,
       .cellular_info_get           = hl7900_cellular_info_get,
       .dfota_local_init            = NULL,
       .dfota_local_update          = NULL,
       .dfota_wireless_init         = NULL,
       .dfota_wireless_start        = NULL,
       .dfota_download_cancel       = NULL,
       .dfota_write                 = NULL,
       .rat_priority_supported      = false,
       .socket_create               = semtech_socket_create,
       .socket_close                = semtech_socket_close,
       .socket_check_usability      = semtech_socket_check_usability,
       .socket_send                 = semtech_socket_send,
       .socket_sent_info_get        = NULL,
       .socket_receive              = semtech_socket_receive};

static void     cell_plmn_convert(uint32_t plmn_id, cell_plmn_info_t * out);
static uint32_t cell_id_convert(const char * eci_str);
static at_req_status_t hl7900_serving_cell_get(cell_cellular_info_t * cell_info,
                                               uint16_t * error_code);
static at_req_status_t at_parser_serving_cell_get(at_request_t * at_req,
                                                  char *         line,
                                                  uint64_t       start_time,
                                                  uint32_t       timeout_ms);

at_req_status_t
hl7900_cellular_info_get (cell_cellular_info_t * cellular_info,
                          uint16_t *             error_code)
{
    at_req_status_t ret = AT_PENDING;
    ret                 = hl7900_serving_cell_get(cellular_info, error_code);

    return ret;
}

static at_req_status_t
hl7900_serving_cell_get (cell_cellular_info_t * cell_info,
                         uint16_t *             error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_cell
        = {.p_send_buff  = "AT%SCAN=\"QUERY\",1\r",
           .resp_prefix  = "%SCAN",
           .p_data       = cell_info,
           .data_len     = (uint16_t) sizeof(cell_cellular_info_t),
           .p_error_code = error_code};
    at_req_get_cell.send_buff_len = strlen(at_req_get_cell.p_send_buff);

    ret = at_command_request(&at_req_get_cell, 300, at_parser_serving_cell_get);
    return ret;
}

static at_req_status_t
at_parser_serving_cell_get (at_request_t * at_req, char * line,
                            uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t        ret       = AT_SUCCESS;

    cell_cellular_info_t * cell_info = (cell_cellular_info_t *) at_req->p_data;
    unsigned int           plmn      = 0;
    char                   eci[16]   = {0};

    // Response : %SCAN: res,band,earfcn,pci,rsrp,rsrq,
    // eci,bandwidth,tac,cstat,emg,plmn_id
    int parsed = sscanf(
        line,
        "%%SCAN: %*d,%2hhu,%u,%*d,%hd,%hd,\"%15[0123456789ABCDEFabcdef]\","
        "%*d,%hu,%*d,%*d,%u",
        &cell_info->serving_cell.band_ind,
        &cell_info->serving_cell.earfcn,
        &cell_info->serving_cell.signal.rsrp,
        &cell_info->serving_cell.signal.rsrq,
        eci,
        &cell_info->serving_cell.tac,
        &plmn);

    if (parsed == 7)
    {
        cell_info->serving_cell.cid = cell_id_convert(eci);
        cell_plmn_convert(plmn, &cell_info->serving_cell.plmn);

        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static void
cell_plmn_convert (uint32_t plmn_id, cell_plmn_info_t * out)
{
    if (plmn_id < 100000) // 5 digits
    {
        snprintf(out->mcc, sizeof(out->mcc), "%03u", plmn_id / 100);
        snprintf(out->mnc, sizeof(out->mnc), "%02u", plmn_id % 100);
    }
    else // 6 digits
    {
        uint32_t mcc_val = (plmn_id / 1000) % 1000;
        uint32_t mnc_val = plmn_id % 1000;

        snprintf(out->mcc, sizeof(out->mcc), "%03u", mcc_val);
        snprintf(out->mnc, sizeof(out->mnc), "%03u", mnc_val);
    }
}

static uint32_t
cell_id_convert (const char * eci_str)
{
    uint32_t eci = 0;
    if (eci_str != NULL)
    {
        sscanf(eci_str, "%x", &eci);
    }
    return eci;
}
