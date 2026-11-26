#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_bg95.c"
/**
 * @file cell_modem_bg95.c
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
 * @brief Modem driver for BG95 cellular module
 * @date 2025-03-31
 */
#include "cell_modem_bg95.h"
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

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>

#define BG95_PDP_ACTIVATION_REQ_TIMEOUT_MS   (150000UL)
#define BG95_PDP_DEACTIVATION_REQ_TIMEOUT_MS (40000UL)

#define SCAN_MODE_AUTO                       0 // GSM and LTE
#define SCAN_MODE_GSM                        1
#define SCAN_MODE_LTE                        3

#define IOTOP_MODE_CATM1                     0
#define IOTOP_MODE_NBIOT                     1
#define IOTOP_MODE_CATM1_NBIOT               2
#define IOTOP_MODE_NOT_APPLICABLE            0xFF

#define SCAN_SEQ_INVALID_BIT                 0
#define SCAN_SEQ_GSM_BIT                     1
#define SCAN_SEQ_CATM1_BIT                   2
#define SCAN_SEQ_NBIOT_BIT                   4

#define SCAN_SEQ_GSM_VAL                     1
#define SCAN_SEQ_CATM1_VAL                   2
#define SCAN_SEQ_NBIOT_VAL                   3

#define GSM_BAND_NO_CHANGE                   0x0
#define GSM_BAND_EGSM900                     0x1
#define GSM_BAND_DCS1800                     0x2
#define GSM_BAND_GSM850                      0x4
#define GSM_BAND_PCS1900                     0x8
#define GSM_BAND_ALL                         0xf

#define QUECTEL_NBIOT_BITMAP_FILE_SIZE       "0400"

// BG95 LTE support up to band 85, so we need to use 128-bit (2x uint64_t) to
// store, and it can be represent by string of length 32
#define MAX_HEX_LEN           32

#define LTE_BAND_MASK_STR_MAX (2 + MAX_HEX_LEN + 1)

typedef struct
{
    uint8_t      bit;
    const char * str;
} scanseq_set_map_t;

typedef struct
{
    uint8_t         scanseq_val;
    cell_rat_type_t rat_type;
} scanseq_get_map_t;

typedef struct
{
    uint8_t scan_seq_mask;
    uint8_t scan_mode;
    uint8_t iotop_mode;
} scanmode_iotopmode_map_t;

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

    [CELL_LTE_BAND_7]  = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_8]  = 7,
    [CELL_LTE_BAND_9]  = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_10] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_12] = 11,
    [CELL_LTE_BAND_13] = 12,

    [CELL_LTE_BAND_17] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_18] = 17,
    [CELL_LTE_BAND_19] = 18,
    [CELL_LTE_BAND_20] = 19,

    [CELL_LTE_BAND_23] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_25] = 24,
    [CELL_LTE_BAND_26] = 25,
    [CELL_LTE_BAND_27] = 26,
    [CELL_LTE_BAND_28] = 27,

    [CELL_LTE_BAND_31] = 30,

    [CELL_LTE_BAND_34] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_38] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_39] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_40] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_41] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_65] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_66] = 65,
    [CELL_LTE_BAND_70] = CELL_LTE_BAND_INVALID,
    [CELL_LTE_BAND_71] = 70,
    [CELL_LTE_BAND_72] = 71,
    [CELL_LTE_BAND_73] = 72,

    [CELL_LTE_BAND_85] = 84,
};

// Band mapping based on bit index (max 128-bit value)
static const band_mapping_t catm1_band_map[] = {
    {0,  CELL_LTE_BAND_1 },
    {1,  CELL_LTE_BAND_2 },
    {2,  CELL_LTE_BAND_3 },
    {3,  CELL_LTE_BAND_4 },
    {4,  CELL_LTE_BAND_5 },
    {7,  CELL_LTE_BAND_8 },
    {11, CELL_LTE_BAND_12},
    {12, CELL_LTE_BAND_13},
    {17, CELL_LTE_BAND_18},
    {18, CELL_LTE_BAND_19},
    {19, CELL_LTE_BAND_20},
    {24, CELL_LTE_BAND_25},
    {25, CELL_LTE_BAND_26},
    {26, CELL_LTE_BAND_27},
    {27, CELL_LTE_BAND_28},
    {30, CELL_LTE_BAND_31},
    {65, CELL_LTE_BAND_66},
    {71, CELL_LTE_BAND_72},
    {72, CELL_LTE_BAND_73},
    {84, CELL_LTE_BAND_85},
};
static const size_t catm1_band_map_size
    = sizeof(catm1_band_map) / sizeof(catm1_band_map[0]);

static const band_mapping_t nbiot_band_map[] = {
    {0,  CELL_LTE_BAND_1 },
    {1,  CELL_LTE_BAND_2 },
    {2,  CELL_LTE_BAND_3 },
    {3,  CELL_LTE_BAND_4 },
    {4,  CELL_LTE_BAND_5 },
    {7,  CELL_LTE_BAND_8 },
    {11, CELL_LTE_BAND_12},
    {12, CELL_LTE_BAND_13},
    {17, CELL_LTE_BAND_18},
    {18, CELL_LTE_BAND_19},
    {19, CELL_LTE_BAND_20},
    {24, CELL_LTE_BAND_25},
    {27, CELL_LTE_BAND_28},
    {30, CELL_LTE_BAND_31},
    {65, CELL_LTE_BAND_66},
    {70, CELL_LTE_BAND_71}, // nbiot only
    {71, CELL_LTE_BAND_72},
    {72, CELL_LTE_BAND_73},
    {84, CELL_LTE_BAND_85},
};
static const size_t nbiot_band_map_size
    = sizeof(nbiot_band_map) / sizeof(nbiot_band_map[0]);

static const scanseq_set_map_t scanseq_set_map[] = {
    [CELL_RAT_GSM]     = {SCAN_SEQ_GSM_BIT,     "01"},
    [CELL_RAT_CATM1]   = {SCAN_SEQ_CATM1_BIT,   "02"},
    [CELL_RAT_NBIOT]   = {SCAN_SEQ_NBIOT_BIT,   "03"},
    [CELL_RAT_INVALID] = {SCAN_SEQ_INVALID_BIT, "\0"}
};

static const scanmode_iotopmode_map_t scanmode_iotopmode_map[] = {
    {SCAN_SEQ_GSM_BIT,                                           SCAN_MODE_GSM,  IOTOP_MODE_NOT_APPLICABLE},
    {SCAN_SEQ_CATM1_BIT,                                         SCAN_MODE_LTE,  IOTOP_MODE_CATM1         },
    {SCAN_SEQ_NBIOT_BIT,                                         SCAN_MODE_LTE,  IOTOP_MODE_NBIOT         },
    {SCAN_SEQ_GSM_BIT | SCAN_SEQ_CATM1_BIT,                      SCAN_MODE_AUTO, IOTOP_MODE_CATM1         },
    {SCAN_SEQ_GSM_BIT | SCAN_SEQ_NBIOT_BIT,                      SCAN_MODE_AUTO, IOTOP_MODE_NBIOT         },
    {SCAN_SEQ_CATM1_BIT | SCAN_SEQ_NBIOT_BIT,
     SCAN_MODE_LTE,                                                              IOTOP_MODE_CATM1_NBIOT   },
    {SCAN_SEQ_GSM_BIT | SCAN_SEQ_CATM1_BIT | SCAN_SEQ_NBIOT_BIT,
     SCAN_MODE_AUTO,                                                             IOTOP_MODE_CATM1_NBIOT   }
};

static at_req_status_t bg95_nb1_cap_feature_bitmap_set(uint16_t * error_code);

static at_req_status_t bg95_nb1_cap_feature_bitmap_get(char *     file_len,
                                                       uint16_t * error_code);

static void            bg95_urc_dfota_handler(void * data);

static at_req_status_t at_parser_net_category_get(at_request_t * at_req,
                                                  char *         line,
                                                  uint64_t       start_time,
                                                  uint32_t       timeout_ms);

static at_req_status_t at_parser_rat_seq_get(at_request_t * at_req, char * line,
                                             uint64_t start_time,
                                             uint32_t timeout_ms);

static at_req_status_t at_parser_service_domain_get(at_request_t * at_req,
                                                    char *         line,
                                                    uint64_t       start_time,
                                                    uint32_t       timeout_ms);

static at_req_status_t at_parser_band_get(at_request_t * at_req, char * line,
                                          uint64_t start_time,
                                          uint32_t timeout_ms);

static at_req_status_t rat_seq_set(cell_rat_type_t * rat_seq,
                                   uint16_t *        error_code);

static at_req_status_t net_category_set(uint8_t *  net_category,
                                        uint16_t * error_code);

static at_req_status_t band_set(cell_rat_settings_t * rat_settings,
                                uint16_t *            error_code);

static void calc_iotop_mode_scan_mode_from_scan_seq(cell_rat_type_t * rat_seq,
                                                    uint8_t * iotop_mode,
                                                    uint8_t * scan_mode);

static at_req_status_t update_scan_seq_from_iotop_mode(
    cell_rat_type_t * rat_seq, uint8_t iotop_mode);

static void            parse_and_update_rat_seq(cell_rat_type_t * rat_seq,
                                                const char *      seq_str);

static at_req_status_t at_parser_nb1_cap_feature_bitmap_get(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t at_parser_storage_space_get(at_request_t * at_req,
                                                   char *         line,
                                                   uint64_t       start_time,
                                                   uint32_t       timeout_ms);

static at_req_status_t at_parser_file_open(at_request_t * at_req, char * line,
                                           uint64_t start_time,
                                           uint32_t timeout_ms);

static at_req_status_t at_parser_dfota_write(at_request_t * at_req, char * line,
                                             uint64_t start_time,
                                             uint32_t timeout_ms);

static at_req_status_t at_parser_scrub_direction_get(at_request_t * at_req,
                                                     char *         line,
                                                     uint64_t       start_time,
                                                     uint32_t       timeout_ms);

static urc_map_handler_t bg95_urc_handler[URC_TYPE_NUMBER] = {
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
    [URC_TYPE_CGREG]               = {.handler = NULL},
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
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_START] = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_HTTPSTART]          = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_COAPSTART]          = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_HTTPEND]     = {.handler = bg95_urc_dfota_handler},
    [URC_TYPE_QIND_FOTA_COAPEND]     = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_DOWNLOADING] = {.handler = bg95_urc_dfota_handler},
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_COMPLETED] = {.handler = NULL},
    [URC_TYPE_LWM2M_FOTA_DOWNLOADED]            = {.handler = NULL},

    [URC_TYPE_QIND_FOTA_FTPSTART]               = {.handler = NULL},

    [URC_TYPE_QIND_FOTA_START_UPDATING]         = {.handler = NULL},
    [URC_TYPE_QIND_FOTA_UPDATING]       = {.handler = bg95_urc_dfota_handler},
    [URC_TYPE_QIND_FOTA_END]            = {.handler = bg95_urc_dfota_handler},

    [URC_TYPE_QIND_FOTA_FTPEND]         = {.handler = NULL},

    [URC_TYPE_QHTTPGET]                 = {.handler = NULL},
    [URC_TYPE_QHTTPPOST]                = {.handler = NULL},
    [URC_TYPE_QHTTPPOSTFILE]            = {.handler = NULL},
    [URC_TYPE_QHTTPPUT]                 = {.handler = NULL},
    [URC_TYPE_QHTTPPUTFILE]             = {.handler = NULL},
    [URC_TYPE_QHTTPREAD]                = {.handler = NULL},
    [URC_TYPE_QHTTPREADFILE]            = {.handler = NULL},

    [URC_TYPE_QMTSNSTAT]                = {.handler = NULL},
    [URC_TYPE_QMTSNRECV]                = {.handler = NULL},
    [URC_TYPE_QMTSNSUB]                 = {.handler = NULL},
    [URC_TYPE_QMTSNUNS]                 = {.handler = NULL},
    [URC_TYPE_QMTSNREG]                 = {.handler = NULL},
    [URC_TYPE_QMTSNPUB]                 = {.handler = NULL},
    [URC_TYPE_QMTSN_WILLUPD]            = {.handler = NULL},
    [URC_TYPE_QMTSNSLEEP]               = {.handler = NULL},
    [URC_TYPE_QMTOPEN]                  = {.handler = NULL},
    [URC_TYPE_QMTCLOSE]                 = {.handler = NULL},
    [URC_TYPE_QMTSTAT]                  = {.handler = NULL},
    [URC_TYPE_QMTCONN]                  = {.handler = NULL},
    [URC_TYPE_QMTSUB]                   = {.handler = NULL},
    [URC_TYPE_QMTPUB]                   = {.handler = NULL},
    [URC_TYPE_QMTRECV]                  = {.handler = NULL},
    [URC_TYPE_QMTDISC]                  = {.handler = NULL},
    [URC_TYPE_QMTUNS]                   = {.handler = NULL},
    [URC_TYPE_QMTPING]                  = {.handler = NULL},

    [URC_TYPE_QJDR_JAMMED]              = {.handler = NULL},
    [URC_TYPE_QJDR_NOJAMMING]           = {.handler = NULL},
    [URC_TYPE_QLWURC_EVENT]             = {.handler = NULL},
    [URC_TYPE_QLWURC_OBSERVE]           = {.handler = NULL},
    [URC_TYPE_QLWURC_ULDATA]            = {.handler = NULL},
    [URC_TYPE_QLWURC_RECV]              = {.handler = NULL},
    [URC_TYPE_QLWREG]                   = {.handler = NULL},
    [URC_TYPE_QLWDEREG]                 = {.handler = NULL},

    [URC_TYPE_QIND_NIPD_OPEN]           = {.handler = NULL},
    [URC_TYPE_QIND_NIPD_RECV]           = {.handler = NULL},
    [URC_TYPE_QIND_NIPD_CLOSE]          = {.handler = NULL},

    [URC_TYPE_QIND_GEOFENCE]            = {.handler = NULL},

    [URC_TYPE_QSCRUB_START]             = {.handler = NULL},
    [URC_TYPE_QSCRUB_END]               = {.handler = NULL},

    [URC_TYPE_QCOAPOPEN]                = {.handler = NULL},
    [URC_TYPE_QCOAPCLOSE]               = {.handler = NULL},
    [URC_TYPE_QCOAPACK]                 = {.handler = NULL},
    [URC_TYPE_QCOAPRECV]                = {.handler = NULL},
    [URC_TYPE_QCOAPIND]                 = {.handler = NULL},

    [URC_TYPE_QFTPOPEN]                 = {.handler = NULL},
    [URC_TYPE_QFTPCLOSE]                = {.handler = NULL},
    [URC_TYPE_QFTPSIZE]                 = {.handler = NULL},
    [URC_TYPE_QFTPGET]                  = {.handler = NULL},
    [URC_TYPE_QFTPPUT]                  = {.handler = NULL},
    [URC_TYPE_QFTPDEL]                  = {.handler = NULL},
    [URC_TYPE_QFTPCWD]                  = {.handler = NULL},
    [URC_TYPE_QFTPMKDIR]                = {.handler = NULL},
    [URC_TYPE_QFTPRMDIR]                = {.handler = NULL},
    [URC_TYPE_QFTPPWD]                  = {.handler = NULL},
    [URC_TYPE_QFTPRENAME]               = {.handler = NULL},
    [URC_TYPE_QFTPMDTM]                 = {.handler = NULL},
    [URC_TYPE_QFTPLIST]                 = {.handler = NULL},
    [URC_TYPE_QFTPNLST]                 = {.handler = NULL},
    [URC_TYPE_QFTPMLSD]                 = {.handler = NULL},
    [URC_TYPE_QFTPSTAT]                 = {.handler = NULL},
    [URC_TYPE_QFTPLEN]                  = {.handler = NULL},

    [URC_TYPE_QCAUSEINFO]               = {.handler = NULL},

    [URC_TYPE_QIOTEVT_REG]              = {.handler = NULL},
    [URC_TYPE_QIOTEVT_EXIT]             = {.handler = NULL},
    [URC_TYPE_QIOTEVT_SUB]              = {.handler = NULL},
    [URC_TYPE_QIOTEVT_RECV]             = {.handler = NULL},
    [URC_TYPE_QIOTEVT_FOTA]             = {.handler = NULL},
    [URC_TYPE_QIOTEVT_SEND]             = {.handler = NULL},
    [URC_TYPE_QIOTPING]                 = {.handler = NULL},

    [URC_TYPE_QPSMTIMER]                = {.handler = NULL},

    [URC_TYPE_QCELLSCAN]                = {.handler = NULL},
    [URC_TYPE_CRTDCP]                   = {.handler = NULL},
    [URC_TYPE_QGPSURC_XTRA]             = {.handler = NULL},
    [URC_TYPE_QGPSURC_JAMMING_IND]      = {.handler = NULL},
    [URC_TYPE_QGPSURC_SINGLE_SHOT]      = {.handler = NULL},

    [URC_TYPE_UNDER_VOLTAGE_WARNING]    = {.handler = NULL},
    [URC_TYPE_UNDER_VOLTAGE_POWER_DOWN] = {.handler = NULL},
    [URC_TYPE_OVER_VOLTAGE_WARNING]     = {.handler = NULL},
    [URC_TYPE_OVER_VOLTAGE_POWER_DOWN]  = {.handler = NULL},
    [URC_TYPE_VOLTAGE_POWER_NORMAL]     = {.handler = NULL},
    [URC_TYPE_OVER_PMIC_TEMPERATURE_WARNING] = {.handler = NULL},
    [URC_TYPE_OVER_OX_TEMPERATURE_WARNING]   = {.handler = NULL},
    [URC_TYPE_OVER_PA_TEMPERATURE_WARNING]   = {.handler = NULL},
    [URC_TYPE_NORMAL_PMIC_TEMPERATURE]       = {.handler = NULL},
    [URC_TYPE_NORMAL_OX_TEMPERATURE]         = {.handler = NULL},
    [URC_TYPE_NORMAL_PA_TEMPERATURE]         = {.handler = NULL},

    [URC_TYPE_QESIM_OTA]                     = {.handler = NULL},
    [URC_TYPE_QESIM_TRANS]                   = {.handler = NULL},
    [URC_TYPE_QESIM_DOWNLOAD]                = {.handler = NULL},
    [URC_TYPE_KTCP_DATA]                     = {.handler = NULL},
    [URC_TYPE_KCNX_IND]                      = {.handler = NULL},
    [URC_TYPE_KTCP_IND]                      = {.handler = NULL},
    [URC_TYPE_KTCP_NOTIF]                    = {.handler = NULL},
    [URC_TYPE_KTCP_SRVREQ]                   = {.handler = NULL},
    [URC_TYPE_KUDP_DATA]                     = {.handler = NULL},
    [URC_TYPE_KUDP_NOTIF]                    = {.handler = NULL},
    [URC_TYPE_KUDP_IND]                      = {.handler = NULL},
};

modem_driver_t bg95_driver
    = {.urc_handler                 = bg95_urc_handler,
       .map_cme_error               = quectel_map_cme_error,
       .map_cms_error               = quectel_map_cms_error,
       .power_off_software          = quectel_module_poweroff,
       .sim_readiness_get           = quectel_sim_readiness_get,
       .sim_pin_enter               = quectel_sim_pin_enter,
       .iccid_get                   = quectel_iccid_get,
       .modem_specific_features_set = bg95_modem_specific_features_set,
       .pdp_settings_get            = quectel_pdp_settings_get,
       .pdp_config                  = quectel_pdp_settings_set,
       .operator_info_get           = quectel_operator_info_get,
       .operator_config             = quectel_operator_settings_set,
       .rat_settings_get            = bg95_rat_settings_get,
       .rat_config                  = bg95_rat_settings_set,
       .service_domain_settings_get = bg95_service_domain_get,
       .service_domain_config       = bg95_service_domain_set,
       .pdp_context_activate        = quectel_pdp_activate,
       .pdp_context_deactivate      = quectel_pdp_deactivate,
       .pdp_status_get              = quectel_pdp_status_get,
       .firmware_version_get        = quectel_firmware_version_get,
       .cellular_info_get           = quectel_cellular_info_get,
       .dfota_local_init            = bg95_dfota_local_init,
       .dfota_local_update          = bg95_dfota_local_update,
       .dfota_wireless_init         = bg95_dfota_wireless_init,
       .dfota_wireless_start        = bg95_dfota_wireless_start,
       .dfota_download_cancel       = bg95_dfota_download_cancel,
       .dfota_write                 = bg95_dfota_write,
       .socket_create               = quectel_socket_create,
       .socket_send                 = quectel_socket_send,
       .socket_sent_info_get        = quectel_socket_get_data_verify,
       .socket_receive              = quectel_socket_receive,
       .socket_close                = quectel_socket_close,
       .socket_query_service_status = quectel_socket_query_service_status,
       .socket_check_usability      = quectel_socket_check_usability,
       .rat_priority_supported      = true};

static uint32_t g_fota_file_handle
    = 0; /**< This variable is used for handling file of DFOTA local. */

at_req_status_t
bg95_modem_specific_features_set (uint16_t * error_code)
{
    at_req_status_t ret          = AT_PENDING;
    char            file_len[32] = {'\0'};
    ret = bg95_nb1_cap_feature_bitmap_get(file_len, error_code);
    if ((AT_SUCCESS == ret)
        && (0 != strcmp(QUECTEL_NBIOT_BITMAP_FILE_SIZE, file_len)))
    {
        ret = bg95_nb1_cap_feature_bitmap_set(error_code);
    }

    if (AT_SUCCESS == ret)
    {
        ret = quectel_urc_port_config(error_code);
    }

    return ret;
}

at_req_status_t
bg95_rat_settings_get (cell_rat_settings_t * rat_settings,
                       uint16_t *            error_code)
{
    at_req_status_t ret    = AT_PENDING;
    at_request_t    at_req = {.p_send_buff = NULL,
                              .resp_prefix = "+QCFG",
                              .p_data      = rat_settings,
                              .data_len = (uint16_t) sizeof(cell_rat_settings_t),
                              .p_error_code = error_code};

    at_req.p_send_buff     = "AT+QCFG=\"nwscanseq\"\r";
    at_req.send_buff_len   = strlen(at_req.p_send_buff);
    ret = at_command_request(&at_req, 300, at_parser_rat_seq_get);

    if (AT_SUCCESS == ret)
    {
        at_req.p_send_buff   = "AT+QCFG=\"iotopmode\"\r";
        at_req.send_buff_len = strlen(at_req.p_send_buff);
        ret = at_command_request(&at_req, 300, at_parser_net_category_get);
    }

    if (AT_SUCCESS == ret)
    {
        at_req.p_send_buff   = "AT+QCFG=\"band\"\r";
        at_req.send_buff_len = strlen(at_req.p_send_buff);
        ret = at_command_request(&at_req, 300, at_parser_band_get);
    }

    return ret;
}

at_req_status_t
bg95_rat_settings_set (cell_rat_settings_t * rat_settings,
                       bool * is_trigger_reboot, uint16_t * error_code)
{
    at_req_status_t ret        = AT_PENDING;
    uint8_t         scan_mode  = 0;
    uint8_t         iotop_mode = 0;

    *is_trigger_reboot         = false;
    calc_iotop_mode_scan_mode_from_scan_seq(
        rat_settings->sequence, &iotop_mode, &scan_mode);

    ret = rat_seq_set(rat_settings->sequence, error_code);

    if (AT_SUCCESS == ret)
    {
        ret = net_category_set(&iotop_mode, error_code);
    }

    if (AT_SUCCESS == ret)
    {
        ret = band_set(rat_settings, error_code);
    }

    return ret;
}

at_req_status_t
bg95_service_domain_get (cell_service_domain_t * service_domain,
                         uint16_t *              error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_service_domain
        = {.p_send_buff  = "AT+QCFG=\"servicedomain\"\r",
           .resp_prefix  = "+QCFG",
           .p_data       = service_domain,
           .data_len     = (uint16_t) sizeof(cell_service_domain_t),
           .p_error_code = error_code};

    at_req_get_service_domain.send_buff_len
        = strlen(at_req_get_service_domain.p_send_buff);
    ret = at_command_request(
        &at_req_get_service_domain, 300, at_parser_service_domain_get);

    return ret;
}

at_req_status_t
bg95_service_domain_set (cell_service_domain_t * service_domain,
                         uint16_t *              error_code)
{
    at_req_status_t ret                       = AT_PENDING;
    char            cmd_buf[32]               = {'\0'};
    at_request_t    at_req_set_service_domain = {.p_send_buff  = cmd_buf,
                                                 .resp_prefix  = NULL,
                                                 .p_data       = NULL,
                                                 .data_len     = 0,
                                                 .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "%s,%d,1\r",
                    "AT+QCFG=\"servicedomain\"",
                    (int) *service_domain);
    at_req_set_service_domain.send_buff_len
        = strlen(at_req_set_service_domain.p_send_buff);

    ret = at_command_request(&at_req_set_service_domain, 300, NULL);

    return ret;
}

at_req_status_t
bg95_storage_free (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[20] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QFDEL=\"EUFS:*\"\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
bg95_storage_space_get (uint32_t * free_size, uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[20] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = "+QFLDS",
                                   .p_data       = free_size,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QFLDS=\"EUFS\"\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(
        &at_req, AT_COMMAND_COMMON_TIMEOUT_MS, at_parser_storage_space_get);

    return at_status;
}

at_req_status_t
bg95_file_open (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[64] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = "+QFOPEN",
                                   .p_data       = &g_fota_file_handle,
                                   .data_len     = sizeof(uint16_t),
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+QFOPEN=\"EUFS:/ufs/firmware_upgrade_file.bin\",0\r");

    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(
        &at_req, AT_COMMAND_COMMON_TIMEOUT_MS, at_parser_file_open);

    return at_status;
}

at_req_status_t
bg95_dfota_write (cell_dfota_block_t * block, uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;
    if (NULL != block)
    {
        at_request_t at_req;
        memset(&at_req, 0x0, sizeof(at_req));
        char cmd_buf[32]    = {'\0'};
        at_req.p_send_buff  = cmd_buf;
        at_req.resp_prefix  = NULL;
        at_req.p_data       = NULL;
        at_req.data_len     = 0;
        at_req.p_error_code = error_code;

        (void) snprintf(cmd_buf,
                        sizeof(cmd_buf),
                        "AT+QFWRITE=%u,%u\r",
                        g_fota_file_handle,
                        (unsigned int) block->len);

        at_req.send_buff_len = strlen(at_req.p_send_buff);
        at_status
            = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

        if ((AT_SUCCESS == at_status) && (NULL != block->data))
        {
            at_req.p_send_buff   = block->data;
            at_req.resp_prefix   = "+QFWRITE";
            at_req.p_data        = NULL;
            at_req.data_len      = 0;
            at_req.p_error_code  = error_code;

            at_req.send_buff_len = block->len;
            at_status
                = at_command_request(&at_req, 10000, at_parser_dfota_write);
        }
    }
    else
    {
        at_status = AT_ERROR_GENERIC;
    }
    return at_status;
}

at_req_status_t
bg95_file_close (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[64] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(
        cmd_buf, sizeof(cmd_buf), "AT+QFCLOSE=%u\r", g_fota_file_handle);
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
bg95_scrub_direction_get (bg95_scrub_direction_t * dir, uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;
    at_request_t at_req  = {.p_send_buff  = "AT+QSCRUBCFG=\"scrubdirection\"\r",
                            .resp_prefix  = "+QSCRUBCFG",
                            .p_data       = dir,
                            .data_len     = sizeof(bg95_scrub_direction_t),
                            .p_error_code = error_code};

    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(
        &at_req, AT_COMMAND_COMMON_TIMEOUT_MS, at_parser_scrub_direction_get);

    return at_status;
}

at_req_status_t
bg95_scrub_flash (uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;
    at_request_t    at_req    = {.p_send_buff  = "AT+QSCRUB=1\r",
                                 .resp_prefix  = NULL,
                                 .p_data       = NULL,
                                 .data_len     = 0,
                                 .p_error_code = error_code};

    at_req.send_buff_len      = strlen(at_req.p_send_buff);
    at_status                 = at_command_request(&at_req, 90000, NULL);

    return at_status;
}

at_req_status_t
bg95_delta_package_copy_to_flash (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[64] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+QFOTADL=\"EUFS:firmware_upgrade_file.bin\",2\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(&at_req, 10000, NULL);

    return at_status;
}

at_req_status_t
bg95_dfota_local_trigger (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[32] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QFOTADL=1\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(&at_req, 600000, NULL);

    return at_status;
}

at_req_status_t
bg95_dfota_config_update_after_download (uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[32] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QFOTADL=3,1\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(&at_req, 10000, NULL);

    return at_status;
}

at_req_status_t
bg95_dfota_download_cancel (void * arg, uint16_t * error_code)
{
    at_req_status_t at_status   = AT_PENDING;
    char            cmd_buf[32] = {'\0'};
    at_request_t    at_req      = {.p_send_buff  = cmd_buf,
                                   .resp_prefix  = NULL,
                                   .p_data       = NULL,
                                   .data_len     = 0,
                                   .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+QFOTADL=6\r");
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status            = at_command_request(&at_req, 10000, NULL);

    return at_status;
}

at_req_status_t
bg95_dfota_local_init (cell_dfota_settings_t * settings, uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;

    at_status                 = bg95_storage_free(error_code);

    uint32_t free_size        = 0;
    if (AT_SUCCESS == at_status)
    {
        at_status = bg95_storage_space_get(&free_size, error_code);
    }

    if ((AT_SUCCESS == at_status) && (settings->block_num * 1000 < free_size))
    {
        at_status = bg95_file_open(error_code);
    }

    return at_status;
}

at_req_status_t
bg95_dfota_local_update (void * arg, uint16_t * error_code)
{
    at_req_status_t at_status        = AT_PENDING;

    at_status                        = bg95_file_close(error_code);

    bg95_scrub_direction_t scrub_dir = BG95_SCRUB_DIRECTION_INVALID;
    if (AT_SUCCESS == at_status)
    {
        at_status = bg95_scrub_direction_get(&scrub_dir, error_code);
    }

    if ((AT_SUCCESS == at_status)
        && (BG95_SCRUB_DIRECTION_FORWARD != scrub_dir))
    {
        at_status = bg95_scrub_flash(error_code);
    }

    if (AT_SUCCESS == at_status)
    {
        at_status = bg95_delta_package_copy_to_flash(error_code);
    }

    if (AT_SUCCESS == at_status)
    {
        at_status = bg95_dfota_local_trigger(error_code);
    }

    return at_status;
}

at_req_status_t
bg95_dfota_wireless_init (cell_dfota_settings_t * settings,
                          uint16_t *              error_code)
{
    at_req_status_t     at_status = AT_PENDING;
    cell_network_info_t net_info;
    memset(&net_info, 0, sizeof(net_info));

    at_status = quectel_pdp_status_get(&net_info, error_code);

    if ((AT_SUCCESS == at_status)
        && (CELL_PDP_CONTEXT_STATE_ACTIVATED == net_info.state))
    {
        at_status = bg95_fota_apn_config(error_code);
    }
    else
    {
        at_status = AT_ERROR_GENERIC;
    }

    if (AT_SUCCESS == at_status)
    {
        at_status = bg95_dfota_config_update_after_download(error_code);
    }

    return at_status;
}

at_req_status_t
bg95_dfota_wireless_start (cell_dfota_settings_t * settings,
                           uint16_t *              error_code)
{
    at_req_status_t at_status = AT_PENDING;
    if (NULL != settings)
    {
        char         cmd_buf[145] = {'\0'};
        at_request_t at_req       = {.p_send_buff  = cmd_buf,
                                     .resp_prefix  = NULL,
                                     .p_data       = NULL,
                                     .data_len     = 0,
                                     .p_error_code = error_code};

        (void) snprintf(
            cmd_buf, sizeof(cmd_buf), "AT+QFOTADL=\"%s\"\r", settings->url);
        at_req.send_buff_len = strlen(at_req.p_send_buff);
        at_status
            = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);
    }
    else
    {
        at_status = AT_ERROR_GENERIC;
    }
    return at_status;
}

at_req_status_t
bg95_fota_apn_config (uint16_t * error_code)
{
    at_req_status_t     at_status = AT_PENDING;
    cell_pdp_settings_t pdp_settings;
    memset(&pdp_settings, 0, sizeof(pdp_settings));
    pdp_settings.context_id = 1;

    at_status = quectel_pdp_settings_get(&pdp_settings, error_code);

    char         cmd_buf[64]  = {'\0'};
    at_request_t at_req       = {.p_send_buff  = cmd_buf,
                                 .resp_prefix  = NULL,
                                 .p_data       = NULL,
                                 .data_len     = 0,
                                 .p_error_code = error_code};
    int          ip_family    = 0;
    int          apn_name_len = sizeof(pdp_settings.apn_name);
    if (AT_SUCCESS == at_status)
    {
        switch (pdp_settings.type)
        {
            case CELL_PDP_CONTEXT_IPV6:
                ip_family = 1;
                break;
            case CELL_PDP_CONTEXT_IPV4V6:
                ip_family = 2;
                break;
            default:
                break;
        }
        (void) snprintf(cmd_buf,
                        sizeof(cmd_buf),
                        "AT+QCFGEXT=\"fota_apn\",%d,\"%.*s\"\r",
                        ip_family,
                        apn_name_len,
                        pdp_settings.apn_name);
    }
    at_req.send_buff_len = strlen(at_req.p_send_buff);
    at_status = at_command_request(&at_req, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return at_status;
}

/**
 * @brief Writes hexadecimal data to a NB1 NV item file in the embedded file
 * system (EFS).
 *
 * @param[out] error_code Store CME or CMS error code.
 * @return Returns a status of type `at_req_status_t` indicating the result of
 * the operation.
 */
static at_req_status_t
bg95_nb1_cap_feature_bitmap_set (uint16_t * error_code)
{
    at_req_status_t ret                  = AT_PENDING;
    char            cmd_buf[80]          = {'\0'};
    at_request_t    at_req_multi_carrier = {.p_send_buff  = cmd_buf,
                                            .resp_prefix  = NULL,
                                            .p_data       = NULL,
                                            .data_len     = 0,
                                            .p_error_code = error_code};

    (void) snprintf(
        cmd_buf,
        sizeof(cmd_buf),
        "AT+QNVFC=\"/nv/item_files/modem/nb1/rrc/cap/nb1_cap_feature_bm\",%s\r",
        QUECTEL_NBIOT_BITMAP_FILE_SIZE);
    at_req_multi_carrier.send_buff_len
        = strlen(at_req_multi_carrier.p_send_buff);
    ret = at_command_request(
        &at_req_multi_carrier, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return ret;
}

/**
 * @brief Reads the contents of a NB1 NV item file from the embedded file system
 * (EFS).
 *
 * @param file_len
 * @param[out] error_code Store CME or CMS error code.
 * @return Returns a status of type `at_req_status_t` indicating the result of
 * the operation.
 */
static at_req_status_t
bg95_nb1_cap_feature_bitmap_get (char * file_len, uint16_t * error_code)
{
    at_req_status_t ret    = AT_PENDING;
    at_request_t    at_req = {
           .p_send_buff
        = "AT+QNVFR=\"/nv/item_files/modem/nb1/rrc/cap/nb1_cap_feature_bm\"\r",
           .resp_prefix  = "+QNVFR",
           .p_data       = file_len,
           .data_len     = 0,
           .p_error_code = error_code};

    at_req.send_buff_len = strlen(at_req.p_send_buff);
    ret                  = at_command_request(&at_req,
                             AT_COMMAND_COMMON_TIMEOUT_MS,
                             at_parser_nb1_cap_feature_bitmap_get);

    return ret;
}

/**
 * @brief This function parse the iotop mode and updates the scan sequence based
 * on the parsed iotop mode.
 * @Example scan_sequence[3] = {CELL_RAT_NBIOT, CELL_RAT_CATM1,
 * CELL_RAT_GSM}, if iotop_mode = 1 (NBIoT) => update scan_sequence so that
 * CELL_RAT_CATM1 is not scanned, now scan_sequence[3] = {CELL_RAT_NBIOT,
 * CELL_RAT_INVALID, CELL_RAT_GSM}
 *
 */
static at_req_status_t
at_parser_net_category_get (at_request_t * at_req, char * line,
                            uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;

    // Response example : +QCFG: "iotopmode",0
    uint8_t iotop_mode = 0;
    int     parsed = sscanf(line, "+QCFG: \"iotopmode\",%2hhu", &iotop_mode);
    if (parsed == 1)
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
        ret = update_scan_seq_from_iotop_mode(rat_settings->sequence,
                                              iotop_mode);
    }

    return ret;
}

static at_req_status_t
at_parser_rat_seq_get (at_request_t * at_req, char * line, uint64_t start_time,
                       uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;
    char                  seq_str[16]  = {0};

    // Response example : +QCFG: "nwscanseq",0
    int parsed = sscanf(line, "+QCFG: \"nwscanseq\",%15s", seq_str);

    if (parsed >= 1)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);

        if (AT_SUCCESS == ret)
        {
            parse_and_update_rat_seq(rat_settings->sequence, seq_str);
        }
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    return ret;
}

static at_req_status_t
at_parser_service_domain_get (at_request_t * at_req, char * line,
                              uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t         ret = AT_SUCCESS;
    cell_service_domain_t * service_domain
        = (cell_service_domain_t *) at_req->p_data;

    // Response example : +QCFG: "servicedomain",0
    int parsed = sscanf(
        line, "+QCFG: \"servicedomain\",%2hhu", (uint8_t *) service_domain);
    if (parsed == 1)
    {
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

static at_req_status_t
at_parser_band_get (at_request_t * at_req, char * line, uint64_t start_time,
                    uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;

    unsigned int          gsm_band_val_tmp                      = 0;

    char                  catm1_band_val[LTE_BAND_MASK_STR_MAX] = {0};
    char                  nbiot_band_val[LTE_BAND_MASK_STR_MAX] = {0};

    /*
     * Response example:
     * +QCFG: "band",0xa,0x15,0x20000000000000000
     */

    int parsed = sscanf(line,
                        "+QCFG: \"band\",0x%x,"
                        "0x%32[0123456789abcdefABCDEFxX],"
                        "0x%32[0123456789abcdefABCDEFxX]",
                        &gsm_band_val_tmp,
                        catm1_band_val,
                        nbiot_band_val);

    if (parsed == 3)
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

        helper_parse_lte_band(rat_settings->catm1_band,
                              catm1_band_val,
                              catm1_band_map,
                              catm1_band_map_size);

        helper_parse_lte_band(rat_settings->nbiot_band,
                              nbiot_band_val,
                              nbiot_band_map,
                              nbiot_band_map_size);
    }

    return ret;
}

static at_req_status_t
rat_seq_set (cell_rat_type_t * rat_seq, uint16_t * error_code)
{
    at_req_status_t ret                = AT_PENDING;
    char            cmd_buf[32]        = {'\0'};
    at_request_t    at_req_set_rat_seq = {.p_send_buff  = cmd_buf,
                                          .resp_prefix  = NULL,
                                          .p_data       = NULL,
                                          .data_len     = 0,
                                          .p_error_code = error_code};

    (void) strcpy(cmd_buf, "AT+QCFG=\"nwscanseq\",");

    for (uint8_t i = 0; i < CELL_RAT_MAX; i++)
    {
        if (SCAN_SEQ_INVALID_BIT != scanseq_set_map[rat_seq[i]].bit
            && (sizeof(cmd_buf) > strlen(scanseq_set_map[rat_seq[i]].str)))
        {
            (void) strcat(cmd_buf, scanseq_set_map[rat_seq[i]].str);
        }
    }
    (void) strcat(cmd_buf, "\r");

    if (AT_PENDING == ret)
    {
        at_req_set_rat_seq.send_buff_len
            = strlen(at_req_set_rat_seq.p_send_buff);
        ret = at_command_request(&at_req_set_rat_seq, 300, NULL);
    }

    return ret;
}

static at_req_status_t
net_category_set (uint8_t * net_category, uint16_t * error_code)
{
    at_req_status_t ret                     = AT_PENDING;
    char            cmd_buf[32]             = {'\0'};
    at_request_t    at_req_set_net_category = {.p_send_buff  = cmd_buf,
                                               .resp_prefix  = NULL,
                                               .p_data       = NULL,
                                               .data_len     = 0,
                                               .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "%s,%d,1\r",
                    "AT+QCFG=\"iotopmode\"",
                    *net_category);
    at_req_set_net_category.send_buff_len
        = strlen(at_req_set_net_category.p_send_buff);

    ret = at_command_request(&at_req_set_net_category, 300, NULL);

    return ret;
}

static at_req_status_t
band_set (cell_rat_settings_t * rat_settings, uint16_t * error_code)
{
    at_req_status_t ret                          = AT_PENDING;
    char            cmd_buf[90]                  = {'\0'};
    char            catm1_band_mask[MAX_HEX_LEN] = {0};
    char            nbiot_band_mask[MAX_HEX_LEN] = {0};
    at_request_t    at_req_set_band              = {.p_send_buff  = cmd_buf,
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

    helper_calculate_band_mask(rat_settings->catm1_band,
                               catm1_band_mask,
                               sizeof(catm1_band_mask),
                               lte_band_index_map);

    helper_calculate_band_mask(rat_settings->nbiot_band,
                               nbiot_band_mask,
                               sizeof(nbiot_band_mask),
                               lte_band_index_map);

    // Build the command buffer in a single snprintf call
    snprintf(cmd_buf,
             sizeof(cmd_buf),
             "AT+QCFG=\"band\",%d,%s,%s,1\r",
             gsm_band_mask,
             catm1_band_mask,
             nbiot_band_mask);
    at_req_set_band.send_buff_len = strlen(at_req_set_band.p_send_buff);

    ret = at_command_request(&at_req_set_band, 300, NULL);

    return ret;
}

/**
 * @brief Calculate iotop mode and scan mode from scan sequence.
 *
 * This function calculates the iotop mode and scan mode based on the provided
 * scan sequence of RAT settings.
 *
 * @param[in]  rat_seq    Pointer to an array of RAT types.
 * @param[out] iotop_mode Pointer to the variable where the iotop mode will be
 * stored.
 * @param[out] scan_mode  Pointer to the variable where the scan mode will be
 * stored.
 * @Note example: if the input is rat_seq[3] = {CELL_RAT_NBIOT, CELL_RAT_CATM1,
 * CELL_RAT_INVALID} then the output are iotop_mode = 2 (eMTC and NB-IoT)
 * scan_mode = 3 (LTE only)
 */
static void
calc_iotop_mode_scan_mode_from_scan_seq (cell_rat_type_t * rat_seq,
                                         uint8_t *         iotop_mode,
                                         uint8_t *         scan_mode)
{
    uint8_t scan_seq_mask = 0;

    for (uint8_t i = 0; i < CELL_RAT_MAX; i++)
    {
        scan_seq_mask |= scanseq_set_map[rat_seq[i]].bit;
    }

    for (uint8_t i = 0;
         i < sizeof(scanmode_iotopmode_map) / sizeof(scanmode_iotopmode_map[0]);
         i++)
    {
        if (scan_seq_mask == scanmode_iotopmode_map[i].scan_seq_mask)
        {
            *scan_mode  = scanmode_iotopmode_map[i].scan_mode;
            *iotop_mode = scanmode_iotopmode_map[i].iotop_mode;
        }
    }
}

static at_req_status_t
update_scan_seq_from_iotop_mode (cell_rat_type_t * rat_seq, uint8_t iotop_mode)
{
    at_req_status_t ret = AT_SUCCESS;
    if (IOTOP_MODE_CATM1 == iotop_mode)
    {
        for (uint8_t i = 0; i < CELL_RAT_MAX; i++)
        {
            if (rat_seq[i] == CELL_RAT_NBIOT)
            {
                rat_seq[i] = CELL_RAT_INVALID;
            }
        }
    }
    else if (IOTOP_MODE_NBIOT == iotop_mode)
    {
        for (uint8_t i = 0; i < CELL_RAT_MAX; i++)
        {
            if (rat_seq[i] == CELL_RAT_CATM1)
            {
                rat_seq[i] = CELL_RAT_INVALID;
            }
        }
    }
    else if (IOTOP_MODE_CATM1_NBIOT == iotop_mode)
    {
        // Do nothing, both eMTC and NB-IoT are supported
    }
    else
    {
        cell_log_debug("Unsupported iotop_mode %d", iotop_mode);
        ret = AT_TIMEOUT;
    }

    return ret;
}

static void
parse_and_update_rat_seq (cell_rat_type_t * rat_seq, const char * seq_str)
{
    for (uint8_t i = 0; i < CELL_RAT_MAX; i++)
    {
        rat_seq[i] = CELL_RAT_INVALID;
    }

    uint8_t idx = 0;
    size_t  len = strlen(seq_str);
    for (size_t pos = 0; pos + 1 < len && idx < CELL_RAT_MAX; pos += 2)
    {

        char token[3] = {seq_str[pos], seq_str[pos + 1], '\0'};

        if (strcmp(token, "01") == 0)
        {
            rat_seq[idx++] = CELL_RAT_GSM;
        }
        else if (strcmp(token, "02") == 0)
        {
            rat_seq[idx++] = CELL_RAT_CATM1;
        }
        else if (strcmp(token, "03") == 0)
        {
            rat_seq[idx++] = CELL_RAT_NBIOT;
        }
    }
}

static at_req_status_t
at_parser_nb1_cap_feature_bitmap_get (at_request_t * at_req, char * line,
                                      uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    char *          data   = (char *) at_req->p_data;

    int             parsed = sscanf(line, "+QNVFR: %31s", data);
    if (parsed == 1)
    {
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

static at_req_status_t
at_parser_storage_space_get (at_request_t * at_req, char * line,
                             uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret         = AT_SUCCESS;

    uint32_t *      p_free_size = (uint32_t *) at_req->p_data;
    uint32_t        total_size  = 0;

    int parsed = sscanf(line, "+QFLDS: %u,%u", p_free_size, &total_size);
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

    return ret;
}

static at_req_status_t
at_parser_file_open (at_request_t * at_req, char * line, uint64_t start_time,
                     uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    uint32_t *      p_file = (uint32_t *) at_req->p_data;
    int             parsed = sscanf(line, "+QFOPEN: %u", p_file);
    if (parsed == 1)
    {
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

static at_req_status_t
at_parser_dfota_write (at_request_t * at_req, char * line, uint64_t start_time,
                       uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    int             res    = 0;
    int             parsed = sscanf(line, "+QFWRITE: %d", &res);
    if (parsed == 1)
    {
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

static at_req_status_t
at_parser_scrub_direction_get (at_request_t * at_req, char * line,
                               uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t          ret   = AT_SUCCESS;
    bg95_scrub_direction_t * p_dir = (bg95_scrub_direction_t *) at_req->p_data;
    int                      value = 0;

    int parsed = sscanf(line, "+QSCRUBCFG: \"scrubdirection\",%d", &value);
    if (parsed == 1)
    {
        *p_dir = (bg95_scrub_direction_t) value;
        ret    = seek_one_line_and_parse_final_resp(
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
bg95_urc_dfota_handler (void * data)
{
    if (NULL != data)
    {
        dfota_urc_type_t   urc_type = DFOTA_URC_TYPE_INVALID;
        urc_dfota_type_t * p_data   = (urc_dfota_type_t *) data;
        switch (p_data->event)
        {
            case URC_DFOTA_QIND_HTTP_DOWNLOADING:
            case URC_DFOTA_QIND_COAP_DOWNLOAD_START:
            {
                urc_type = DFOTA_URC_TYPE_DOWNLOADING;
            }
            break;

            case URC_DFOTA_QIND_HTTP_END:
            case URC_DFOTA_QIND_COAP_END:
            case URC_DFOTA_QIND_FTP_END:
            {
                urc_type = (URC_DFOTA_ERR_DOWNLOAD_SUCCESSFUL
                            == p_data->params.error_code)
                               ? DFOTA_URC_TYPE_DOWNLOAD_SUCCESSFUL
                               : DFOTA_URC_TYPE_DOWNLOAD_FAILED;
            }
            break;

            case URC_DFOTA_QIND_UPDATING_PROGRESS:
            {
                urc_type = DFOTA_URC_TYPE_UPDATING;
            }
            break;

            case URC_DFOTA_QIND_UPDATING_FINISHED:
            {
                urc_type = (URC_DFOTA_ERR_UPDATE_SUCCESSFUL
                            == p_data->params.error_code)
                               ? DFOTA_URC_TYPE_SUCCESSFUL
                               : DFOTA_URC_TYPE_FAILED;
            }
            break;

            default:
                break;
        }
        if (DFOTA_URC_TYPE_INVALID != urc_type)
        {
            dfota_notify_urc(urc_type);
        }
    }
}
