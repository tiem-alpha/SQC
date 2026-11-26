#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_eg915u.c"
/**
 * @file cell_modem_eg915u.c
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
 * @brief Modem driver for EG915U cellular module
 * @date 2025-03-31
 */
#include "cell_modem_eg915u.h"
#include "cell_modem_common.h"
#include "cell_modem_quectel.h"
#include "cell_modem_helper.h"
#include "cell_internal.h"
#include "cell_at.h"

#include "cell_log.h"
#include "cell_os.h"
#include "cell_connect.h"
#include "cell_types.h"
#include "cell_urc.h"
#include "cell_dfota.h"
#include "cell_socket.h"
#include "cell_com.h"

#include "stdio.h"

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#define EG915U_PDP_ACTIVATION_REQ_TIMEOUT_MS   (150000UL)
#define EG915U_PDP_DEACTIVATION_REQ_TIMEOUT_MS (40000UL)

#define SCAN_MODE_AUTO                         0 // GSM and LTE
#define SCAN_MODE_GSM                          1
#define SCAN_MODE_LTE                          3

#define SCAN_SEQ_GSM_VAL                       1
#define SCAN_SEQ_CATM1_VAL                     2

#define SCAN_SEQ_INVALID_BIT                   0
#define SCAN_SEQ_GSM_BIT                       1
#define SCAN_SEQ_CATM1_BIT                     2

#define GSM_BAND_NO_CHANGE                     0x0
#define GSM_BAND_EGSM900                       0x1
#define GSM_BAND_DCS1800                       0x2
#define GSM_BAND_GSM850                        0x4
#define GSM_BAND_PCS1900                       0x8
#define GSM_BAND_ALL                           0xf

// EG915U LTE support up to band 66, so we need to use 128-bit (2x uint64_t) to
// store, and it can be represent by string of length 32
#define MAX_HEX_LEN                          32

#define EG915U_CMD_RESPONSE_POLL_INTERVAL_MS 3

#define LTE_BAND_MASK_STR_MAX                (2 + MAX_HEX_LEN + 1)

typedef enum
{
    MODE_AUTO = 0,
    MODE_GSM  = 1,
    MODE_LTE  = 3
} modem_rat_mode_t;

static const gsm_band_map_t gsm_band_map[] = {
    {GSM_BAND_EGSM900, CELL_GSM_BAND_EGSM900},
    {GSM_BAND_DCS1800, CELL_GSM_BAND_DCS1800},
    {GSM_BAND_GSM850,  CELL_GSM_BAND_GSM850 },
    {GSM_BAND_PCS1900, CELL_GSM_BAND_PCS1900}
};
static uint8_t gsm_band_map_size
    = (uint8_t) (sizeof(gsm_band_map) / sizeof(gsm_band_map[0]));

// Map band to index values
static uint8_t lte_band_index_map[CELL_LTE_BAND_MAX] = {
    [CELL_LTE_BAND_1]  = 0,
    [CELL_LTE_BAND_2]  = 1,
    [CELL_LTE_BAND_3]  = 2,
    [CELL_LTE_BAND_4]  = 3,
    [CELL_LTE_BAND_5]  = 4,
    [CELL_LTE_BAND_7]  = 6,
    [CELL_LTE_BAND_8]  = 7,

    [CELL_LTE_BAND_9]  = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_10] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_12] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_13] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_17] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_18] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_19] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_20] = 19,

    [CELL_LTE_BAND_23] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_25] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_26] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_27] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_28] = 27,

    [CELL_LTE_BAND_31] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_34] = 33,
    [CELL_LTE_BAND_38] = 37,
    [CELL_LTE_BAND_39] = 38,
    [CELL_LTE_BAND_40] = 39,
    [CELL_LTE_BAND_41] = 40,

    [CELL_LTE_BAND_65] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_66] = 65,
    [CELL_LTE_BAND_70] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_71] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_72] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_73] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_85] = CELL_LTE_BAND_INVALID,
};
// Band mapping based on bit index (max 128-bit value)
static const band_mapping_t lte_band_map[] = {
    {0,  CELL_LTE_BAND_1 },
    {1,  CELL_LTE_BAND_2 },
    {2,  CELL_LTE_BAND_3 },
    {3,  CELL_LTE_BAND_4 },
    {4,  CELL_LTE_BAND_5 },
    {6,  CELL_LTE_BAND_7 },
    {7,  CELL_LTE_BAND_8 },
    {19, CELL_LTE_BAND_20},
    {27, CELL_LTE_BAND_28},
    {33, CELL_LTE_BAND_34},
    {37, CELL_LTE_BAND_38},
    {38, CELL_LTE_BAND_39},
    {39, CELL_LTE_BAND_40},
    {40, CELL_LTE_BAND_41},
    {65, CELL_LTE_BAND_66},
};

static const size_t lte_band_map_size
    = sizeof(lte_band_map) / sizeof(lte_band_map[0]);

static at_req_status_t  at_parser_rat_mode_get(at_request_t * at_req,
                                               char * line, uint64_t start_time,
                                               uint32_t timeout_ms);

static at_req_status_t  at_parser_band_get(at_request_t * at_req, char * line,
                                           uint64_t start_time,
                                           uint32_t timeout_ms);

static modem_rat_mode_t get_mode_from_seq(cell_rat_type_t * seq);

static at_req_status_t  rat_mode_set(uint8_t mode, uint16_t * error_code);

static at_req_status_t  band_set(cell_rat_settings_t * rat_settings,
                                 uint16_t *            error_code);

static void parse_and_update_mode(cell_rat_type_t * seq, modem_rat_mode_t mode);

static urc_map_handler_t eg915u_urc_handler[URC_TYPE_NUMBER] = {
    [URC_TYPE_RDY]                 = {.handler = NULL},
    [URC_TYPE_APP_RDY]             = {.handler = NULL},
    [URC_TYPE_POWER_DOWN]          = {.handler = NULL},
    [URC_TYPE_NORMAL_POWER_DOWN]   = {.handler = NULL},
    [URC_TYPE_PSM_POWER_DOWN]      = {.handler = NULL},
    [URC_TYPE_RING]                = {.handler = NULL},
    [URC_TYPE_CFUN_1]              = {.handler = NULL},
    [URC_TYPE_QUSIM]               = {.handler = quectel_handle_urc_qusim},
    [URC_TYPE_CPIN]                = {.handler = quectel_handle_urc_cpin},
    [URC_TYPE_SIMSTAT]             = {.handler = NULL},
    [URC_TYPE_QIND_SMS_FULL]       = {.handler = NULL},
    [URC_TYPE_QIND_SMS_DONE]       = {.handler = NULL},
    [URC_TYPE_CRING]               = {.handler = NULL},
    [URC_TYPE_QIND_CSQ]            = {.handler = NULL},
    [URC_TYPE_QCSCON]              = {.handler = NULL},
    [URC_TYPE_CTZV]                = {.handler = NULL},
    [URC_TYPE_CTZE]                = {.handler = NULL},
    [URC_TYPE_CMTI]                = {.handler = NULL},
    [URC_TYPE_CMT]                 = {.handler = NULL},
    [URC_TYPE_CBM]                 = {.handler = NULL},
    [URC_TYPE_CDS]                 = {.handler = NULL},
    [URC_TYPE_CDSI]                = {.handler = NULL},
    [URC_TYPE_COLP]                = {.handler = NULL},
    [URC_TYPE_CLIP]                = {.handler = NULL},
    [URC_TYPE_CCWA]                = {.handler = NULL},
    [URC_TYPE_QIND_AIRPLANESTATUS] = {.handler = NULL},
    [URC_TYPE_CSSI]                = {.handler = NULL},
    [URC_TYPE_CSSU]                = {.handler = NULL},
    [URC_TYPE_CMGL]                = {.handler = NULL},
    [URC_TYPE_QCMGR]               = {.handler = NULL},
    [URC_TYPE_CMGR]                = {.handler = NULL},
    [URC_TYPE_CEDRXP]              = {.handler = NULL},
    [URC_TYPE_QPTWEDRXP]           = {.handler = NULL},
    [URC_TYPE_CGEV_REJECT]         = {.handler = NULL},
    [URC_TYPE_CGEV_NW_REACT]       = {.handler = NULL},
    [URC_TYPE_CGEV_NW_DEACT]       = {.handler = NULL},
    [URC_TYPE_CGEV_ME_DEACT]       = {.handler = NULL},
    [URC_TYPE_CGEV_NW_DETACH]      = {.handler = NULL},
    [URC_TYPE_CGEV_ME_DETACH]      = {.handler = NULL},
    [URC_TYPE_CGEV_NW_CLASS]       = {.handler = NULL},
    [URC_TYPE_CGEV_ME_CLASS]       = {.handler = NULL},
    [URC_TYPE_CGEV_PDP_ACT]        = {.handler = NULL},
    [URC_TYPE_CGEV_PDP_DEACT]      = {.handler = NULL},

    [URC_TYPE_CGEV_DED_NW_DED_ACT] = {.handler = NULL},
    [URC_TYPE_CGEV_ME_ACT]         = {.handler = NULL},
    [URC_TYPE_CGEV_DED_NW_DEACT]   = {.handler = NULL},
    [URC_TYPE_CGEV_NW_MODIFY]      = {.handler = NULL},
    [URC_TYPE_CGEV_ME_MODIFY]      = {.handler = NULL},

    [URC_TYPE_CREG]                = {.handler = NULL},
    [URC_TYPE_CGREG]               = {.handler = quectel_handle_urc_cgreg},
    [URC_TYPE_CEREG]               = {.handler = quectel_handle_urc_cereg},
    [URC_TYPE_QIOPEN]       = {.handler = quectel_handle_urc_socket_open},
    [URC_TYPE_QIURC_CLOSED] = {.handler = quectel_handle_urc_socket_closed},
    [URC_TYPE_QIURC_RECV]   = {.handler = quectel_handle_urc_socket_recv},
    [URC_TYPE_QIURC_INCOMING_FULL] = {.handler = NULL},
    [URC_TYPE_QIURC_INCOMING] = {.handler = quectel_handle_urc_socket_incoming},
    [URC_TYPE_QIURC_PDPDEACT] = {.handler = quectel_handle_urc_qiurc_pdpdeact},
    [URC_TYPE_QIURC_DNSGIP]   = {.handler = NULL},
    [URC_TYPE_QNTP]           = {.handler = NULL},
    [URC_TYPE_QPING]          = {.handler = NULL},
    [URC_TYPE_QSSLOPEN]       = {.handler = NULL},
    [URC_TYPE_QSSLURC_RECV]   = {.handler = NULL},
    [URC_TYPE_QSSLURC_CLOSED] = {.handler = NULL},
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_START]     = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_HTTPSTART]              = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_COAPSTART]              = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_HTTPEND]                = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_COAPEND]                = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_DOWNLOADING]            = {.handler = NULL},
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_COMPLETED] = {.handler = NULL},
    [URC_TYPE_LWM2M_FOTA_DOWNLOADED]            = {.handler = NULL},

    [URC_TYPE_QIND_FOTA_FTPSTART]               = {.handler = NULL},

    [URC_TYPE_QIND_FOTA_START_UPDATING]         = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_UPDATING]               = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_END]                    = {.handler = NULL},

    [URC_TYPE_QIND_FOTA_FTPEND]                 = {.handler = NULL},

    [URC_TYPE_QHTTPGET]                         = {.handler = NULL},
    [URC_TYPE_QHTTPPOST]                        = {.handler = NULL},
    [URC_TYPE_QHTTPPOSTFILE]                    = {.handler = NULL},
    [URC_TYPE_QHTTPPUT]                         = {.handler = NULL},
    [URC_TYPE_QHTTPPUTFILE]                     = {.handler = NULL},
    [URC_TYPE_QHTTPREAD]                        = {.handler = NULL},
    [URC_TYPE_QHTTPREADFILE]                    = {.handler = NULL},

    [URC_TYPE_QMTSNSTAT]                        = {.handler = NULL},
    [URC_TYPE_QMTSNRECV]                        = {.handler = NULL},
    [URC_TYPE_QMTSNSUB]                         = {.handler = NULL},
    [URC_TYPE_QMTSNUNS]                         = {.handler = NULL},
    [URC_TYPE_QMTSNREG]                         = {.handler = NULL},
    [URC_TYPE_QMTSNPUB]                         = {.handler = NULL},
    [URC_TYPE_QMTSN_WILLUPD]                    = {.handler = NULL},
    [URC_TYPE_QMTSNSLEEP]                       = {.handler = NULL},
    [URC_TYPE_QMTOPEN]                          = {.handler = NULL},
    [URC_TYPE_QMTCLOSE]                         = {.handler = NULL},
    [URC_TYPE_QMTSTAT]                          = {.handler = NULL},
    [URC_TYPE_QMTCONN]                          = {.handler = NULL},
    [URC_TYPE_QMTSUB]                           = {.handler = NULL},
    [URC_TYPE_QMTPUB]                           = {.handler = NULL},
    [URC_TYPE_QMTRECV]                          = {.handler = NULL},
    [URC_TYPE_QMTDISC]                          = {.handler = NULL},
    [URC_TYPE_QMTUNS]                           = {.handler = NULL},
    [URC_TYPE_QMTPING]                          = {.handler = NULL},

    [URC_TYPE_QJDR_JAMMED]                      = {.handler = NULL},
    [URC_TYPE_QJDR_NOJAMMING]                   = {.handler = NULL},
    [URC_TYPE_QLWURC_EVENT]                     = {.handler = NULL},
    [URC_TYPE_QLWURC_OBSERVE]                   = {.handler = NULL},
    [URC_TYPE_QLWURC_ULDATA]                    = {.handler = NULL},
    [URC_TYPE_QLWURC_RECV]                      = {.handler = NULL},
    [URC_TYPE_QLWREG]                           = {.handler = NULL},
    [URC_TYPE_QLWDEREG]                         = {.handler = NULL},

    [URC_TYPE_QIND_NIPD_OPEN]                   = {.handler = NULL},
    [URC_TYPE_QIND_NIPD_RECV]                   = {.handler = NULL},
    [URC_TYPE_QIND_NIPD_CLOSE]                  = {.handler = NULL},

    [URC_TYPE_QIND_GEOFENCE]                    = {.handler = NULL},

    [URC_TYPE_QSCRUB_START]                     = {.handler = NULL},
    [URC_TYPE_QSCRUB_END]                       = {.handler = NULL},

    [URC_TYPE_QCOAPOPEN]                        = {.handler = NULL},
    [URC_TYPE_QCOAPCLOSE]                       = {.handler = NULL},
    [URC_TYPE_QCOAPACK]                         = {.handler = NULL},
    [URC_TYPE_QCOAPRECV]                        = {.handler = NULL},
    [URC_TYPE_QCOAPIND]                         = {.handler = NULL},

    [URC_TYPE_QFTPOPEN]                         = {.handler = NULL},
    [URC_TYPE_QFTPCLOSE]                        = {.handler = NULL},
    [URC_TYPE_QFTPSIZE]                         = {.handler = NULL},
    [URC_TYPE_QFTPGET]                          = {.handler = NULL},
    [URC_TYPE_QFTPPUT]                          = {.handler = NULL},
    [URC_TYPE_QFTPDEL]                          = {.handler = NULL},
    [URC_TYPE_QFTPCWD]                          = {.handler = NULL},
    [URC_TYPE_QFTPMKDIR]                        = {.handler = NULL},
    [URC_TYPE_QFTPRMDIR]                        = {.handler = NULL},
    [URC_TYPE_QFTPPWD]                          = {.handler = NULL},
    [URC_TYPE_QFTPRENAME]                       = {.handler = NULL},
    [URC_TYPE_QFTPMDTM]                         = {.handler = NULL},
    [URC_TYPE_QFTPLIST]                         = {.handler = NULL},
    [URC_TYPE_QFTPNLST]                         = {.handler = NULL},
    [URC_TYPE_QFTPMLSD]                         = {.handler = NULL},
    [URC_TYPE_QFTPSTAT]                         = {.handler = NULL},
    [URC_TYPE_QFTPLEN]                          = {.handler = NULL},

    [URC_TYPE_QCAUSEINFO]                       = {.handler = NULL},

    [URC_TYPE_QIOTEVT_REG]                      = {.handler = NULL},
    [URC_TYPE_QIOTEVT_EXIT]                     = {.handler = NULL},
    [URC_TYPE_QIOTEVT_SUB]                      = {.handler = NULL},
    [URC_TYPE_QIOTEVT_RECV]                     = {.handler = NULL},
    [URC_TYPE_QIOTEVT_FOTA]                     = {.handler = NULL},
    [URC_TYPE_QIOTEVT_SEND]                     = {.handler = NULL},
    [URC_TYPE_QIOTPING]                         = {.handler = NULL},

    [URC_TYPE_QPSMTIMER]                        = {.handler = NULL},

    [URC_TYPE_QCELLSCAN]                        = {.handler = NULL},
    [URC_TYPE_CRTDCP]                           = {.handler = NULL},
    [URC_TYPE_QGPSURC_XTRA]                     = {.handler = NULL},
    [URC_TYPE_QGPSURC_JAMMING_IND]              = {.handler = NULL},
    [URC_TYPE_QGPSURC_SINGLE_SHOT]              = {.handler = NULL},

    [URC_TYPE_UNDER_VOLTAGE_WARNING]            = {.handler = NULL},
    [URC_TYPE_UNDER_VOLTAGE_POWER_DOWN]         = {.handler = NULL},
    [URC_TYPE_OVER_VOLTAGE_WARNING]             = {.handler = NULL},
    [URC_TYPE_OVER_VOLTAGE_POWER_DOWN]          = {.handler = NULL},
    [URC_TYPE_VOLTAGE_POWER_NORMAL]             = {.handler = NULL},
    [URC_TYPE_OVER_PMIC_TEMPERATURE_WARNING]    = {.handler = NULL},
    [URC_TYPE_OVER_OX_TEMPERATURE_WARNING]      = {.handler = NULL},
    [URC_TYPE_OVER_PA_TEMPERATURE_WARNING]      = {.handler = NULL},
    [URC_TYPE_NORMAL_PMIC_TEMPERATURE]          = {.handler = NULL},
    [URC_TYPE_NORMAL_OX_TEMPERATURE]            = {.handler = NULL},
    [URC_TYPE_NORMAL_PA_TEMPERATURE]            = {.handler = NULL},

    [URC_TYPE_QESIM_OTA]                        = {.handler = NULL},
    [URC_TYPE_QESIM_TRANS]                      = {.handler = NULL},
    [URC_TYPE_QESIM_DOWNLOAD]                   = {.handler = NULL},
    [URC_TYPE_KTCP_DATA]                        = {.handler = NULL},
    [URC_TYPE_KCNX_IND]                         = {.handler = NULL},
    [URC_TYPE_KTCP_IND]                         = {.handler = NULL},
    [URC_TYPE_KTCP_NOTIF]                       = {.handler = NULL},
    [URC_TYPE_KTCP_SRVREQ]                      = {.handler = NULL},
    [URC_TYPE_KUDP_DATA]                        = {.handler = NULL},
    [URC_TYPE_KUDP_NOTIF]                       = {.handler = NULL},
    [URC_TYPE_KUDP_IND]                         = {.handler = NULL},
};

modem_driver_t eg915u_driver = {
    .urc_handler                 = eg915u_urc_handler,
    .map_cme_error               = quectel_map_cme_error,
    .map_cms_error               = quectel_map_cms_error,
    .power_off_software          = quectel_module_poweroff,
    .sim_readiness_get           = quectel_sim_readiness_get,
    .sim_pin_enter               = quectel_sim_pin_enter,
    .iccid_get                   = quectel_iccid_get,
    .modem_specific_features_set = eg915u_modem_specific_features_set,
    .pdp_settings_get            = quectel_pdp_settings_get,
    .pdp_config                  = quectel_pdp_settings_set,
    .operator_info_get           = quectel_operator_info_get,
    .operator_config             = quectel_operator_settings_set,
    .rat_settings_get            = eg915u_rat_settings_get,
    .rat_config                  = eg915u_rat_settings_set,
    .service_domain_settings_get = eg915u_service_domain_get,
    .service_domain_config       = eg915u_service_domain_set,
    .pdp_context_activate        = quectel_pdp_activate,
    .pdp_context_deactivate      = quectel_pdp_deactivate,
    .pdp_status_get              = quectel_pdp_status_get,
    .firmware_version_get        = quectel_firmware_version_get,
    .cellular_info_get           = quectel_cellular_info_get,
    .socket_create               = quectel_socket_create,
    .socket_close                = quectel_socket_close,
    .socket_query_service_status = quectel_socket_query_service_status,
    .socket_check_usability      = quectel_socket_check_usability,
    .socket_send                 = quectel_socket_send,
    .socket_sent_info_get        = quectel_socket_get_data_verify,
    .socket_receive              = quectel_socket_receive,
};

at_req_status_t
eg915u_modem_specific_features_set (uint16_t * error_code)
{

    at_req_status_t ret = AT_PENDING;

    ret                 = quectel_urc_port_config(error_code);

    return ret;
}

at_req_status_t
eg915u_service_domain_get (cell_service_domain_t * service_domain,
                           uint16_t *              error_code)
{
    at_req_status_t ret = AT_SUCCESS;

    return ret;
}

at_req_status_t
eg915u_service_domain_set (cell_service_domain_t * service_domain,
                           uint16_t *              error_code)
{
    at_req_status_t ret = AT_SUCCESS;

    return ret;
}

at_req_status_t
eg915u_rat_settings_get (cell_rat_settings_t * rat_settings,
                         uint16_t *            error_code)
{
    at_req_status_t ret    = AT_PENDING;
    at_request_t    at_req = {.p_send_buff = NULL,
                              .resp_prefix = "+QCFG",
                              .p_data      = rat_settings,
                              .data_len = (uint16_t) sizeof(cell_rat_settings_t),
                              .p_error_code = error_code};

    at_req.p_send_buff     = "AT+QCFG=\"nwscanmode\"\r";
    at_req.send_buff_len   = strlen(at_req.p_send_buff);
    ret = at_command_request(&at_req, 300, at_parser_rat_mode_get);

    if (AT_SUCCESS == ret)
    {
        at_req.p_send_buff   = "AT+QCFG=\"band\"\r";
        at_req.send_buff_len = strlen(at_req.p_send_buff);
        ret = at_command_request(&at_req, 300, at_parser_band_get);
    }
    return ret;
}

at_req_status_t
eg915u_rat_settings_set (cell_rat_settings_t * rat_settings,
                         bool * is_trigger_reboot, uint16_t * error_code)
{
    at_req_status_t ret       = AT_PENDING;
    uint8_t         scan_mode = 0;

    *is_trigger_reboot        = false;
    /* Convert sequence -> nwscanmode */
    scan_mode = get_mode_from_seq(rat_settings->sequence);

    /* 1. Set nwscanmode */
    ret = rat_mode_set(scan_mode, error_code);

    /* 2. Set band */
    if (AT_SUCCESS == ret)
    {
        ret = band_set(rat_settings, error_code);
    }

    return ret;
}

static at_req_status_t
band_set (cell_rat_settings_t * rat_settings, uint16_t * error_code)
{
    at_req_status_t ret                         = AT_PENDING;
    char            cmd_buf[90]                 = {'\0'};
    char            cat1_band_mask[MAX_HEX_LEN] = {0};
    at_request_t    at_req_set_band             = {.p_send_buff  = cmd_buf,
                                                   .resp_prefix  = NULL,
                                                   .p_data       = NULL,
                                                   .data_len     = 0,
                                                   .p_error_code = error_code};

    // Calculate GSM band mask
    uint8_t gsm_band_mask = 0;
    for (uint8_t i = 0; i < CELL_GSM_BAND_MAX; i++)
    {
        if (CELL_GSM_BAND_INVALID == rat_settings->gsm_band[i])
        {
            break;
        }
        else
        {
            gsm_band_mask |= ((uint8_t) 1 << i);
        }
    }

    // Calculate LTE band masks
    helper_calculate_band_mask(rat_settings->lte_band,
                               cat1_band_mask,
                               sizeof(cat1_band_mask),
                               lte_band_index_map);

    // Build the command buffer in a single snprintf call
    snprintf(cmd_buf,
             sizeof(cmd_buf),
             "AT+QCFG=\"band\",%d,%s\r",
             gsm_band_mask,
             cat1_band_mask);
    at_req_set_band.send_buff_len = strlen(at_req_set_band.p_send_buff);

    ret = at_command_request(&at_req_set_band, 300, NULL);

    return ret;
}

static void
parse_and_update_mode (cell_rat_type_t * seq, modem_rat_mode_t mode)
{
    for (int i = 0; i < CELL_RAT_MAX; i++)
    {
        seq[i] = CELL_RAT_INVALID;
    }

    switch (mode)
    {
        case MODE_AUTO: // LTE/GSM
            seq[0] = CELL_RAT_LTE;
            seq[1] = CELL_RAT_GSM;
            break;

        case MODE_GSM: // GSM only
            seq[0] = CELL_RAT_GSM;
            break;

        case MODE_LTE: // CAT1 only
            seq[0] = CELL_RAT_LTE;
            break;
        default:
            break;
    }
}

static modem_rat_mode_t
get_mode_from_seq (cell_rat_type_t * seq)
{
    if (((CELL_RAT_GSM == seq[0]) && (CELL_RAT_LTE == seq[1]))
        || ((CELL_RAT_LTE == seq[0]) && (CELL_RAT_GSM == seq[1])))
    {
        return MODE_AUTO;
    }

    if (CELL_RAT_GSM == seq[0])
    {
        return MODE_GSM;
    }

    if (CELL_RAT_LTE == seq[0])
    {
        return MODE_LTE;
    }

    return MODE_AUTO; /* fallback AUTO */
}

static at_req_status_t
rat_mode_set (uint8_t mode, uint16_t * error_code)
{
    char cmd_buf[32] = {0};

    snprintf(cmd_buf, sizeof(cmd_buf), "AT+QCFG=\"nwscanmode\",%u\r", mode);

    at_request_t at_req = {.p_send_buff   = cmd_buf,
                           .send_buff_len = strlen(cmd_buf),
                           .resp_prefix   = NULL,
                           .p_error_code  = error_code};

    return at_command_request(&at_req, 300, NULL);
}

static at_req_status_t
at_parser_rat_mode_get (at_request_t * at_req, char * line, uint64_t start_time,
                        uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;
    modem_rat_mode_t      scan_mode    = 0;
    // Response example : +QCFG: "nwscanmode",0

    if (strstr(line, "nwscanmode") != NULL)
    {
        const char * comma = strchr(line, ',');
        if (comma != NULL)
        {
            unsigned int tmp = 0;
            if (sscanf(comma + 1, "%u", &tmp) == 1)
            {
                scan_mode = (modem_rat_mode_t) tmp;
                parse_and_update_mode(rat_settings->sequence, scan_mode);
            }
        }
    }

    ret = seek_one_line_and_parse_final_resp(
        line, start_time, timeout_ms, at_req->p_error_code);

    return ret;
}

static at_req_status_t
at_parser_band_get (at_request_t * at_req, char * line, uint64_t start_time,
                    uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;

    unsigned int          gsm_band_val_tmp                    = 0;

    char                  lte_band_val[LTE_BAND_MASK_STR_MAX] = {0};

    /*
     * Response example:
     * +QCFG: "band",0xa,0x15,0x20000000000000000
     */

    int parsed = sscanf(line,
                        "+QCFG: \"band\",0x%x,"
                        "0x%32[0123456789abcdefABCDEFxX],",
                        &gsm_band_val_tmp,
                        lte_band_val);

    if (parsed == 2)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    if (AT_SUCCESS == ret)
    {
        helper_parse_gsm_band((uint8_t) gsm_band_val_tmp,
                              rat_settings,
                              gsm_band_map,
                              gsm_band_map_size,
                              GSM_BAND_ALL);

        helper_parse_lte_band(rat_settings->lte_band,
                              lte_band_val,
                              lte_band_map,
                              lte_band_map_size);
    }

    return ret;
}
