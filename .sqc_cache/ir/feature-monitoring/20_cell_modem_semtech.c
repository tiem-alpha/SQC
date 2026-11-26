#include <sqc_builtin.h>
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_modem_semtech.c"
/**
 * @file cell_modem_semtech.c
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
 * @brief Modem driver implementation for Sierra/Semtech cellular module
 * @date 2026-04-02
 */

/*******************************************************
 * Same Project Header Files                          *
 *******************************************************/

#include "cell_modem_semtech.h"
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
#include "cell_modem_helper.h"

/*******************************************************
 * Third-party Header Files                            *
 *******************************************************/

#ifdef __TRUSTINSOFT_ANALYZER__
#include <tis_builtin.h>
#endif

/*******************************************************
 * Data types, Constants & Marcos                      *
 *******************************************************/

#define MAX_HL_HEX_LEN                      32
#define MAX_HL_URC                          60
#define LTE_SEMTECH_BAND_MASK_STR_MAX       (2 + MAX_HL_HEX_LEN + 1)
#define SEMTECH_MAXIMUM_SUPPORT_SOCKET      6
#define EOF_PATTERN                         "--EOF--Pattern--"
#define DATA_TX_BUF_SIZE                    512
#define SEMTECH_SOCKET_SESSION_STATUS_READY 1

typedef struct
{
    uint8_t         modem_rat;
    cell_rat_type_t cell_rat;
} semtech_rat_map_t;

static const semtech_rat_map_t semtech_rat_map[] = {
    {0, CELL_RAT_CATM1},
    {1, CELL_RAT_NBIOT}
};

typedef enum
{
    SOCKET_DISCONNECTED = 0,
    SOCKET_CONNECTED
} socket_status_t;

typedef enum
{
    SOCKET_CLIENT = 0,
    SOCKET_SERVER,
    SOCKET_CHILD_CREATED_BY_SERVER
} socket_mode_t;

typedef struct
{
    uint32_t        socket_id;
    bool            found;
    socket_status_t status;
    socket_mode_t   mode;
} socket_info_t;

typedef struct
{
    cell_service_domain_t serv_domain;
    uint8_t               cemode;
} service_domain_map_t;

static const service_domain_map_t sd_map[] = {
    {CELL_SERVICE_PS,    0},
    {CELL_SERVICE_CS_PS, 1}
};

static const size_t sd_map_size = sizeof(sd_map) / sizeof(sd_map[0]);

typedef struct
{
    cell_pdp_context_type_t type;
    const char *            str;
} pdp_type_map_t;

static const pdp_type_map_t pdp_type_map[] = {
    {CELL_PDP_CONTEXT_IPV4,   "IP"    },
    {CELL_PDP_CONTEXT_IPV6,   "IPV6"  },
    {CELL_PDP_CONTEXT_IPV4V6, "IPV4V6"}
};

static const size_t pdp_type_map_size
    = sizeof(pdp_type_map) / sizeof(pdp_type_map[0]);

// Map band to index values
static uint8_t lte_semtech_band_hex_map[CELL_LTE_BAND_MAX] = {
    [CELL_LTE_BAND_1]  = 0,
    [CELL_LTE_BAND_2]  = 1,
    [CELL_LTE_BAND_3]  = 2,
    [CELL_LTE_BAND_4]  = 3,
    [CELL_LTE_BAND_5]  = 4,

    [CELL_LTE_BAND_7]  = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_8]  = 7,
    [CELL_LTE_BAND_9]  = 8,
    [CELL_LTE_BAND_10] = 9,
    [CELL_LTE_BAND_12] = 11,
    [CELL_LTE_BAND_13] = 12,
    [CELL_LTE_BAND_17] = 16,
    [CELL_LTE_BAND_18] = 17,
    [CELL_LTE_BAND_19] = 18,
    [CELL_LTE_BAND_20] = 19,
    [CELL_LTE_BAND_23] = 22,
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

    [CELL_LTE_BAND_65] = 64,
    [CELL_LTE_BAND_66] = 65,
    [CELL_LTE_BAND_70] = 69,

    [CELL_LTE_BAND_71] = CELL_LTE_BAND_INVALID,

    [CELL_LTE_BAND_72] = 71,
    [CELL_LTE_BAND_73] = 72,
    [CELL_LTE_BAND_85] = 84,
};

// Band mapping based on bit index (max 128-bit value)
// the logical representation of 1«(BandNumber-1)
static const band_mapping_t semtech_catm_band_map[] = {
    {0,  CELL_LTE_BAND_1 },
    {1,  CELL_LTE_BAND_2 },
    {2,  CELL_LTE_BAND_3 },
    {3,  CELL_LTE_BAND_4 },
    {4,  CELL_LTE_BAND_5 },
    {7,  CELL_LTE_BAND_8 },
    {8,  CELL_LTE_BAND_9 },
    {9,  CELL_LTE_BAND_10},
    {11, CELL_LTE_BAND_12},
    {12, CELL_LTE_BAND_13},
    {16, CELL_LTE_BAND_17},
    {17, CELL_LTE_BAND_18},
    {18, CELL_LTE_BAND_19},
    {19, CELL_LTE_BAND_20},
    {22, CELL_LTE_BAND_23},
    {24, CELL_LTE_BAND_25},
    {25, CELL_LTE_BAND_26},
    {26, CELL_LTE_BAND_27},
    {27, CELL_LTE_BAND_28},
    {30, CELL_LTE_BAND_31},
    {64, CELL_LTE_BAND_65},
    {65, CELL_LTE_BAND_66},
    {69, CELL_LTE_BAND_70},
    {71, CELL_LTE_BAND_72},
    {72, CELL_LTE_BAND_73},
    {84, CELL_LTE_BAND_85},
};
static const size_t semtech_catm_band_map_size
    = sizeof(semtech_catm_band_map) / sizeof(semtech_catm_band_map[0]);

static const band_mapping_t semtech_nbiot_band_map[] = {
    {0,  CELL_LTE_BAND_1 },
    {1,  CELL_LTE_BAND_2 },
    {2,  CELL_LTE_BAND_3 },
    {3,  CELL_LTE_BAND_4 },
    {4,  CELL_LTE_BAND_5 },
    {7,  CELL_LTE_BAND_8 },
    {8,  CELL_LTE_BAND_9 },
    {9,  CELL_LTE_BAND_10},
    {11, CELL_LTE_BAND_12},
    {12, CELL_LTE_BAND_13},
    {16, CELL_LTE_BAND_17},
    {17, CELL_LTE_BAND_18},
    {18, CELL_LTE_BAND_19},
    {19, CELL_LTE_BAND_20},
    {22, CELL_LTE_BAND_23},
    {24, CELL_LTE_BAND_25},
    {25, CELL_LTE_BAND_26},
    {26, CELL_LTE_BAND_27},
    {27, CELL_LTE_BAND_28},
    {30, CELL_LTE_BAND_31},
    {64, CELL_LTE_BAND_65},
    {65, CELL_LTE_BAND_66},
    {69, CELL_LTE_BAND_70},
    {71, CELL_LTE_BAND_72},
    {72, CELL_LTE_BAND_73},
    {84, CELL_LTE_BAND_85},
};
static const size_t semtech_nbiot_band_map_size
    = sizeof(semtech_nbiot_band_map) / sizeof(semtech_nbiot_band_map[0]);

typedef enum
{
    SEMTECH_HL_CME_ERR_PHONE_FAILURE                                 = 0,
    SEMTECH_HL_CME_ERR_NO_CONNECTION_TO_PHONE                        = 1,
    SEMTECH_HL_CME_ERR_PHONE_ADAPTOR_LINK_RESERVED                   = 2,
    SEMTECH_HL_CME_ERR_OPERATION_NOT_ALLOWED                         = 3,
    SEMTECH_HL_CME_ERR_OPERATION_NOT_SUPPORTED                       = 4,
    SEMTECH_HL_CME_ERR_PH_SIM_PIN_REQUIRED                           = 5,
    SEMTECH_HL_CME_ERR_PH_FSIM_PIN_REQUIRED                          = 6,
    SEMTECH_HL_CME_ERR_PH_FSIM_PUK_REQUIRED                          = 7,
    SEMTECH_HL_CME_ERR_USIM_NOT_INSERTED                             = 10,
    SEMTECH_HL_CME_ERR_USIM_PIN_REQUIRED                             = 11,
    SEMTECH_HL_CME_ERR_USIM_PUK_REQUIRED                             = 12,
    SEMTECH_HL_CME_ERR_USIM_FAILURE                                  = 13,
    SEMTECH_HL_CME_ERR_USIM_BUSY                                     = 14,
    SEMTECH_HL_CME_ERR_USIM_WRONG                                    = 15,
    SEMTECH_HL_CME_ERR_INCORRECT_PASSWORD                            = 16,
    SEMTECH_HL_CME_ERR_USIM_PIN2_REQUIRED                            = 17,
    SEMTECH_HL_CME_ERR_USIM_PUK2_REQUIRED                            = 18,
    SEMTECH_HL_CME_ERR_MEMORY_FULL                                   = 20,
    SEMTECH_HL_CME_ERR_INVALID_INDEX                                 = 21,
    SEMTECH_HL_CME_ERR_NOT_FOUND                                     = 22,
    SEMTECH_HL_CME_ERR_MEMORY_FAILURE                                = 23,
    SEMTECH_HL_CME_ERR_TEXT_STRING_TOO_LONG                          = 24,
    SEMTECH_HL_CME_ERR_INVALID_CHARACTERS_IN_TEXT_STRING             = 25,
    SEMTECH_HL_CME_ERR_DIAL_STRING_TOO_LONG                          = 26,
    SEMTECH_HL_CME_ERR_INVALID_CHARACTERS_IN_DIAL_STRING             = 27,
    SEMTECH_HL_CME_ERR_NO_NETWORK_SERVICE                            = 30,
    SEMTECH_HL_CME_ERR_NETWORK_TIMEOUT                               = 31,
    SEMTECH_HL_CME_ERR_NETWORK_NOT_ALLOWED_EMERGENCY_CALLS_ONLY      = 32,
    SEMTECH_HL_CME_ERR_NETWORK_PERSONALIZATION_PIN_REQUIRED          = 40,
    SEMTECH_HL_CME_ERR_NETWORK_PERSONALIZATION_PUK_REQUIRED          = 41,
    SEMTECH_HL_CME_ERR_NETWORK_SUBSET_PERSONALIZATION_PIN_REQUIRED   = 42,
    SEMTECH_HL_CME_ERR_NETWORK_SUBSET_PERSONALIZATION_PUK_REQUIRED   = 43,
    SEMTECH_HL_CME_ERR_SERVICE_PROVIDER_PERSONALIZATION_PIN_REQUIRED = 44,
    SEMTECH_HL_CME_ERR_SERVICE_PROVIDER_PERSONALIZATION_PUK_REQUIRED = 45,
    SEMTECH_HL_CME_ERR_CORPORATE_PERSONALIZATION_PIN_REQUIRED        = 46,
    SEMTECH_HL_CME_ERR_CORPORATE_PERSONALIZATION_PUK_REQUIRED        = 47,
} semtech_hl_cme_err_t;

typedef enum
{
    SEMTECH_HL_CMS_ERR_ME_FAILURE              = 300,
    SEMTECH_HL_CMS_ERR_SMS_ME_RESERVED         = 301,
    SEMTECH_HL_CMS_ERR_OPERATION_NOT_ALLOWED   = 302,
    SEMTECH_HL_CMS_ERR_OPERATION_NOT_SUPPORTED = 303,
    SEMTECH_HL_CMS_ERR_INVALID_PDU_MODE        = 304,
    SEMTECH_HL_CMS_ERR_INVALID_TEXT_MODE       = 305,
    SEMTECH_HL_CMS_ERR_USIM_NOT_INSERTED       = 310,
    SEMTECH_HL_CMS_ERR_USIM_PIN_NECESSARY      = 311,
    SEMTECH_HL_CMS_ERR_PH_USIM_PIN_NECESSARY   = 312,
    SEMTECH_HL_CMS_ERR_USIM_FAILURE            = 313,
    SEMTECH_HL_CMS_ERR_USIM_BUSY               = 314,
    SEMTECH_HL_CMS_ERR_USIM_WRONG              = 315,
    SEMTECH_HL_CMS_ERR_USIM_PUK_REQUIRED       = 316,
    SEMTECH_HL_CMS_ERR_USIM_PIN2_REQUIRED      = 317,
    SEMTECH_HL_CMS_ERR_USIM_PUK2_REQUIRED      = 318,
    SEMTECH_HL_CMS_ERR_MEMORY_FAILURE          = 320,
    SEMTECH_HL_CMS_ERR_INVALID_MEMORY_INDEX    = 321,
    SEMTECH_HL_CMS_ERR_MEMORY_FULL             = 322,
    SEMTECH_HL_CMS_ERR_SMSC_ADDRESS_UNKNOWN    = 330,
    SEMTECH_HL_CMS_ERR_NO_NETWORK              = 331,
    SEMTECH_HL_CMS_ERR_NETWORK_TIMEOUT         = 332,
    SEMTECH_HL_CMS_ERR_UNKNOWN                 = 500,
} semtech_hl_cms_err_t;

/*******************************************************
 * Private Function Prototypes                         *
 *******************************************************/

static at_req_status_t at_parser_semtech_iccid_get(at_request_t * at_req,
                                                   char *         line,
                                                   uint64_t       start_time,
                                                   uint32_t       timeout_ms);

static at_req_status_t at_parser_semtech_firmware_version(at_request_t * at_req,
                                                          char *         line,
                                                          uint64_t start_time,
                                                          uint32_t timeout_ms);

static at_req_status_t at_parser_semtech_service_domain_get(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t at_parser_semtech_operator_info_get(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t at_parser_rat_get(at_request_t * at_req, char * line,
                                         uint64_t start_time,
                                         uint32_t timeout_ms);

static cell_rat_type_t semtech_band_mapping_rat(uint8_t modem_rat);

static at_req_status_t at_parser_semtech_band_get(at_request_t * at_req,
                                                  char *         line,
                                                  uint64_t       start_time,
                                                  uint32_t       timeout_ms);

static at_req_status_t at_parser_semtech_pdp_settings_get(at_request_t * at_req,
                                                          char *         line,
                                                          uint64_t start_time,
                                                          uint32_t timeout_ms);
static at_req_status_t at_parser_semtech_pdp_authentication_get(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);
static at_req_status_t semtech_pdp_context_info_get(
    cell_network_info_t * p_net_info, uint16_t * error_code);

static at_req_status_t semtech_pdp_address_get(cell_network_info_t * p_net_info,
                                               uint16_t * error_code);

static at_req_status_t at_parser_semtech_pdp_stat_get(at_request_t * at_req,
                                                      char *         line,
                                                      uint64_t       start_time,
                                                      uint32_t timeout_ms);

static bool         service_domain_mapping_cemode(cell_service_domain_t domain,
                                                  uint8_t *             cemode);
static const char * pdp_type_to_str(cell_pdp_context_type_t type);

static at_req_status_t at_parser_semtech_pdp_context_info_get(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);

static at_req_status_t rat_set(cell_rat_settings_t * rat,
                               uint16_t *            error_code);
static at_req_status_t semtech_band_set(cell_rat_settings_t * rat_settings,
                                        uint16_t *            error_code);
static at_req_status_t define_pdp_context(cell_pdp_settings_t * pdp_settings,
                                          uint16_t *            error_code);
static at_req_status_t semtech_pdp_authentication_set(
    cell_pdp_settings_t * pdp_settings, uint16_t * error_code);

static at_req_status_t semtech_enable_sim_functionality(uint16_t * error_code);

static at_req_status_t at_parser_socket_check_exist(at_request_t * at_req,
                                                    char *         line,
                                                    uint64_t       start_time,
                                                    uint32_t       timeout_ms);
static at_req_status_t semtech_socket_recv_unread_len_get(
    uint32_t socket_id, size_t * unread_recv_len, uint16_t * error_code);
static at_req_status_t at_parser_socket_get_unread_recv_len(
    at_request_t * at_req, char * line, uint64_t start_time,
    uint32_t timeout_ms);
static at_req_status_t at_parser_socket_receive(at_request_t * at_req,
                                                char *         line,
                                                uint64_t       start_time,
                                                uint32_t       timeout_ms);
static void build_socket_receive_command(cell_socket_t * socket, char * cmd_buf,
                                         uint16_t cmd_len, uint16_t recv_len);
static at_req_status_t send_socket_data_wait_connect(at_request_t * at_req,
                                                     uint32_t       timeout);

static at_req_status_t at_parser_socket_id(at_request_t * at_req, char * line,
                                           uint64_t start_time,
                                           uint32_t timeout_ms);
static at_req_status_t get_tcp_socket_status(uint32_t        socket_id,
                                             socket_info_t * socket_info,
                                             uint16_t *      error_code);
static at_req_status_t at_parser_get_tcp_socket_status(at_request_t * at_req,
                                                       char *         line,
                                                       uint64_t start_time,
                                                       uint32_t timeout_ms);
static at_req_status_t semtech_gprs_connection_set(cell_pdp_settings_t * pdp,
                                                   uint16_t * error_code);
static at_req_status_t semtech_tcp_config_socket(
    const cell_socket_settings_t * socket, uint32_t * socket_id,
    uint16_t * error_code);
static at_req_status_t tcp_connect(uint32_t socket_id, uint16_t * error_code);
static at_req_status_t tcp_close(uint32_t socket_id, uint16_t * error_code);
static at_req_status_t tcp_delete(uint32_t socket_id, uint16_t * error_code);
static void build_send_command(cell_socket_t * socket, char * cmd_buf,
                               uint16_t cmd_len, size_t send_lend);
static at_req_status_t semtech_udp_config_socket(
    const cell_socket_settings_t * socket, uint32_t * socket_id,
    uint16_t * error_code);
static at_req_status_t udp_close(uint32_t socket_id, uint16_t * error_code);
static at_req_status_t udp_receive_remote_info_get(
    char * line, socket_data_recv_info_t * udp_remote, uint64_t start_time,
    uint32_t timeout_ms);
static at_req_status_t socket_receive_until_eof(
    socket_data_recv_info_t * recv_info, uint64_t start_time,
    uint32_t timeout_ms);
static bool is_socket_service_tcp(cell_socket_service_type_t service_type);

urc_map_handler_t semtech_urc_handler[URC_TYPE_NUMBER] = {
    [URC_TYPE_RDY]                 = {.handler = NULL},
    [URC_TYPE_APP_RDY]             = {.handler = NULL},
    [URC_TYPE_POWER_DOWN]          = {.handler = NULL},
    [URC_TYPE_NORMAL_POWER_DOWN]   = {.handler = NULL},
    [URC_TYPE_PSM_POWER_DOWN]      = {.handler = NULL},
    [URC_TYPE_RING]                = {.handler = NULL},
    [URC_TYPE_CFUN_1]              = {.handler = NULL},
    [URC_TYPE_QUSIM]               = {.handler = NULL},
    [URC_TYPE_CPIN]                = {.handler = NULL},
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
    [URC_TYPE_CEREG]               = {.handler = semtech_handle_urc_cereg},
    [URC_TYPE_QIOPEN]              = {.handler = NULL},
    [URC_TYPE_QIURC_CLOSED]        = {.handler = NULL},
    [URC_TYPE_QIURC_RECV]          = {.handler = NULL},
    [URC_TYPE_QIURC_INCOMING_FULL] = {.handler = NULL},
    [URC_TYPE_QIURC_INCOMING]      = {.handler = NULL},
    [URC_TYPE_QIURC_PDPDEACT]      = {.handler = NULL},
    [URC_TYPE_QIURC_DNSGIP]        = {.handler = NULL},
    [URC_TYPE_QNTP]                = {.handler = NULL},
    [URC_TYPE_QPING]               = {.handler = NULL},
    [URC_TYPE_QSSLOPEN]            = {.handler = NULL},
    [URC_TYPE_QSSLURC_RECV]        = {.handler = NULL},
    [URC_TYPE_QSSLURC_CLOSED]      = {.handler = NULL},
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
    [URC_TYPE_KTCP_DATA]   = {.handler = semtech_handle_urc_ktcp_data},
    [URC_TYPE_KCNX_IND]    = {.handler = NULL},
    [URC_TYPE_KTCP_IND]    = {.handler = semtech_handle_urc_ktcp_ind},
    [URC_TYPE_KTCP_NOTIF]  = {.handler = semtech_handle_urc_ktcp_notif},
    [URC_TYPE_KTCP_SRVREQ] = {.handler = semtech_handle_urc_ktcp_srvreq},
    [URC_TYPE_KUDP_DATA]   = {.handler = semtech_handle_urc_kudp_data},
    [URC_TYPE_KUDP_NOTIF]  = {.handler = semtech_handle_urc_kudp_notif},
    [URC_TYPE_KUDP_IND]    = {.handler = semtech_handle_urc_kudp_ind},
};

/*******************************************************
 * Public Function Bodies                              *
 *******************************************************/

at_req_status_t
semtech_module_poweroff (uint16_t * error_code)
{
    at_req_status_t at_status       = AT_PENDING;
    at_request_t    at_req_poweroff = {.p_send_buff  = "AT+CPWROFF\r",
                                       .resp_prefix  = NULL,
                                       .p_data       = NULL,
                                       .data_len     = 0,
                                       .p_error_code = error_code};
    at_req_poweroff.send_buff_len   = strlen(at_req_poweroff.p_send_buff);
    // Send AT command
    at_status = at_command_request(
        &at_req_poweroff, AT_COMMAND_COMMON_TIMEOUT_MS, NULL);

    return at_status;
}

at_req_status_t
semtech_iccid_get (char * p_iccid, uint16_t * error_code)
{
    at_req_status_t ret              = AT_PENDING;
    at_request_t    at_req_get_iccid = {.p_send_buff  = "AT+CCID\r",
                                        .resp_prefix  = "+CCID",
                                        .p_data       = p_iccid,
                                        .data_len     = CELL_ICCID_MAX_SIZE + 1,
                                        .p_error_code = error_code};

    at_req_get_iccid.send_buff_len   = strlen(at_req_get_iccid.p_send_buff);
    ret                              = at_command_request(
        &at_req_get_iccid, 300, at_parser_semtech_iccid_get);

    return ret;
}

at_req_status_t
semtech_firmware_version_get (char * p_fw_ver, uint16_t * error_code)
{
    at_req_status_t ret           = AT_PENDING;
    at_request_t    at_req_fw_ver = {.p_send_buff = "AT+CGMR\r",
                                     .resp_prefix = NULL,
                                     .p_data      = p_fw_ver,
                                     .data_len = CELL_FW_VERSION_MAX_SIZE + 1,
                                     .p_error_code = error_code};

    at_req_fw_ver.send_buff_len   = strlen(at_req_fw_ver.p_send_buff);
    ret                           = at_command_request(
        &at_req_fw_ver, 300, at_parser_semtech_firmware_version);

    return ret;
}

at_req_status_t
semtech_modem_specific_features_set (uint16_t * error_code)
{
    at_req_status_t ret                 = AT_PENDING;
    at_request_t    at_req_save_current = {.p_send_buff  = "AT&W\r",
                                           .resp_prefix  = NULL,
                                           .p_data       = NULL,
                                           .data_len     = 0,
                                           .p_error_code = error_code};
    at_req_save_current.send_buff_len = strlen(at_req_save_current.p_send_buff);
    ret = at_command_request(&at_req_save_current, 300, NULL);

    return ret;
}

at_req_status_t
semtech_rat_settings_get (cell_rat_settings_t * rat_settings,
                          uint16_t *            error_code)
{
    at_req_status_t ret    = AT_PENDING;
    at_request_t    at_req = {.p_send_buff = NULL,
                              .resp_prefix = NULL,
                              .p_data      = rat_settings,
                              .data_len = (uint16_t) sizeof(cell_rat_settings_t),
                              .p_error_code = error_code};

    at_req.p_send_buff     = "AT+KSRAT?\r";
    at_req.resp_prefix     = "+KSRAT";
    at_req.send_buff_len   = strlen(at_req.p_send_buff);
    ret = at_command_request(&at_req, 300, at_parser_rat_get);

    if (AT_SUCCESS == ret)
    {
        at_req.p_send_buff   = "AT+KBNDCFG?\r";
        at_req.resp_prefix   = "+KBNDCFG";
        at_req.send_buff_len = strlen(at_req.p_send_buff);
        ret = at_command_request(&at_req, 300, at_parser_semtech_band_get);
    }
    return ret;
}

at_req_status_t
semtech_rat_settings_set (cell_rat_settings_t * rat_settings,
                          bool * is_trigger_reboot, uint16_t * error_code)
{
    at_req_status_t ret = AT_PENDING;

    ret                 = semtech_band_set(rat_settings, error_code);
    if (AT_SUCCESS == ret)
    {
        ret = rat_set(rat_settings, error_code);
    }
    if (AT_SUCCESS == ret)
    {
        *is_trigger_reboot = true;
    }

    return ret;
}

at_req_status_t
semtech_operator_info_get (cell_operator_info_t * info, uint16_t * error_code)
{
    at_req_status_t ret             = AT_PENDING;
    at_request_t    at_req_settings = {.p_send_buff = "AT+COPS?\r",
                                       .resp_prefix = "+COPS",
                                       .p_data      = info,
                                       .data_len = sizeof(cell_operator_info_t),
                                       .p_error_code = error_code};

    at_req_settings.send_buff_len   = strlen(at_req_settings.p_send_buff);
    ret                             = at_command_request(
        &at_req_settings, 180000, at_parser_semtech_operator_info_get);

    return ret;
}

at_req_status_t
semtech_operator_settings_set (cell_operator_settings_t * op_settings,
                               uint16_t *                 error_code)
{
    at_req_status_t ret                    = AT_PENDING;
    char            cmd_buf[100]           = {'\0'};
    at_request_t    at_req_set_op_settings = {.p_send_buff  = cmd_buf,
                                              .resp_prefix  = NULL,
                                              .p_data       = NULL,
                                              .data_len     = 0,
                                              .p_error_code = error_code};
    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+COPS=%d,%d,%s\r",
                    (int) op_settings->selection_mode,
                    (int) op_settings->format_type,
                    op_settings->name);
    at_req_set_op_settings.send_buff_len
        = strlen(at_req_set_op_settings.p_send_buff);

    ret = at_command_request(&at_req_set_op_settings, 180000, NULL);
    return ret;
}

at_req_status_t
semtech_service_domain_get (cell_service_domain_t * service_domain,
                            uint16_t *              error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_service_domain
        = {.p_send_buff  = "AT+CEMODE?\r",
           .resp_prefix  = "+CEMODE",
           .p_data       = service_domain,
           .data_len     = (uint16_t) sizeof(cell_service_domain_t),
           .p_error_code = error_code};

    at_req_get_service_domain.send_buff_len
        = strlen(at_req_get_service_domain.p_send_buff);
    ret = at_command_request(
        &at_req_get_service_domain, 300, at_parser_semtech_service_domain_get);

    return ret;
}

at_req_status_t
semtech_service_domain_set (cell_service_domain_t * service_domain,
                            uint16_t *              error_code)
{
    at_req_status_t ret                       = AT_PENDING;
    char            cmd_buf[32]               = {'\0'};
    at_request_t    at_req_set_service_domain = {.p_send_buff  = cmd_buf,
                                                 .resp_prefix  = NULL,
                                                 .p_data       = NULL,
                                                 .data_len     = 0,
                                                 .p_error_code = error_code};

    uint8_t         cemode                    = 0;
    if ((service_domain != NULL)
        && (service_domain_mapping_cemode(*service_domain, &cemode) == true))
    {
        (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+CEMODE=%d\r", cemode);
        at_req_set_service_domain.send_buff_len
            = strlen(at_req_set_service_domain.p_send_buff);
        ret = at_command_request(&at_req_set_service_domain, 300, NULL);
    }

    return ret;
}

at_req_status_t
semtech_pdp_status_get (cell_network_info_t * p_net_info, uint16_t * error_code)
{
    at_req_status_t at_status = AT_PENDING;

    // Get PDP context activation state
    at_status = semtech_pdp_context_info_get(p_net_info, error_code);
    if ((at_status == AT_SUCCESS)
        && (p_net_info->state == CELL_PDP_CONTEXT_STATE_ACTIVATED))
    {
        // Get IP address if PDP context is active
        at_status = semtech_pdp_address_get(p_net_info, error_code);
    }
    return at_status;
}

at_req_status_t
semtech_pdp_activate (uint8_t ctx_id, uint16_t * error_code)
{

    at_req_status_t at_status      = AT_PENDING;
    char            cmd_buf[16]    = {'\0'};

    at_request_t    at_req_act_pdp = {.p_send_buff  = cmd_buf,
                                      .resp_prefix  = NULL,
                                      .p_data       = NULL,
                                      .data_len     = 0,
                                      .p_error_code = error_code};
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+CGACT=1,%d\r", ctx_id);
    at_req_act_pdp.send_buff_len = strlen(at_req_act_pdp.p_send_buff);
    at_status = at_command_request(&at_req_act_pdp, 60000, NULL);

    return at_status;
}

at_req_status_t
semtech_pdp_deactivate (uint8_t ctx_id, uint16_t * error_code)
{
    at_req_status_t at_status        = AT_PENDING;
    char            cmd_buf[16]      = {'\0'};
    at_request_t    at_req_deact_pdp = {.p_send_buff  = cmd_buf,
                                        .resp_prefix  = NULL,
                                        .p_data       = NULL,
                                        .data_len     = 0,
                                        .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+CGACT=0,%d\r", ctx_id);
    at_req_deact_pdp.send_buff_len = strlen(at_req_deact_pdp.p_send_buff);
    at_status = at_command_request(&at_req_deact_pdp, 60000, NULL);

    return at_status;
}

at_req_status_t
semtech_pdp_settings_get (cell_pdp_settings_t * pdp_settings,
                          uint16_t *            error_code)
{
    at_req_status_t ret = AT_PENDING;
    at_request_t    at_req_get_pdp_settings
        = {.p_send_buff  = "AT+CGDCONT?\r",
           .resp_prefix  = "+CGDCONT",
           .p_data       = pdp_settings,
           .data_len     = (uint16_t) sizeof(cell_pdp_settings_t),
           .p_error_code = error_code};

    at_req_get_pdp_settings.send_buff_len
        = strlen(at_req_get_pdp_settings.p_send_buff);
    ret = at_command_request(
        &at_req_get_pdp_settings, 5000, at_parser_semtech_pdp_settings_get);

    if (ret == AT_SUCCESS)
    {
        at_req_get_pdp_settings.p_send_buff = "AT+CGAUTH?\r";
        at_req_get_pdp_settings.resp_prefix = "+CGAUTH";
        at_req_get_pdp_settings.send_buff_len
            = strlen(at_req_get_pdp_settings.p_send_buff);
        ret = at_command_request(&at_req_get_pdp_settings,
                                 5000,
                                 at_parser_semtech_pdp_authentication_get);
    }

    return ret;
}

at_req_status_t
semtech_pdp_settings_set (cell_pdp_settings_t * pdp_settings,
                          uint16_t *            error_code)
{
    at_req_status_t ret = AT_PENDING;
    ret                 = define_pdp_context(pdp_settings, error_code);

    if (AT_SUCCESS == ret)
    {
        ret = semtech_pdp_authentication_set(pdp_settings, error_code);
    }

    return ret;
}

at_req_status_t
semtech_sim_enter_pin (cell_simcard_settings_t sim_settings,
                       bool * is_sim_ready_requires_urc, uint16_t * error_code)
{
    at_req_status_t ret = AT_PENDING;
    ret                 = common_pin_enter(sim_settings, error_code);
    if (AT_SUCCESS == ret)
    {
        *is_sim_ready_requires_urc = false;
    }
    return ret;
}

at_req_status_t
semtech_sim_readiness_info_get (cell_sim_lock_status_t * status,
                                uint16_t *               error_code)
{
    at_req_status_t ret = AT_PENDING;
    ret                 = semtech_enable_sim_functionality(error_code);
    if (AT_SUCCESS == ret || AT_ERROR_GENERIC == ret)
    {
        ret = common_sim_readiness_check(status, error_code);
    }
    return ret;
}

at_req_status_t
semtech_socket_create (const cell_socket_settings_t * socket,
                       uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t     ret = AT_PENDING;
    cell_pdp_settings_t pdp = {0};

    ret                     = semtech_pdp_settings_get(&pdp, error_code);
    if (ret == AT_SUCCESS)
    {
        ret = semtech_gprs_connection_set(&pdp, error_code);
    }
    if (ret == AT_SUCCESS)
    {
        switch (socket->service_type)
        {
            case CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT:
            case CELL_SOCKET_SERVICE_TYPE_TCP_SERVER:
            {
                ret = semtech_tcp_config_socket(socket, &socket_id, error_code);

                if (ret == AT_SUCCESS)
                {
                    ret = tcp_connect(socket_id, error_code);
                }
            }
            break;

            case CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT:
            case CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE:
            {
                ret = semtech_udp_config_socket(socket, &socket_id, error_code);
            }
            break;

            default:
                break;
        }
    }
    return ret;
}

at_req_status_t
semtech_socket_close (uint32_t socket_id, cell_socket_service_type_t type,
                      uint16_t * error_code)
{
    at_req_status_t ret         = AT_PENDING;
    socket_info_t   socket_info = {0};

    if (is_socket_service_tcp(type))
    {
        ret = get_tcp_socket_status(socket_id, &socket_info, error_code);
        if (ret == AT_SUCCESS && socket_info.found)
        {
            switch (socket_info.status)
            {
                case SOCKET_CONNECTED:
                {
                    ret = tcp_close(socket_id, error_code);
                    /**
                     * Unlike other socket types, SOCKET_CHILD_CREATED_BY_SERVER
                     * config is already removed by tcp_close and do NOT require
                     * tcp_delete
                     */
                    if ((ret == AT_SUCCESS)
                        && (socket_info.mode != SOCKET_CHILD_CREATED_BY_SERVER))
                    {
                        ret = tcp_delete(socket_id, error_code);
                    }
                }
                break;

                case SOCKET_DISCONNECTED:
                {
                    ret = tcp_delete(socket_id, error_code);
                }
                break;

                default:
                    break;
            }
        }
    }
    else
    {
        ret = udp_close(socket_id, error_code);
    }

    return ret;
}

at_req_status_t
semtech_socket_check_usability (all_socket_status_t * socket_state_handle,
                                uint8_t context_id, uint16_t * error_code)
{
    at_req_status_t ret         = AT_PENDING;
    char            cmd_buf[32] = {0};
    at_request_t    at_req_socket_query_service_stat
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = NULL,
           .p_data       = socket_state_handle,
           .data_len     = sizeof(all_socket_status_t),
           .p_error_code = error_code};
    socket_state_handle->sockets[0] = true;
    socket_state_handle->no_supported_socket
        = SEMTECH_MAXIMUM_SUPPORT_SOCKET + 1;
    (void) context_id;
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPCFG?\r");
    at_req_socket_query_service_stat.resp_prefix = "+KTCPCFG";
    at_req_socket_query_service_stat.send_buff_len
        = strlen(at_req_socket_query_service_stat.p_send_buff);
    ret = at_command_request(
        &at_req_socket_query_service_stat, 5000, at_parser_socket_check_exist);

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KUDPCFG?\r");
    at_req_socket_query_service_stat.resp_prefix = "+KUDPCFG";
    at_req_socket_query_service_stat.send_buff_len
        = strlen(at_req_socket_query_service_stat.p_send_buff);
    ret = at_command_request(
        &at_req_socket_query_service_stat, 5000, at_parser_socket_check_exist);

    return ret;
}

at_req_status_t
semtech_socket_send (cell_socket_t * socket, const uint8_t * data,
                     size_t * length, uint16_t * error_code)
{
    at_req_status_t ret                = AT_PENDING;
    char            cmd_buf[70]        = {'\0'};
    at_request_t    at_req_send_socket = {.p_send_buff  = cmd_buf,
                                          .resp_prefix  = NULL,
                                          .p_data       = NULL,
                                          .data_len     = 0,
                                          .p_error_code = error_code};

    build_send_command(socket, cmd_buf, sizeof(cmd_buf), *length);
    at_req_send_socket.send_buff_len = strlen(at_req_send_socket.p_send_buff);
    ret = send_socket_data_wait_connect(&at_req_send_socket, 120000);

    if (AT_SUCCESS == ret)
    {
        cell_com_send_buffer((char *) data, *length);
        if (sizeof(cmd_buf) > strlen(EOF_PATTERN))
        {
            strcpy(cmd_buf, EOF_PATTERN);
            ret = at_command_request(&at_req_send_socket, 120000, NULL);
        }
    }
    return ret;
}

at_req_status_t
semtech_socket_receive (cell_socket_t * socket, const uint8_t * data,
                        size_t * data_length, uint16_t * error_code)
{
    at_req_status_t ret             = AT_PENDING;
    char            cmd_buf[32]     = {'\0'};
    size_t          unread_recv_len = 0;

    if (CELL_SOCKET_SERVICE_TYPE_TCP_SERVER == socket->service_type)
    {
        ret = semtech_socket_recv_unread_len_get(
            socket->incoming_id, &unread_recv_len, error_code);
    }
    else if (CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT == socket->service_type)
    {
        ret = semtech_socket_recv_unread_len_get(
            socket->socket_id, &unread_recv_len, error_code);
    }
    else
    {
        // UDP
        ret = AT_SUCCESS;
    }

    if (AT_SUCCESS == ret)
    {
        if ((is_socket_service_tcp(socket->service_type))
            && (unread_recv_len < *data_length))
        {
            *data_length = unread_recv_len;
        }

        cell_log_debug("Bytes received from socket = %d", *data_length);

        socket_data_recv_info_t data_recv_info
            = {socket->service_type, data, data_length, &socket->remote};
        at_request_t at_req_socket_receive
            = {.p_send_buff  = cmd_buf,
               .resp_prefix  = NULL,
               .p_data       = &data_recv_info,
               .data_len     = sizeof(data_recv_info),
               .p_error_code = error_code};

        build_socket_receive_command(
            socket, cmd_buf, sizeof(cmd_buf), *data_length);
        at_req_socket_receive.send_buff_len
            = strlen(at_req_socket_receive.p_send_buff);

        ret = at_command_request(
            &at_req_socket_receive, 120000, at_parser_socket_receive);
    }

    return ret;
}

void
semtech_handle_urc_ktcp_ind (void * data)
{
    urc_ktcp_ind_type_t * tcp_ind = (urc_ktcp_ind_type_t *) data;
    if (tcp_ind->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        if (tcp_ind->stat == SEMTECH_SOCKET_SESSION_STATUS_READY)
        {
            socket_process_urc_opened_success(tcp_ind->session_id);
        }
    }
}

void
semtech_handle_urc_ktcp_notif (void * data)
{
    urc_ktcp_notif_type_t * tcp_notif = (urc_ktcp_notif_type_t *) data;
    if (tcp_notif->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        if (tcp_notif->tcp_notif == URC_KTCP_NOTIF_TCP_DISCONNECTION_BY_REMOTE)
        {
            socket_process_urc_closed(tcp_notif->session_id);
        }
        else
        {
            socket_process_urc_open_failed(tcp_notif->session_id);
        }
    }
}

void
semtech_handle_urc_ktcp_srvreq (void * data)
{
    urc_ktcp_srvreq_type_t * tcp_incoming_info
        = (urc_ktcp_srvreq_type_t *) data;
    if ((tcp_incoming_info->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
        && (tcp_incoming_info->subsession_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET))
    {
        socket_process_urc_incoming_connection(tcp_incoming_info->subsession_id,
                                               tcp_incoming_info->session_id);
    }
}

void
semtech_handle_urc_ktcp_data (void * data)
{
    urc_ktcp_data_type_t * tcp_data = (urc_ktcp_data_type_t *) data;
    if (tcp_data->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        if (tcp_data->ndata > 0)
        {
            socket_process_urc_incoming_data(tcp_data->session_id);
        }
    }
}

void
semtech_handle_urc_cereg (void * data)
{
    urc_cereg_type_t *    type     = (urc_cereg_type_t *) data;
    connection_urc_type_t urc_type = CONNECTION_URC_INVALID;
    switch (type->status)
    {
        case URC_CREG_STATUS_REGISTERED:
        case URC_CREG_STATUS_REGISTERED_ROAMING:
            urc_type = CONNECTION_URC_REGISTERED;
            break;

        case URC_CREG_STATUS_NOT_REGISTERED_AND_IDLE:
            urc_type = CONNECTION_URC_NOT_REGISTERING_IDLE;
            break;

        case URC_CREG_STATUS_UNKNOWN:
            urc_type = CONNECTION_URC_NOT_REGISTERING_UNKNOWN;
            break;

        case URC_CREG_STATUS_DENIED:
            urc_type = CONNECTION_URC_REJECTED;
            break;

        case URC_CREG_STATUS_NOT_REGISTERED_AND_SEARCHING:
            urc_type = CONNECTION_URC_REGISTERING;
            break;

        default:
            break;
    }

    if (urc_type != CONNECTION_URC_INVALID)
    {
        connection_notify_urc(urc_type);
    }
}

void
semtech_handle_urc_kudp_notif (void * data)
{
    urc_kudp_notif_type_t * udp_notif = (urc_kudp_notif_type_t *) data;
    if (udp_notif->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        socket_process_urc_open_failed(udp_notif->session_id);
    }
}

void
semtech_handle_urc_kudp_ind (void * data)
{
    urc_kudp_ind_type_t * udp_ind = (urc_kudp_ind_type_t *) data;
    if (udp_ind->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        if (udp_ind->stat == SEMTECH_SOCKET_SESSION_STATUS_READY)
        {
            socket_process_urc_opened_success(udp_ind->session_id);
        }
    }
}

void
semtech_handle_urc_kudp_data (void * data)
{
    urc_kudp_data_type_t * udp_data = (urc_kudp_data_type_t *) data;
    if (udp_data->session_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET)
    {
        if (udp_data->ndata > 0)
        {
            socket_process_urc_incoming_data(udp_data->session_id);
        }
    }
}

cme_error_t
semtech_map_cme_error (uint16_t error_code)
{
    cme_error_t cme_err = CME_ERR_INVALID;
    switch (error_code)
    {
        case SEMTECH_HL_CME_ERR_USIM_NOT_INSERTED:
            cme_err = CME_ERR_SIM_NOT_INSERTED;
            break;

        case SEMTECH_HL_CME_ERR_USIM_FAILURE:
            cme_err = CME_ERR_SIM_NOT_READY;
            break;

        default:
            break;
    }

    return cme_err;
}

cms_error_t
semtech_map_cms_error (uint16_t error_code)
{
    cms_error_t cms_err = CMS_ERR_INVALID;
    return cms_err;
}

/*******************************************************
 * Private Function Bodies                             *
 *******************************************************/

static at_req_status_t
at_parser_semtech_pdp_authentication_get (at_request_t * at_req, char * line,
                                          uint64_t start_time,
                                          uint32_t timeout_ms)
{
    at_req_status_t       ret    = AT_SUCCESS;
    cell_pdp_settings_t * pdp    = (cell_pdp_settings_t *) at_req->p_data;
    int                   cid    = 0;
    int                   auth   = 0;

    int                   parsed = sscanf(line,
                        "+CGAUTH: %d,%d,\"%32[^\"]\",\"%32[^\"]\"",
                        &cid,
                        &auth,
                        pdp->username,
                        pdp->password);
    if (parsed >= 2)
    {
        pdp->context_id = (uint8_t) cid;
        pdp->auth_type  = (cell_pdp_auth_type_t) auth;
        ret             = seek_one_line_and_parse_final_resp(
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
define_pdp_context (cell_pdp_settings_t * pdp_settings, uint16_t * error_code)
{
    at_req_status_t ret                    = AT_PENDING;
    char            cmd_buf[180]           = {'\0'};
    at_request_t    at_req_set_pdp_context = {.p_send_buff  = cmd_buf,
                                              .resp_prefix  = NULL,
                                              .p_data       = NULL,
                                              .data_len     = 0,
                                              .p_error_code = error_code};

    const char *    pdp_type_str = pdp_type_to_str(pdp_settings->type);

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+CGDCONT=%d,\"%s\",\"%s\"\r",
                    pdp_settings->context_id,
                    pdp_type_str,
                    pdp_settings->apn_name);
    at_req_set_pdp_context.send_buff_len
        = strlen(at_req_set_pdp_context.p_send_buff);
    ret = at_command_request(&at_req_set_pdp_context, 300, NULL);

    return ret;
}

static at_req_status_t
semtech_pdp_authentication_set (cell_pdp_settings_t * pdp_settings,
                                uint16_t *            error_code)
{
    at_req_status_t ret                    = AT_PENDING;
    char            cmd_buf[180]           = {'\0'};
    at_request_t    at_req_set_pdp_context = {.p_send_buff  = cmd_buf,
                                              .resp_prefix  = NULL,
                                              .p_data       = NULL,
                                              .data_len     = 0,
                                              .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+CGAUTH=%d,%d,\"%s\",\"%s\"\r",
                    pdp_settings->context_id,
                    (int) pdp_settings->auth_type,
                    pdp_settings->username,
                    pdp_settings->password);
    at_req_set_pdp_context.send_buff_len
        = strlen(at_req_set_pdp_context.p_send_buff);
    ret = at_command_request(&at_req_set_pdp_context, 300, NULL);

    return ret;
}

static at_req_status_t
at_parser_semtech_iccid_get (at_request_t * at_req, char * line,
                             uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    char *          info   = (char *) at_req->p_data;
    int             parsed = sscanf(line, "+CCID: %20[^\n]", info);
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
at_parser_semtech_firmware_version (at_request_t * at_req, char * line,
                                    uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret    = AT_SUCCESS;
    char *          info   = (char *) at_req->p_data;
    int             parsed = sscanf(line, "%32[^\n]", info);
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
at_parser_semtech_service_domain_get (at_request_t * at_req, char * line,
                                      uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t         ret = AT_SUCCESS;
    cell_service_domain_t * service_domain
        = (cell_service_domain_t *) at_req->p_data;
    uint8_t service_cemode = 0;

    int     parsed         = sscanf(line, "+CEMODE: %2hhu", &service_cemode);
    if (parsed == 1)
    {
        switch (service_cemode)
        {
            case 1:
            case 2:
            {
                *service_domain = CELL_SERVICE_CS_PS;
                break;
            }
            default:
            {
                *service_domain = CELL_SERVICE_PS;
                break;
            }
        }
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

static bool
service_domain_mapping_cemode (cell_service_domain_t domain, uint8_t * cemode)
{
    bool correct_map = false;
    if (cemode != NULL)
    {
        for (size_t i = 0; i < sd_map_size; i++)
        {
            if (sd_map[i].serv_domain == domain)
            {
                *cemode     = sd_map[i].cemode;
                correct_map = true;
                break;
            }
        }
    }
    return correct_map;
}

static at_req_status_t
at_parser_semtech_operator_info_get (at_request_t * at_req, char * line,
                                     uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t        ret = AT_SUCCESS;
    cell_operator_info_t * operator=(cell_operator_info_t *) at_req -> p_data;
    int                    mode   = 0;
    int                    format = 0;
    int                    rat    = -1;

    int                    parsed = sscanf(line,
                        "+COPS: %d,%d,\"%64[^\"]\",%d",
                        &mode,
                        &format,
                        operator->name,
                        &rat);

    if (parsed >= 1)
    {
        ret = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    if (AT_SUCCESS == ret && parsed >= 1)
    {
        operator->mode =(cell_operator_selection_mode_t) mode;
        if (parsed >= 3)
        {
            operator->format =(cell_operator_name_format_t) format;
        }
        else
        {
            operator->format  = CELL_OPERATOR_NAME_FORMAT_NOT_PRESENT;
            operator->name[0] = '\0';
        }
        if (parsed >= 4)
        {
            switch (rat)
            {
                case URC_ACT_EUTRAN:
                    operator->rat = CELL_RAT_CATM1;
                    break;
                case URC_ACT_NB_IOT:
                    operator->rat = CELL_RAT_NBIOT;
                    break;

                default:
                    ret = AT_TIMEOUT;
                    break;
            }
        }
        else
        {
            operator->rat = CELL_RAT_INVALID;
        }
    }
    return ret;
}

// Use for at parser get PDP
static bool
pdp_type_from_str (const char * str, cell_pdp_context_type_t * type)
{
    bool found = false;

    if ((str != NULL) && (type != NULL))
    {
        for (size_t i = 0; i < pdp_type_map_size; i++)
        {
            if (strcmp(str, pdp_type_map[i].str) == 0)
            {
                *type = pdp_type_map[i].type;
                found = true;
                break;
            }
        }
    }
    return found;
}

static at_req_status_t
at_parser_semtech_pdp_settings_get (at_request_t * at_req, char * line,
                                    uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_pdp_settings_t * pdp_settings = (cell_pdp_settings_t *) at_req->p_data;

    int                   cid          = 0;
    char                  type_str[12] = {0};
    // Response : +CGDCONT:<cid>,<type>,<APN>
    int parsed = sscanf(line,
                        "+CGDCONT: %d,\"%11[^\"]\",\"%64[^\"]\"",
                        &cid,
                        type_str,
                        pdp_settings->apn_name);

    if (parsed >= 2)
    {
        pdp_settings->context_id = (uint8_t) cid;
        if (!pdp_type_from_str(type_str, &pdp_settings->type))
        {
            pdp_settings->type = CELL_PDP_CONTEXT_TYPE_MAX;
        }
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

// Use for set PDP
static const char *
pdp_type_to_str (cell_pdp_context_type_t type)
{
    for (size_t i = 0; i < pdp_type_map_size; i++)
    {
        if (pdp_type_map[i].type == type)
        {
            return pdp_type_map[i].str;
        }
    }
    return "";
}

static bool
match_rat_to_sequence (uint8_t rat, cell_rat_type_t * p_rat)
{
    for (uint32_t i = 0;
         i < (sizeof(semtech_rat_map) / sizeof(semtech_rat_map[0]));
         i++)
    {
        if (semtech_rat_map[i].modem_rat == rat)
        {
            *p_rat = semtech_rat_map[i].cell_rat;
            return true;
        }
    }
    return false;
}

static at_req_status_t
at_parser_rat_get (at_request_t * at_req, char * line, uint64_t start_time,
                   uint32_t timeout_ms)
{
    at_req_status_t       ret          = AT_SUCCESS;
    cell_rat_settings_t * rat_settings = (cell_rat_settings_t *) at_req->p_data;
    uint8_t               rat_value    = CELL_RAT_INVALID;

    // Response example: +KSRAT: 0
    int parsed = sscanf(line, "+KSRAT: %2hhu", &rat_value);

    if (parsed >= 1)
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
        // Clear sequence
        for (int i = 0; i < CELL_RAT_MAX; i++)
        {
            rat_settings->sequence[i] = CELL_RAT_INVALID;
        }
        cell_rat_type_t rat = CELL_RAT_INVALID;

        if (match_rat_to_sequence(rat_value, &rat))
        {
            rat_settings->sequence[0] = rat;
        }
        else
        {
            ret = AT_ERROR_GENERIC;
        }
    }
    return ret;
}

static cell_rat_type_t
semtech_band_mapping_rat (uint8_t modem_rat)
{
    cell_rat_type_t result = CELL_RAT_INVALID;
    for (size_t i = 0; i < sizeof(semtech_rat_map) / sizeof(semtech_rat_map[0]);
         i++)
    {
        if (semtech_rat_map[i].modem_rat == modem_rat)
        {
            result = semtech_rat_map[i].cell_rat;
            break;
        }
    }
    return result;
}

static at_req_status_t
at_parser_semtech_band_get (at_request_t * at_req, char * line,
                            uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret     = AT_SUCCESS;
    cell_rat_settings_t * rat_set = (cell_rat_settings_t *) at_req->p_data;

    uint8_t               rat_id  = 0;
    char                  band_val[LTE_SEMTECH_BAND_MASK_STR_MAX] = {0};
    bool                  continue_parsing                        = true;
    /* Meet OK right away */
    if (strncmp(line, "OK", 2) == 0
        && (line[2] == '\0' || line[2] == '\r' || line[2] == '\n'))
    {
        ret              = AT_SUCCESS;
        continue_parsing = false;
    }

    /* Parse multi line +KBNDCFG */
    while (continue_parsing == true)
    {
        /* Response: +KBNDCFG: <rat>,<band_bitmap> */
        int parsed = sscanf(line, "+KBNDCFG: %2hhu,%31s", &rat_id, band_val);

        if (parsed == 2)
        {
            cell_rat_type_t rat = semtech_band_mapping_rat(rat_id);

            switch (rat)
            {
                case CELL_RAT_CATM1:
                    helper_parse_lte_band(rat_set->catm1_band,
                                          band_val,
                                          semtech_catm_band_map,
                                          semtech_catm_band_map_size);
                    break;

                case CELL_RAT_NBIOT:
                    helper_parse_lte_band(rat_set->nbiot_band,
                                          band_val,
                                          semtech_nbiot_band_map,
                                          semtech_nbiot_band_map_size);
                    break;

                default:
                    break;
            }

            /* get next line / check OK */
            ret = seek_one_line_and_parse_final_resp(
                line, start_time, timeout_ms, at_req->p_error_code);

            if (ret == AT_SUCCESS)
            {
                continue_parsing = false;
            }
        }
        else
        {
            ret = seek_and_parse_error_resp(
                line, start_time, timeout_ms, at_req->p_error_code);
            continue_parsing = false;
        }
    }

    return ret;
}

static bool
match_seq_to_rat (cell_rat_type_t cell_rat, uint8_t * p_modem_rat)
{
    for (uint32_t i = 0;
         i < sizeof(semtech_rat_map) / sizeof(semtech_rat_map[0]);
         i++)
    {
        if (semtech_rat_map[i].cell_rat == cell_rat)
        {
            *p_modem_rat = semtech_rat_map[i].modem_rat;
            return true;
        }
    }
    return false;
}

static at_req_status_t
rat_set (cell_rat_settings_t * rat, uint16_t * error_code)
{
    at_req_status_t ret            = AT_PENDING;
    char            cmd_buf[32]    = {'\0'};
    uint8_t         modem_rat      = CELL_RAT_INVALID;
    at_request_t    at_req_set_rat = {.p_send_buff  = cmd_buf,
                                      .resp_prefix  = NULL,
                                      .p_data       = NULL,
                                      .data_len     = 0,
                                      .p_error_code = error_code};

    if ((rat != NULL) && (rat->sequence[0] != CELL_RAT_INVALID))
    {
        if (match_seq_to_rat(rat->sequence[0], &modem_rat))
        {
            (void) snprintf(
                cmd_buf, sizeof(cmd_buf), "AT+KSRAT=%d,1\r", modem_rat);
            at_req_set_rat.send_buff_len = strlen(at_req_set_rat.p_send_buff);

            ret = at_command_request(&at_req_set_rat, 10000, NULL);
        }
    }

    return ret;
}

static at_req_status_t
semtech_band_set (cell_rat_settings_t * rat_settings, uint16_t * error_code)
{
    at_req_status_t ret                       = AT_SUCCESS;
    char            cmd_buf[90]               = {'\0'};
    char            band_mask[MAX_HL_HEX_LEN] = {0};
    at_request_t    at_req_set_band           = {.p_send_buff  = cmd_buf,
                                                 .resp_prefix  = NULL,
                                                 .p_data       = NULL,
                                                 .data_len     = 0,
                                                 .p_error_code = error_code};

    // CATM: RAT = 0
    helper_calculate_band_mask(rat_settings->catm1_band,
                               band_mask,
                               sizeof(band_mask),
                               lte_semtech_band_hex_map);
    if ((strcmp(band_mask, "0") != 0) && (ret == AT_SUCCESS))
    {
        snprintf(cmd_buf, sizeof(cmd_buf), "AT+KBNDCFG=0,%s\r", band_mask);
        at_req_set_band.send_buff_len = strlen(at_req_set_band.p_send_buff);
        ret = at_command_request(&at_req_set_band, 300, NULL);
    }

    // NBIOT: RAT = 1
    memset(band_mask, 0, sizeof(band_mask));
    helper_calculate_band_mask(rat_settings->nbiot_band,
                               band_mask,
                               sizeof(band_mask),
                               lte_semtech_band_hex_map);
    if ((strcmp(band_mask, "0") != 0) && (ret == AT_SUCCESS))
    {
        snprintf(cmd_buf, sizeof(cmd_buf), "AT+KBNDCFG=1,%s\r", band_mask);
        at_req_set_band.send_buff_len = strlen(at_req_set_band.p_send_buff);
        ret = at_command_request(&at_req_set_band, 300, NULL);
    }
    return ret;
}

static at_req_status_t
semtech_pdp_context_info_get (cell_network_info_t * p_net_info,
                              uint16_t *            error_code)
{
    at_req_status_t at_status           = AT_PENDING;
    at_request_t    at_req_get_act_info = {.p_send_buff = "AT+CGACT?\r",
                                           .resp_prefix = "+CGACT",
                                           .p_data      = p_net_info,
                                           .data_len = sizeof(cell_network_info_t),
                                           .p_error_code = error_code};

    at_req_get_act_info.send_buff_len = strlen(at_req_get_act_info.p_send_buff);
    at_status                         = at_command_request(
        &at_req_get_act_info, 60000, at_parser_semtech_pdp_context_info_get);

    return at_status;
}

static at_req_status_t
semtech_pdp_address_get (cell_network_info_t * p_net_info,
                         uint16_t *            error_code)
{
    at_req_status_t at_status = AT_PENDING;
    at_request_t    at_req_get_pdp_status
        = {.p_send_buff  = "AT+CGDCONT?\r",
           .resp_prefix  = "+CGDCONT",
           .p_data       = p_net_info,
           .data_len     = (uint16_t) sizeof(cell_network_info_t),
           .p_error_code = error_code};

    at_req_get_pdp_status.send_buff_len
        = strlen(at_req_get_pdp_status.p_send_buff);
    at_status = at_command_request(
        &at_req_get_pdp_status, 5000, at_parser_semtech_pdp_stat_get);

    return at_status;
}

static at_req_status_t
at_parser_semtech_pdp_context_info_get (at_request_t * at_req, char * line,
                                        uint64_t start_time,
                                        uint32_t timeout_ms)
{
    at_req_status_t       ret = AT_PENDING;
    cell_network_info_t * activate_info
        = (cell_network_info_t *) at_req->p_data;

    int cid    = 0;
    int state  = 0;
    int parsed = sscanf(line, "+CGACT: %d,%d", &cid, &state);

    if (parsed == 2)
    {
        activate_info->cid   = (uint8_t) cid;
        activate_info->state = (cell_pdp_context_state_t) state;
        ret                  = seek_one_line_and_parse_final_resp(
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
at_parser_semtech_pdp_stat_get (at_request_t * at_req, char * line,
                                uint64_t start_time, uint32_t timeout_ms)
{
    cell_network_info_t * p_net_info  = (cell_network_info_t *) at_req->p_data;
    at_req_status_t       at_stat     = AT_PENDING;
    int                   cid         = 0;
    char                  pdp_type[8] = {0};
    char                  ip_addr[40] = {'\0'};

    // PDP Activated -> IP available
    if ((sscanf(line,
                "+CGDCONT: %d,\"%7[^\"]\",\"%*64[^\"]\",%39[^,]",
                &cid,
                pdp_type,
                ip_addr)
         == 3))
    {
        p_net_info->cid = (uint8_t) cid;
        if (!pdp_type_from_str(pdp_type, &p_net_info->pdp_type))
        {
            p_net_info->pdp_type = CELL_PDP_CONTEXT_TYPE_MAX;
        }
        helper_ip_convert(ip_addr, p_net_info);
        at_stat = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        at_stat = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return at_stat;
}

static at_req_status_t
semtech_enable_sim_functionality (uint16_t * error_code)
{
    at_req_status_t         ret   = AT_PENDING;
    cell_ue_functionality_t state = CELL_UE_FULL_FUNCTIONALITY;
    ret = common_ue_functionality_set(state, error_code);
    if (AT_SUCCESS == ret)
    {
        // wait for modem fully enable
        cell_os_sleep_ms(5000);
    }
    return ret;
}

static at_req_status_t
semtech_gprs_connection_set (cell_pdp_settings_t * pdp, uint16_t * error_code)
{
    at_req_status_t ret                   = AT_PENDING;
    char            cmd_buf[180]          = {'\0'};
    at_request_t    at_req_connect_config = {.p_send_buff  = cmd_buf,
                                             .resp_prefix  = NULL,
                                             .p_data       = NULL,
                                             .data_len     = 0,
                                             .p_error_code = error_code};

    (void) snprintf(cmd_buf,
                    sizeof(cmd_buf),
                    "AT+KCNXCFG=%d,\"GPRS\",\"%s\",\"%s\",\"%s\"\r",
                    pdp->context_id,
                    pdp->apn_name,
                    pdp->username,
                    pdp->password);
    at_req_connect_config.send_buff_len
        = strlen(at_req_connect_config.p_send_buff);

    ret = at_command_request(&at_req_connect_config, 30000, NULL);
    return ret;
}

static at_req_status_t
semtech_tcp_config_socket (const cell_socket_settings_t * socket,
                           uint32_t * socket_id, uint16_t * error_code)
{
    at_req_status_t ret                                      = AT_PENDING;
    char            cmd_buf[128]                             = {'\0'};
    char            ip_address[CELL_IP_ADDRESS_MAX_SIZE + 1] = {0};

    at_request_t    at_req_tcp_config_socket
        = {.p_send_buff  = cmd_buf,
           .resp_prefix  = "+KTCPCFG",
           .p_data       = socket_id,
           .data_len     = (uint16_t) sizeof(uint32_t),
           .p_error_code = error_code};
    if (socket->service_type == CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT)
    {
        helper_ip_to_str(socket->remote, ip_address),
            (void) snprintf(cmd_buf,
                            sizeof(cmd_buf),
                            "AT+KTCPCFG=%d,0,\"%s\",%d\r",
                            socket->context_id,
                            ip_address,
                            socket->remote.port);
    }
    else
    {
        (void) snprintf(cmd_buf,
                        sizeof(cmd_buf),
                        "AT+KTCPCFG=%d,1,,%d\r",
                        socket->context_id,
                        socket->local.port);
    }

    at_req_tcp_config_socket.send_buff_len
        = strlen(at_req_tcp_config_socket.p_send_buff);
    ret = at_command_request(
        &at_req_tcp_config_socket, 150000, at_parser_socket_id);

    return ret;
}

static at_req_status_t
at_parser_socket_id (at_request_t * at_req, char * line, uint64_t start_time,
                     uint32_t timeout_ms)
{
    at_req_status_t ret       = AT_SUCCESS;
    uint32_t *      socket_id = (uint32_t *) at_req->p_data;
    int             parsed    = sscanf(line, "+KTCPCFG: %u", socket_id);
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
tcp_connect (uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t ret                = AT_PENDING;
    char            cmd_buf[128]       = {'\0'};
    at_request_t    at_req_tcp_connect = {.p_send_buff  = cmd_buf,
                                          .resp_prefix  = NULL,
                                          .p_data       = NULL,
                                          .data_len     = 0,
                                          .p_error_code = error_code};
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPCNX=%u\r", socket_id);
    at_req_tcp_connect.send_buff_len = strlen(at_req_tcp_connect.p_send_buff);

    ret = at_command_request(&at_req_tcp_connect, 3000, NULL);
    return ret;
}

static at_req_status_t
tcp_close (uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t ret              = AT_PENDING;
    char            cmd_buf[32]      = {'\0'};
    at_request_t    at_req_close_tcp = {.p_send_buff  = cmd_buf,
                                        .resp_prefix  = NULL,
                                        .p_data       = NULL,
                                        .data_len     = 0,
                                        .p_error_code = error_code};
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPCLOSE=%u,1\r", socket_id);
    at_req_close_tcp.send_buff_len = strlen(at_req_close_tcp.p_send_buff);

    ret = at_command_request(&at_req_close_tcp, 20000, NULL);

    return ret;
}

static at_req_status_t
tcp_delete (uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t ret            = AT_PENDING;
    char            cmd_buf[32]    = {'\0'};
    at_request_t    at_req_tcp_del = {.p_send_buff  = cmd_buf,
                                      .resp_prefix  = NULL,
                                      .p_data       = NULL,
                                      .data_len     = 0,
                                      .p_error_code = error_code};
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPDEL=%u\r", socket_id);
    at_req_tcp_del.send_buff_len = strlen(at_req_tcp_del.p_send_buff);
    ret = at_command_request(&at_req_tcp_del, 20000, NULL);
    return ret;
}

static at_req_status_t
get_tcp_socket_status (uint32_t socket_id, socket_info_t * socket_info,
                       uint16_t * error_code)
{
    at_req_status_t ret              = AT_PENDING;
    char            cmd_buf[32]      = {0};
    at_request_t    at_req_socket_id = {.p_send_buff  = cmd_buf,
                                        .resp_prefix  = "+KTCPCFG",
                                        .p_data       = socket_info,
                                        .data_len     = sizeof(socket_info_t),
                                        .p_error_code = error_code};

    socket_info->socket_id           = socket_id;
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPCFG?\r");
    at_req_socket_id.send_buff_len = strlen(at_req_socket_id.p_send_buff);
    ret                            = at_command_request(
        &at_req_socket_id, 5000, at_parser_get_tcp_socket_status);
    return ret;
}

static at_req_status_t
at_parser_get_tcp_socket_status (at_request_t * at_req, char * line,
                                 uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret                    = AT_PENDING;
    socket_info_t * socket_info            = (socket_info_t *) at_req->p_data;
    unsigned int    socket_id              = 0U;
    unsigned int    status                 = 0U;
    unsigned int    mode                   = 0U;
    uint8_t         no_existing_connection = 0;

    socket_info->found                     = false;

    if (strcmp(line, "OK") == 0)
    {
        ret = AT_SUCCESS;
    }
    else
    {
        while (no_existing_connection < SEMTECH_MAXIMUM_SUPPORT_SOCKET)
        {
            int parsed = sscanf(
                line, "+KTCPCFG: %u,%u,%*d,%u", &socket_id, &status, &mode);
            if (parsed == 3)
            {
                if (socket_info->socket_id == socket_id)
                {

                    socket_info->mode   = (socket_mode_t) mode;
                    socket_info->status = (socket_status_t) status;
                    socket_info->found  = true;
                    ret                 = seek_and_parse_final_resp(
                        line, start_time, timeout_ms, at_req->p_error_code);

                    break;
                }

                no_existing_connection++;
                ret = seek_one_line_and_parse_final_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                if (ret == AT_SUCCESS)
                {
                    break;
                }
            }
            else
            {
                ret = seek_and_parse_error_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                break;
            }
        }
    }

    return ret;
}

static at_req_status_t
at_parser_socket_check_exist (at_request_t * at_req, char * line,
                              uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t       ret = AT_PENDING;

    all_socket_status_t * socket_state_handle
        = (all_socket_status_t *) at_req->p_data;

    uint8_t  no_existing_connection = 0;
    uint32_t socket_id              = 0;

    if (strcmp(line, "OK") == 0)
    {
        ret = AT_SUCCESS;
    }
    else
    {
        const char * format = NULL;
        if (strcmp(at_req->resp_prefix, "+KTCPCFG") == 0)
        {
            format = "+KTCPCFG: %u";
        }
        else
        {
            format = "+KUDPCFG: %u";
        }
        while (no_existing_connection < SEMTECH_MAXIMUM_SUPPORT_SOCKET)
        {
            if (sscanf(line, format, &socket_id) == 1)
            {
                if ((socket_id >= 1)
                    && (socket_id <= SEMTECH_MAXIMUM_SUPPORT_SOCKET))
                {
                    socket_state_handle->sockets[socket_id] = true;
                }
                no_existing_connection++;
                ret = seek_one_line_and_parse_final_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                if (ret == AT_SUCCESS)
                {
                    break;
                }
            }
            else
            {
                ret = seek_and_parse_error_resp(
                    line, start_time, timeout_ms, at_req->p_error_code);
                break;
            }
        }
    }
    return ret;
}

static void
build_send_command (cell_socket_t * socket, char * cmd_buf, uint16_t cmd_len,
                    size_t send_lend)
{

    switch (socket->service_type)
    {
        case CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KTCPSND=%u,%u\r",
                            socket->socket_id,
                            send_lend);
            break;

        case CELL_SOCKET_SERVICE_TYPE_TCP_SERVER:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KTCPSND=%u,%u\r",
                            socket->incoming_id,
                            send_lend);
            break;

        case CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KUDPSND=%u,"
                            ",,%u\r",
                            socket->socket_id,
                            send_lend);
            break;

        case CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE:
        {
            char ip_addr[CELL_IP_ADDRESS_MAX_SIZE + 1] = {0};
            helper_ip_to_str(socket->remote, ip_addr);
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KUDPSND=%u,\"%s\",%u,%u\r",
                            socket->socket_id,
                            ip_addr,
                            socket->remote.port,
                            send_lend);
            break;
        }

        default:
            break;
    }
}

static at_req_status_t
send_socket_data_wait_connect (at_request_t * at_req, uint32_t timeout)
{
    at_req_status_t ret        = AT_TIMEOUT;
    uint64_t        start_time = 0;

    cell_com_send_buffer(at_req->p_send_buff, at_req->send_buff_len);

    start_time                     = cell_os_get_time_ms();

    char line[AT_MAX_LINE_LEN + 1] = {0};
    while (at_read_line(line, AT_MAX_LINE_LEN, start_time, timeout))
    {
        if (0 == strcmp(line, "CONNECT"))
        {
            ret = AT_SUCCESS;
            break;
        }
        urc_parse_unsolicited_response(line);
    }
    return ret;
}

static at_req_status_t
semtech_socket_recv_unread_len_get (uint32_t   socket_id,
                                    size_t *   unread_recv_len,
                                    uint16_t * error_code)
{
    at_req_status_t ret                        = AT_PENDING;
    char            cmd_buf[32]                = {'\0'};
    at_request_t    at_req_socket_get_recv_len = {.p_send_buff = cmd_buf,
                                                  .resp_prefix = "+KTCPSTAT",
                                                  .p_data   = unread_recv_len,
                                                  .data_len = sizeof(size_t),
                                                  .p_error_code = error_code};

    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KTCPSTAT=%u\r", socket_id);
    at_req_socket_get_recv_len.send_buff_len
        = strlen(at_req_socket_get_recv_len.p_send_buff);

    ret = at_command_request(&at_req_socket_get_recv_len,
                             120000,
                             at_parser_socket_get_unread_recv_len);

    return ret;
}

static at_req_status_t
at_parser_socket_get_unread_recv_len (at_request_t * at_req, char * line,
                                      uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret               = AT_SUCCESS;
    size_t *        unread_length     = (size_t *) at_req->p_data;

    unsigned int    tcp_status        = 0U;
    unsigned int    tcp_notif         = 0U;
    unsigned int    remaining_tx_data = 0U;
    unsigned int    unread_len        = 0U;
    int             parsed            = sscanf(line,
                        "+KTCPSTAT: %u,%u,%u,%u",
                        &tcp_status,
                        &tcp_notif,
                        &remaining_tx_data,
                        &unread_len);

    if (parsed == 4)
    {
        *unread_length = (size_t) unread_len;
        ret            = seek_one_line_and_parse_final_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }
    return ret;
}

/**
 * @brief Parse socket receive responses returned by AT+KTCPRCV or
 * AT+KUDPRCV.
 *
 * After the modem returns the "CONNECT" indication, this function receives
 * the socket payload, parses the final command response and, for UDP sockets,
 * retrieves the remote endpoint information returned by the modem.
 *
 * @param[in,out] at_req AT request context. p_data must point to a
 * socket_data_recv_info_t structure. The receive buffer and payload length
 * are updated on success.
 * @param[in] line Current response line from the AT parser.
 * @param[in] start_time Timestamp (ms) when the command was issued.
 * @param[in] timeout_ms Maximum allowed operation time in milliseconds.
 *
 * @return AT_SUCCESS if the payload and modem responses are parsed
 * successfully.
 * @return Error status otherwise.
 */
static at_req_status_t
at_parser_socket_receive (at_request_t * at_req, char * line,
                          uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t           ret = AT_SUCCESS;
    socket_data_recv_info_t * recv_info
        = (socket_data_recv_info_t *) at_req->p_data;

    if (strcmp(line, "CONNECT") == 0)
    {
        ret = socket_receive_until_eof(recv_info, start_time, timeout_ms);

        if (ret == AT_SUCCESS)
        {
            ret = seek_one_line_and_parse_final_resp(
                line, start_time, timeout_ms, at_req->p_error_code);

            if ((AT_SUCCESS == ret)
                && (!is_socket_service_tcp(recv_info->service_type)))
            {
                ret = udp_receive_remote_info_get(
                    line, recv_info, start_time, timeout_ms);
            }
        }
    }
    else
    {
        ret = seek_and_parse_error_resp(
            line, start_time, timeout_ms, at_req->p_error_code);
    }

    return ret;
}

static void
build_socket_receive_command (cell_socket_t * socket, char * cmd_buf,
                              uint16_t cmd_len, uint16_t recv_len)
{
    switch (socket->service_type)
    {
        case CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KTCPRCV=%u,%u\r",
                            socket->socket_id,
                            recv_len);
            break;

        case CELL_SOCKET_SERVICE_TYPE_TCP_SERVER:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KTCPRCV=%u,%u\r",
                            socket->incoming_id,
                            recv_len);
            break;

        case CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT:
        case CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE:
            (void) snprintf(cmd_buf,
                            cmd_len,
                            "AT+KUDPRCV=%u,%u\r",
                            socket->socket_id,
                            recv_len);
            break;
        default:
            break;
    }
}

static at_req_status_t
semtech_udp_config_socket (const cell_socket_settings_t * socket,
                           uint32_t * socket_id, uint16_t * error_code)
{
    at_req_status_t ret                                      = AT_PENDING;
    char            cmd_buf[128]                             = {'\0'};
    char            ip_address[CELL_IP_ADDRESS_MAX_SIZE + 1] = {0};

    at_request_t    at_req_udp_config_socket = {.p_send_buff  = cmd_buf,
                                                .resp_prefix  = "KUDPCFG",
                                                .p_data       = socket_id,
                                                .data_len     = 0,
                                                .p_error_code = error_code};
    switch (socket->service_type)
    {
        case CELL_SOCKET_SERVICE_TYPE_UDP_CLIENT:
            helper_ip_to_str(socket->remote, ip_address);
            (void) snprintf(cmd_buf,
                            sizeof(cmd_buf),
                            "AT+KUDPCFG=%d,0,,,\"%s\",%d\r",
                            socket->context_id,
                            ip_address,
                            socket->remote.port);
            break;
        case CELL_SOCKET_SERVICE_TYPE_UDP_SERVICE:
            (void) snprintf(cmd_buf,
                            sizeof(cmd_buf),
                            "AT+KUDPCFG=%d,1,%d\r",
                            socket->context_id,
                            socket->local.port);
            break;
        default:
            break;
    }
    at_req_udp_config_socket.send_buff_len
        = strlen(at_req_udp_config_socket.p_send_buff);
    ret = at_command_request(&at_req_udp_config_socket, 150000, NULL);
    return ret;
}

static at_req_status_t
udp_close (uint32_t socket_id, uint16_t * error_code)
{
    at_req_status_t ret              = AT_PENDING;
    char            cmd_buf[32]      = {'\0'};
    at_request_t    at_req_close_udp = {.p_send_buff  = cmd_buf,
                                        .resp_prefix  = NULL,
                                        .p_data       = NULL,
                                        .data_len     = 0,
                                        .p_error_code = error_code};
    (void) snprintf(cmd_buf, sizeof(cmd_buf), "AT+KUDPCLOSE=%u\r", socket_id);
    at_req_close_udp.send_buff_len = strlen(at_req_close_udp.p_send_buff);
    ret = at_command_request(&at_req_close_udp, 20000, NULL);
    return ret;
}

static at_req_status_t
udp_receive_remote_info_get (char * line, socket_data_recv_info_t * udp_remote,
                             uint64_t start_time, uint32_t timeout_ms)
{
    at_req_status_t ret = AT_TIMEOUT;
    if (at_read_line(line, AT_MAX_LINE_LEN, start_time, timeout_ms))
    {
        char     addr[64] = {0};
        uint16_t port     = 0U;
        if (sscanf(line, "+KUDP_RCV: \"%63[^\"]\",%hu", addr, &port) == 2)
        {
            if (helper_str_to_ip(udp_remote->remote, addr))
            {
                udp_remote->remote->port = port;
                ret                      = AT_SUCCESS;
            }
            else
            {
                ret = AT_ERROR_GENERIC;
            }
        }
    }
    return ret;
}

/**
 * @brief Receive socket payload until EOF_PATTERN is detected.
 *
 * Reads payload bytes from the modem and stops when EOF_PATTERN is found.
 * The EOF marker is excluded from the returned payload length.
 *
 * @param[in,out] recv_info Receive context.
 * @param[in] start_time Timestamp (ms) when the receive operation started.
 * @param[in] timeout_ms Maximum allowed operation time in milliseconds.
 *
 * @return AT_SUCCESS if EOF_PATTERN is detected.
 * @return AT_TIMEOUT if the operation times out before EOF_PATTERN is
 * received.
 */
static at_req_status_t
socket_receive_until_eof (socket_data_recv_info_t * recv_info,
                          uint64_t start_time, uint32_t timeout_ms)
{
    /**
     * Data format: CONNECT\n<data>EOF_PATTERN
     * Read bytes until EOF_PATTERN ("--EOF--Pattern--") is detected.
     */
    at_req_status_t ret        = AT_TIMEOUT;
    size_t          data_len   = *recv_info->data_length;
    size_t          total_read = 0;
    uint8_t *       buf        = (uint8_t *) recv_info->data;
    const char *    eof_pat    = EOF_PATTERN;
    const size_t    eof_len    = strlen(eof_pat);

    while ((cell_os_get_time_ms() - start_time < timeout_ms)
           && (total_read < (data_len + eof_len)))
    {
#ifdef __TRUSTINSOFT_ANALYZER__
        tis_variable_split(&total_read, sizeof(total_read), 10000);
#endif
        int32_t bytes_gotten = cell_com_read_last_bytes(
            (char *) buf + total_read, 1, false, '\0');
        if (bytes_gotten <= 0)
        {
            cell_os_sleep_ms(1);
            continue;
        }
        total_read += bytes_gotten;
        if ((total_read >= eof_len)
            && (memcmp(buf + total_read - eof_len, eof_pat, eof_len) == 0))
        {
            *recv_info->data_length = total_read - eof_len;
            ret                     = AT_SUCCESS;
            break;
        }
    }
    return ret;
}

static bool
is_socket_service_tcp (cell_socket_service_type_t service_type)
{
    return (service_type == CELL_SOCKET_SERVICE_TYPE_TCP_CLIENT)
           || (service_type == CELL_SOCKET_SERVICE_TYPE_TCP_SERVER);
}
