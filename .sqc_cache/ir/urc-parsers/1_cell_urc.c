#include <sqc_builtin.h>
#ifndef SQC_LEGACY_COMPAT_ALIASES
#define SQC_LEGACY_COMPAT_ALIASES
#ifndef tis_interval
#define tis_interval sqc_interval
#endif
#ifndef tis_int_interval
#define tis_int_interval sqc_int_interval
#endif
#ifndef tis_long_long_interval
#define tis_long_long_interval sqc_long_long_interval
#endif
#ifndef tis_unsigned_char_interval
#define tis_unsigned_char_interval sqc_unsigned_char_interval
#endif
#ifndef tis_unsigned_int_interval
#define tis_unsigned_int_interval sqc_unsigned_int_interval
#endif
#ifndef tis_unsigned_long_interval
#define tis_unsigned_long_interval sqc_unsigned_long_interval
#endif
#ifndef tis_interval_split
#define tis_interval_split sqc_interval_split
#endif
#ifndef tis_make_unknown
#define tis_make_unknown sqc_make_unknown
#endif
#ifndef tis_nondet
#define tis_nondet sqc_nondet
#endif
#ifndef tis_nondet_ptr
#define tis_nondet_ptr sqc_nondet_ptr
#endif
#ifndef tis_nondet3_ptr
#define tis_nondet3_ptr sqc_nondet3_ptr
#endif
#ifndef tis_nondet5_ptr
#define tis_nondet5_ptr sqc_nondet5_ptr
#endif
#ifndef tis_find_variable
#define tis_find_variable sqc_find_variable
#endif
#ifndef tis_override
#define tis_override sqc_override
#endif
#ifndef tis_ub
#define tis_ub sqc_ub
#endif
#ifndef tis_entropy_source
#define tis_entropy_source sqc_entropy_source
#endif
#endif
#line 1 "/home/nguyentiem/Project/Code_quality/cmcell/modem/src/cell_urc.c"
/**
 * @file cell_urc.c
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
 * @author Dev : NGUYEN HUY Hai
 * @brief Implementation URC's parser and handler.
 * @date 2025-06-06
 */
#include "cell_urc.h"
#include "cell_com.h"

#include "cell_log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define UNUSED_PARAM(x) (void) (x)

typedef enum
{
    PARSER_SUCCESS,
    PARSER_FAILURE
} urc_parser_status_t;

typedef struct
{
    const char * token;
    void (*parser)(const char * urc_line);
} urc_map_t;

typedef struct
{
    const char *                 rat_str;
    urc_cell_access_technology_t rat_type;
} urc_rat_map_t;

static const urc_rat_map_t rat_map[] = {
    {"GSM",   URC_ACT_GSM   },
    {"eMTC",  URC_ACT_eMTC  },
    {"NBIoT", URC_ACT_NB_IOT}
};

static void set_bit(uint8_t * x, uint8_t pos);

static void clear_bit(uint8_t * x, uint8_t pos);

static int  convert_ip_str(const char * ip_str, urc_ip_addr_union_t * ip_data,
                           bool is_ipv4);

static urc_cell_access_technology_t get_rat_type(const char * rat_type_str);

static urc_parser_status_t bin_string_to_uint4(const char * str, uint8_t * u4);

static urc_type_t          get_urc_index(const char * urc_line);

static void                parse_urc_qusim(const char * p_line);

static void                parse_urc_cpin(const char * p_line);

static void                parse_urc_qsimstat(const char * p_line);

static void                parse_urc_qind_csq(const char * p_line);

static void                parse_urc_cring(const char * p_line);

static void                parse_urc_ctzv(const char * p_line);

static void                parse_urc_ctze(const char * p_line);

static void                parse_urc_cmti(const char * p_line);

static void                parse_urc_cmt_pdu(const char * p_line);

static void                parse_urc_cbm_pdu(const char * p_line);

static void                parse_urc_cds_pdu(const char * p_line);

static void                parse_urc_cdsi(const char * p_line);

static void                parse_urc_colp(const char * p_line);

static void                parse_urc_clip(const char * p_line);

static void                parse_urc_ccwa(const char * p_line);

static void                parse_urc_cssi(const char * p_line);

static void                parse_urc_cssu(const char * p_line);

static void                parse_urc_cedrxp(const char * p_line);

static void                parse_urc_qptwedrxp(const char * p_line);

static void                parse_urc_cgev_reject(const char * p_line);

static void                parse_urc_cgev_nw_react(const char * p_line);

static void                parse_urc_cgev_nw_deact(const char * p_line);

static void                parse_urc_cgev_me_deact(const char * p_line);

static void                parse_urc_cgev_nw_class(const char * p_line);

static void                parse_urc_cgev_me_class(const char * p_line);

static void                parse_urc_cgev_pdp_act(const char * p_line);

static void                parse_urc_cgev_pdp_deact(const char * p_line);

static void                parse_urc_creg(const char * p_line);

static void                parse_urc_cgreg(const char * p_line);

static void                parse_urc_cereg(const char * p_line);

static void                parse_urc_qiopen(const char * p_line);

static void                parse_urc_qiurc_closed(const char * p_line);

static void parse_urc_qiurc_recv_buffer_mode(const char * p_line);

static void parse_urc_qiurc_incoming(const char * p_line);

static void parse_urc_qiurc_pdp_deact(const char * p_line);

static void parse_urc_qiurc_dnsgip(const char * p_line);

static void parse_urc_qntp(const char * p_line);

static void parse_urc_qping(const char * p_line);

static void parse_urc_qssl_open(const char * p_line);

static void parse_urc_qcscon(const char * p_line);

static void parse_urc_qssl_buffer_mode(const char * p_line);

static void parse_urc_qssl_closed(const char * p_line);

static void parse_urc_fota_downloading(const char * p_line);

static void parse_urc_fota_downloaded(const char * p_line);

static void parse_urc_fota_httpend(const char * p_line);

static void parse_urc_fota_coapend(const char * p_line);

static void parse_urc_fota_updating(const char * p_line);

static void parse_urc_fota_end(const char * p_line);

static void parse_urc_fota_ftpend(const char * p_line);

static void parse_urc_fota_httpstart(const char * p_line);

static void parse_urc_fota_coapstart(const char * p_line);

static void parse_urc_fota_ftpstart(const char * p_line);

static void parse_urc_fota_start_updating(const char * p_line);

static void parse_urc_qhttp_get(const char * p_line);

static void parse_urc_qhttp_post(const char * p_line);

static void parse_urc_qhttp_postfile(const char * p_line);

static void parse_urc_qhttp_put(const char * p_line);

static void parse_urc_qhttp_putfile(const char * p_line);

static void parse_urc_qhttp_read(const char * p_line);

static void parse_urc_qhttp_readfile(const char * p_line);

static void parse_urc_qmtsn_stat(const char * p_line);

static void parse_urc_qmtsn_recv(const char * p_line);

static void parse_urc_qmtsn_sub(const char * p_line);

static void parse_urc_qmtsn_uns(const char * p_line);

static void parse_urc_qmtsn_reg(const char * p_line);

static void parse_urc_qmtsn_pub(const char * p_line);

static void parse_urc_qmtsn_willupd(const char * p_line);

static void parse_urc_qmtsn_sleep(const char * p_line);

static void parse_urc_qmtopen(const char * p_line);

static void parse_urc_qmtclose(const char * p_line);

static void parse_urc_qmtstat(const char * p_line);

static void parse_urc_qmtconn(const char * p_line);

static void parse_urc_qmtsub(const char * p_line);

static void parse_urc_qmtpub(const char * p_line);

static void parse_urc_recv(const char * p_line);

static void parse_urc_qmtdisc(const char * p_line);

static void parse_urc_qmtpuns(const char * p_line);

static void parse_urc_qmtping(const char * p_line);

static void parse_urc_qind_geofence(const char * p_line);

static void parse_urc_qscrub_end(const char * p_line);

static void parse_urc_psm_timer(const char * p_line);

static void parse_urc_qcellscan(const char * p_line);

static void parse_urc_crtdcp(const char * p_line);

static void parse_urc_qlwurc_event(const char * p_line);

static void parse_urc_qlwurc_observe(const char * p_line);

static void parse_urc_qlwurc_uldata(const char * p_line);

static void parse_urc_qlwurc_recv(const char * p_line);

static void parse_urc_qlwreg(const char * p_line);

static void parse_urc_qlwdereg(const char * p_line);

static void parse_urc_qind_open(const char * p_line);

static void parse_urc_xtra_dl(const char * p_line);

static void parse_urc_jamming_ind(const char * p_line);

static void parse_urc_qcoapopen(const char * p_line);

static void parse_urc_qcoapclose(const char * p_line);

static void parse_urc_qcoapack(const char * p_line);

static void parse_urc_qcoaprecv(const char * p_line);

static void parse_urc_qcoapind(const char * p_line);

static void parse_urc_qftpopen(const char * p_line);

static void parse_urc_qftpclose(const char * p_line);

static void parse_urc_qftpsize(const char * p_line);

static void parse_urc_qftpget(const char * p_line);

static void parse_urc_qftplen(const char * p_line);

static void parse_urc_qftpstat(const char * p_line);

static void parse_urc_qftpput(const char * p_line);

static void parse_urc_qftpdel(const char * p_line);

static void parse_urc_qftpcwd(const char * p_line);

static void parse_urc_qftpmkdir(const char * p_line);

static void parse_urc_qftprmdir(const char * p_line);

static void parse_urc_qftppwd(const char * p_line);

static void parse_urc_qftprename(const char * p_line);

static void parse_urc_qftpmdtm(const char * p_line);

static void parse_urc_qftpmlsd(const char * p_line);

static void parse_urc_qftplist(const char * p_line);

static void parse_urc_qftpnlst(const char * p_line);

static void parse_urc_default(const char * p_line);
static void parse_urc_kcnx_ind(const char * p_line);
static void parse_urc_ktcp_data(const char * p_line);
static void parse_urc_ktcp_notif(const char * p_line);
static void parse_urc_ktcp_ind(const char * p_line);
static void parse_urc_ktcp_srvreq(const char * p_line);
static void parse_urc_kudp_data(const char * p_line);
static void parse_urc_kudp_ind(const char * p_line);
static void parse_urc_kudp_notif(const char * p_line);

static urc_map_handler_t * p_urc_handlers_table              = NULL;

static const urc_map_t     urc_parser_table[URC_TYPE_NUMBER] = {
    [URC_TYPE_RDY]     = {.token = "RDY",                                             .parser = parse_urc_default         },
    [URC_TYPE_APP_RDY] = {.token = "APP RDY",                                         .parser = parse_urc_default         },
    [URC_TYPE_POWER_DOWN]
    = {.token = "POWERED DOWN",                                    .parser = parse_urc_default         },
    [URC_TYPE_NORMAL_POWER_DOWN]
    = {.token = "NORMAL POWER DOWN",                               .parser = parse_urc_default         },
    [URC_TYPE_PSM_POWER_DOWN]
    = {.token = "PSM POWER DOWN",                                  .parser = parse_urc_default         },
    [URC_TYPE_RING]   = {.token = "RING",                                            .parser = parse_urc_default         },
    [URC_TYPE_CFUN_1] = {.token = "+CFUN: 1",                                        .parser = parse_urc_default         },
    [URC_TYPE_QUSIM]  = {.token = "+QUSIM:",                                         .parser = parse_urc_qusim           },
    [URC_TYPE_CPIN]   = {.token = "+CPIN:",                                          .parser = parse_urc_cpin            },
    [URC_TYPE_SIMSTAT]
    = {.token = "+QSIMSTAT: 1,",                                   .parser = parse_urc_qsimstat        },
    [URC_TYPE_QIND_SMS_FULL]
    = {.token = "+QIND: \"smsfull\"",                              .parser = parse_urc_default         },
    [URC_TYPE_QIND_SMS_DONE]
    = {.token = "+QIND: SMS DONE",                                 .parser = parse_urc_default         },
    [URC_TYPE_CRING] = {.token = "+CRING:",                                         .parser = parse_urc_cring           },
    [URC_TYPE_QIND_CSQ]
    = {.token = "+QIND: \"csq\",",                                 .parser = parse_urc_qind_csq        },
    [URC_TYPE_QCSCON] = {.token = "+QCSCON:",                                        .parser = parse_urc_qcscon          },
    [URC_TYPE_CTZV]   = {.token = "+CTZV:",                                          .parser = parse_urc_ctzv            },
    [URC_TYPE_CTZE]   = {.token = "+CTZE:",                                          .parser = parse_urc_ctze            },
    [URC_TYPE_CMTI]   = {.token = "+CMTI:",                                          .parser = parse_urc_cmti            },
    [URC_TYPE_CMT]    = {.token = "+CMT:",                                           .parser = parse_urc_cmt_pdu         },
    [URC_TYPE_CBM]    = {.token = "+CBM:",                                           .parser = parse_urc_cbm_pdu         },
    [URC_TYPE_CDS]    = {.token = "+CDS:",                                           .parser = parse_urc_cds_pdu         },
    [URC_TYPE_CDSI]   = {.token = "+CDSI:",                                          .parser = parse_urc_cdsi            },
    [URC_TYPE_COLP]   = {.token = "+COLP:",                                          .parser = parse_urc_colp            },
    [URC_TYPE_CLIP]   = {.token = "+CLIP:",                                          .parser = parse_urc_clip            },
    [URC_TYPE_CCWA]   = {.token = "+CCWA:",                                          .parser = parse_urc_ccwa            },
    [URC_TYPE_QIND_AIRPLANESTATUS]
    = {.token = "+QIND: \"airplanestatus\",",                      .parser = parse_urc_default         },
    [URC_TYPE_CSSI]   = {.token = "+CSSI:",                                          .parser = parse_urc_cssi            },
    [URC_TYPE_CSSU]   = {.token = "+CSSU:",                                          .parser = parse_urc_cssu            },
    [URC_TYPE_CMGL]   = {.token = "+CMGL:",                                          .parser = parse_urc_default         },
    [URC_TYPE_QCMGR]  = {.token = "+QCMGR:",                                         .parser = parse_urc_default         },
    [URC_TYPE_CMGR]   = {.token = "+CMGR:",                                          .parser = parse_urc_default         },
    [URC_TYPE_CEDRXP] = {.token = "+CEDRXP:",                                        .parser = parse_urc_cedrxp          },
    [URC_TYPE_QPTWEDRXP]
    = {.token = "+QPTWEDRXP",                                      .parser = parse_urc_qptwedrxp       },
    [URC_TYPE_CGEV_REJECT]
    = {.token = "+CGEV: REJECT",                                   .parser = parse_urc_cgev_reject     },
    [URC_TYPE_CGEV_NW_REACT]
    = {.token = "+CGEV: NW REACT",                                 .parser = parse_urc_cgev_nw_react   },
    [URC_TYPE_CGEV_NW_DEACT]
    = {.token = "+CGEV: NW DEACT",                                 .parser = parse_urc_cgev_nw_deact   },
    [URC_TYPE_CGEV_ME_DEACT]
    = {.token = "+CGEV: ME DEACT",                                 .parser = parse_urc_cgev_me_deact   },
    [URC_TYPE_CGEV_NW_DETACH]
    = {.token = "+CGEV: NW DETACH",                                .parser = parse_urc_default         },
    [URC_TYPE_CGEV_ME_DETACH]
    = {.token = "+CGEV: ME DETACH",                                .parser = parse_urc_default         },
    [URC_TYPE_CGEV_NW_CLASS]
    = {.token = "+CGEV: NW CLASS",                                 .parser = parse_urc_cgev_nw_class   },
    [URC_TYPE_CGEV_ME_CLASS]
    = {.token = "+CGEV: ME CLASS",                                 .parser = parse_urc_cgev_me_class   },
    [URC_TYPE_CGEV_PDP_ACT]
    = {.token = "+CGEV: PDP ACT",                                  .parser = parse_urc_cgev_pdp_act    },
    [URC_TYPE_CGEV_PDP_DEACT]
    = {.token = "+CGEV: PDP DEACT",                                .parser = parse_urc_cgev_pdp_deact  },

    [URC_TYPE_CGEV_DED_NW_DED_ACT]
    = {.token = "+CGEV: DED NW DED ACT",                           .parser = parse_urc_default         },
    [URC_TYPE_CGEV_ME_ACT]
    = {.token = "+CGEV: ME ACT",                                   .parser = parse_urc_default         },
    [URC_TYPE_CGEV_DED_NW_DEACT]
    = {.token = "+CGEV: DED NW DEACT",                             .parser = parse_urc_default         },
    [URC_TYPE_CGEV_NW_MODIFY]
    = {.token = "+CGEV: NW MODIFY",                                .parser = parse_urc_default         },
    [URC_TYPE_CGEV_ME_MODIFY]
    = {.token = "+CGEV: ME MODIFY",                                .parser = parse_urc_default         },

    [URC_TYPE_CREG]   = {.token = "+CREG:",                                          .parser = parse_urc_creg            },
    [URC_TYPE_CGREG]  = {.token = "+CGREG:",                                         .parser = parse_urc_cgreg           },
    [URC_TYPE_CEREG]  = {.token = "+CEREG:",                                         .parser = parse_urc_cereg           },
    [URC_TYPE_QIOPEN] = {.token = "+QIOPEN",                                         .parser = parse_urc_qiopen          },
    [URC_TYPE_QIURC_CLOSED]
    = {.token = "+QIURC: \"closed\",",                             .parser = parse_urc_qiurc_closed    },
    [URC_TYPE_QIURC_RECV] = {.token  = "+QIURC: \"recv\",",
                          .parser = parse_urc_qiurc_recv_buffer_mode                                                      },
    [URC_TYPE_QIURC_INCOMING_FULL]
    = {.token = "+QIURC: \"incoming full\"",                       .parser = parse_urc_default         },
    [URC_TYPE_QIURC_INCOMING]
    = {.token = "+QIURC: \"incoming\",",                           .parser = parse_urc_qiurc_incoming  },
    [URC_TYPE_QIURC_PDPDEACT]
    = {.token = "+QIURC: \"pdpdeact\",",                           .parser = parse_urc_qiurc_pdp_deact },
    [URC_TYPE_QIURC_DNSGIP]
    = {.token = "+QIURC: \"dnsgip\",",                             .parser = parse_urc_qiurc_dnsgip    },
    [URC_TYPE_QNTP]  = {.token = "+QNTP:",                                          .parser = parse_urc_qntp            },
    [URC_TYPE_QPING] = {.token = "+QPING:",                                         .parser = parse_urc_qping           },
    [URC_TYPE_QSSLOPEN]
    = {.token = "+QSSLOPEN:",                                      .parser = parse_urc_qssl_open       },
    [URC_TYPE_QSSLURC_RECV]
    = {.token = "+QSSLURC: \"recv\",",                             .parser = parse_urc_qssl_buffer_mode},
    [URC_TYPE_QSSLURC_CLOSED]
    = {.token = "+QSSLURC: \"closed\",",                           .parser = parse_urc_qssl_closed     },
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_START]
    = {.token  = "+LWM2M: \"FOTA\",\"DOWNLOADING\",\"START\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_QIND_FOTA_HTTPSTART] = {.token = "+QIND: \"FOTA\",\"HTTPSTART\"",
                          .parser = parse_urc_fota_httpstart                                                              },
    [URC_TYPE_QIND_FOTA_HTTPEND] = {.token  = "+QIND: \"FOTA\",\"HTTPEND\"",
                          .parser = parse_urc_fota_httpend                                                                },
    [URC_TYPE_QIND_FOTA_COAPSTART] = {.token = "+QIND: \"FOTA\",\"COAPSTART\"",
                          .parser = parse_urc_fota_coapstart                                                              },
    [URC_TYPE_QIND_FOTA_COAPEND] = {.token  = "+QIND: \"FOTA\",\"COAPEND\"",
                          .parser = parse_urc_fota_coapend                                                                },
    [URC_TYPE_QIND_FOTA_DOWNLOADING]
    = {.token  = "+QIND: \"FOTA\",\"DOWNLOADING\",",
                          .parser = parse_urc_fota_downloading                                                            },
    [URC_TYPE_LWM2M_FOTA_DOWNLOADING_COMPLETED]
    = {.token  = "+LWM2M: \"FOTA\",\"DOWNLOADING\",\"COMPLETED\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_LWM2M_FOTA_DOWNLOADED]
    = {.token  = "+LWM2M: \"FOTA\",\"DOWNLOADED\"",
                          .parser = parse_urc_fota_downloaded                                                             },

    [URC_TYPE_QIND_FOTA_FTPSTART] = {.token = "+QIND: \"FOTA\",\"FTPSTART\"",
                          .parser = parse_urc_fota_ftpstart                                                               },

    [URC_TYPE_QIND_FOTA_START_UPDATING]
    = {.token  = "+QIND: \"FOTA\",\"START\"",
                          .parser = parse_urc_fota_start_updating                                                         },
    [URC_TYPE_QIND_FOTA_UPDATING] = {.token = "+QIND: \"FOTA\",\"UPDATING\",",
                          .parser = parse_urc_fota_updating                                                               },
    [URC_TYPE_QIND_FOTA_END]
    = {.token = "+QIND: \"FOTA\",\"END\",",                        .parser = parse_urc_fota_end        },

    [URC_TYPE_QIND_FOTA_FTPEND]
    = {.token = "+QIND: \"FOTA\",\"FTPEND\"",                      .parser = parse_urc_fota_ftpend     },

    [URC_TYPE_QHTTPGET]
    = {.token = "+QHTTPGET:",                                      .parser = parse_urc_qhttp_get       },
    [URC_TYPE_QHTTPPOST]
    = {.token = "+QHTTPPOST:",                                     .parser = parse_urc_qhttp_post      },
    [URC_TYPE_QHTTPPOSTFILE]
    = {.token = "+QHTTPPOSTFILE:",                                 .parser = parse_urc_qhttp_postfile  },
    [URC_TYPE_QHTTPPUT]
    = {.token = "+QHTTPPUT:",                                      .parser = parse_urc_qhttp_put       },
    [URC_TYPE_QHTTPPUTFILE]
    = {.token = "+QHTTPPUTFILE:",                                  .parser = parse_urc_qhttp_putfile   },
    [URC_TYPE_QHTTPREAD]
    = {.token = "+QHTTPREAD:",                                     .parser = parse_urc_qhttp_read      },
    [URC_TYPE_QHTTPREADFILE]
    = {.token = "+QHTTPREADFILE:",                                 .parser = parse_urc_qhttp_readfile  },

    [URC_TYPE_QMTSNSTAT]
    = {.token = "+QMTSNSTAT:",                                     .parser = parse_urc_qmtsn_stat      },
    [URC_TYPE_QMTSNRECV]
    = {.token = "+QMTSNRECV:",                                     .parser = parse_urc_qmtsn_recv      },
    [URC_TYPE_QMTSNSUB]
    = {.token = "+QMTSNSUB:",                                      .parser = parse_urc_qmtsn_sub       },
    [URC_TYPE_QMTSNUNS]
    = {.token = "+QMTSNUNS:",                                      .parser = parse_urc_qmtsn_uns       },
    [URC_TYPE_QMTSNREG]
    = {.token = "+QMTSNREG:",                                      .parser = parse_urc_qmtsn_reg       },
    [URC_TYPE_QMTSNPUB]
    = {.token = "+QMTSNPUB:",                                      .parser = parse_urc_qmtsn_pub       },
    [URC_TYPE_QMTSN_WILLUPD]
    = {.token = "+QMTSNWILLUPD:",                                  .parser = parse_urc_qmtsn_willupd   },
    [URC_TYPE_QMTSNSLEEP]
    = {.token = "+QMTSNSLEEP:",                                    .parser = parse_urc_qmtsn_sleep     },
    [URC_TYPE_QMTOPEN] = {.token = "+QMTOPEN:",                                       .parser = parse_urc_qmtopen         },
    [URC_TYPE_QMTCLOSE] = {.token = "+QMTCLOSE:",                                      .parser = parse_urc_qmtclose        },
    [URC_TYPE_QMTSTAT] = {.token = "+QMTSTAT:",                                       .parser = parse_urc_qmtstat         },
    [URC_TYPE_QMTCONN] = {.token = "+QMTCONN:",                                       .parser = parse_urc_qmtconn         },
    [URC_TYPE_QMTSUB]  = {.token = "+QMTSUB:",                                        .parser = parse_urc_qmtsub          },
    [URC_TYPE_QMTPUB]  = {.token = "+QMTPUB:",                                        .parser = parse_urc_qmtpub          },
    [URC_TYPE_QMTRECV] = {.token = "+QMTRECV:",                                       .parser = parse_urc_recv            },
    [URC_TYPE_QMTDISC] = {.token = "+QMTDISC:",                                       .parser = parse_urc_qmtdisc         },
    [URC_TYPE_QMTUNS] = {.token = "+QMTUNS:",                                        .parser = parse_urc_qmtpuns         },
    [URC_TYPE_QMTPING] = {.token = "+QMTPING:",                                       .parser = parse_urc_qmtping         },

    [URC_TYPE_QJDR_JAMMED]
    = {.token = "+QJDR: JAMMED",                                   .parser = parse_urc_default         },
    [URC_TYPE_QJDR_NOJAMMING]
    = {.token = "+QJDR: NOJAMMING",                                .parser = parse_urc_default         },
    [URC_TYPE_QLWURC_EVENT]
    = {.token = "+QLWURC: \"event\",",                             .parser = parse_urc_qlwurc_event    },
    [URC_TYPE_QLWURC_OBSERVE]
    = {.token = "+QLWURC: \"observe\",",                           .parser = parse_urc_qlwurc_observe  },
    [URC_TYPE_QLWURC_ULDATA]
    = {.token = "+QLWURC: \"uldata\",",                            .parser = parse_urc_qlwurc_uldata   },
    [URC_TYPE_QLWURC_RECV]
    = {.token = "+QLWURC: \"recv\",",                              .parser = parse_urc_qlwurc_recv     },
    [URC_TYPE_QLWREG] = {.token = "+QLWREG:",                                        .parser = parse_urc_qlwreg          },
    [URC_TYPE_QLWDEREG] = {.token = "+QLWDEREG:",                                      .parser = parse_urc_qlwdereg        },

    [URC_TYPE_QIND_NIPD_OPEN]
    = {.token = "+QIND: \"nipd\",\"open\"",                        .parser = parse_urc_qind_open       },
    [URC_TYPE_QIND_NIPD_RECV]
    = {.token = "+QIND: \"nipd\",\"recv\"",                        .parser = parse_urc_default         },
    [URC_TYPE_QIND_NIPD_CLOSE]
    = {.token = "+QIND: \"nipd\",\"close\"",                       .parser = parse_urc_default         },

    [URC_TYPE_QIND_GEOFENCE]
    = {.token = "+QIND: \"GEOFENCE\",",                            .parser = parse_urc_qind_geofence   },

    [URC_TYPE_QSCRUB_START]
    = {.token = "+QSCRUB: \"START\"",                              .parser = parse_urc_default         },
    [URC_TYPE_QSCRUB_END]
    = {.token = "+QSCRUB: \"END\"",                                .parser = parse_urc_qscrub_end      },

    [URC_TYPE_QCOAPOPEN]
    = {.token = "+QCOAPOPEN:",                                     .parser = parse_urc_qcoapopen       },
    [URC_TYPE_QCOAPCLOSE]
    = {.token = "+QCOAPCLOSE",                                     .parser = parse_urc_qcoapclose      },
    [URC_TYPE_QCOAPACK] = {.token = "+QCOAPACK",                                       .parser = parse_urc_qcoapack        },
    [URC_TYPE_QCOAPRECV]
    = {.token = "+QCOAPRECV",                                      .parser = parse_urc_qcoaprecv       },
    [URC_TYPE_QCOAPIND] = {.token = "+QCOAPIND",                                       .parser = parse_urc_qcoapind        },

    [URC_TYPE_QFTPOPEN] = {.token = "+QFTPOPEN",                                       .parser = parse_urc_qftpopen        },
    [URC_TYPE_QFTPCLOSE]
    = {.token = "+QFTPCLOSE",                                      .parser = parse_urc_qftpclose       },
    [URC_TYPE_QFTPSIZE] = {.token = "+QFTPSIZE",                                       .parser = parse_urc_qftpsize        },
    [URC_TYPE_QFTPGET] = {.token = "+QFTPGET",                                        .parser = parse_urc_qftpget         },
    [URC_TYPE_QFTPPUT] = {.token = "+QFTPPUT",                                        .parser = parse_urc_qftpput         },
    [URC_TYPE_QFTPDEL] = {.token = "+QFTPDEL",                                        .parser = parse_urc_qftpdel         },
    [URC_TYPE_QFTPCWD] = {.token = "+QFTPCWD",                                        .parser = parse_urc_qftpcwd         },
    [URC_TYPE_QFTPMKDIR]
    = {.token = "+QFTPMKDIR",                                      .parser = parse_urc_qftpmkdir       },
    [URC_TYPE_QFTPRMDIR]
    = {.token = "+QFTPRMDIR",                                      .parser = parse_urc_qftprmdir       },
    [URC_TYPE_QFTPPWD] = {.token = "+QFTPPWD",                                        .parser = parse_urc_qftppwd         },
    [URC_TYPE_QFTPRENAME]
    = {.token = "+QFTPRENAME",                                     .parser = parse_urc_qftprename      },
    [URC_TYPE_QFTPMDTM] = {.token = "+QFTPMDTM",                                       .parser = parse_urc_qftpmdtm        },
    [URC_TYPE_QFTPLIST] = {.token = "+QFTPLIST",                                       .parser = parse_urc_qftplist        },
    [URC_TYPE_QFTPNLST] = {.token = "+QFTPNLST",                                       .parser = parse_urc_qftpnlst        },
    [URC_TYPE_QFTPMLSD] = {.token = "+QFTPMLSD",                                       .parser = parse_urc_qftpmlsd        },
    [URC_TYPE_QFTPSTAT] = {.token = "+QFTPSTAT",                                       .parser = parse_urc_qftpstat        },
    [URC_TYPE_QFTPLEN] = {.token = "+QFTPLEN",                                        .parser = parse_urc_qftplen         },

    [URC_TYPE_QCAUSEINFO]
    = {.token = "+QCAUSEINFO:",                                    .parser = parse_urc_default         },

    [URC_TYPE_QIOTEVT_REG]
    = {.token = "+QIOTEVT: reg",                                   .parser = parse_urc_default         },
    [URC_TYPE_QIOTEVT_EXIT]
    = {.token = "+QIOTEVT: exit",                                  .parser = parse_urc_default         },
    [URC_TYPE_QIOTEVT_SUB]
    = {.token = "+QIOTEVT: sub",                                   .parser = parse_urc_default         },
    [URC_TYPE_QIOTEVT_RECV]
    = {.token = "+QIOTEVT: recv",                                  .parser = parse_urc_default         },
    [URC_TYPE_QIOTEVT_FOTA]
    = {.token = "+QIOTEVT: fota",                                  .parser = parse_urc_default         },
    [URC_TYPE_QIOTEVT_SEND]
    = {.token = "+QIOTEVT: send",                                  .parser = parse_urc_default         },
    [URC_TYPE_QIOTPING] = {.token = "+QIOTPING:",                                      .parser = parse_urc_default         },

    [URC_TYPE_QPSMTIMER]
    = {.token = "+QPSMTIMER",                                      .parser = parse_urc_psm_timer       },

    [URC_TYPE_QCELLSCAN]
    = {.token = "+QCELLSCAN:",                                     .parser = parse_urc_qcellscan       },
    [URC_TYPE_CRTDCP] = {.token = "+CRTDCP:",                                        .parser = parse_urc_crtdcp          },
    [URC_TYPE_QGPSURC_XTRA]
    = {.token = "+QGPSURC: \"XTRA_DL\",",                          .parser = parse_urc_xtra_dl         },
    [URC_TYPE_QGPSURC_JAMMING_IND]
    = {.token = "+QGPSURC: \"jamming_ind\",",                      .parser = parse_urc_jamming_ind     },
    [URC_TYPE_QGPSURC_SINGLE_SHOT]
    = {.token = "+QGPSURC: \"single_shot\"",                       .parser = parse_urc_default         },

    [URC_TYPE_UNDER_VOLTAGE_WARNING]
    = {.token  = "+QIND: \"UNDER VOLTAGE WARNING\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_UNDER_VOLTAGE_POWER_DOWN]
    = {.token  = "+QIND: \"UNDER VOLTAGE POWER DOWN\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_OVER_VOLTAGE_WARNING]
    = {.token = "+QIND: \"OVER VOLTAGE WARNING\"",                 .parser = parse_urc_default         },
    [URC_TYPE_OVER_VOLTAGE_POWER_DOWN]
    = {.token  = "+QIND: \"OVER VOLTAGE POWER DOWN\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_VOLTAGE_POWER_NORMAL]
    = {.token = "+QIND: \"VOLTAGE POWER NORMAL\"",                 .parser = parse_urc_default         },
    [URC_TYPE_OVER_PMIC_TEMPERATURE_WARNING]
    = {.token  = "+QIND: \"OVER PMIC TEMPERATURE WARNING\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_OVER_OX_TEMPERATURE_WARNING]
    = {.token  = "+QIND: \"OVER OX TEMPERATURE WARNING\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_OVER_PA_TEMPERATURE_WARNING]
    = {.token  = "+QIND: \"OVER PA TEMPERATURE WARNING\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_NORMAL_PMIC_TEMPERATURE]
    = {.token  = "+QIND: \"NORMAL PMIC TEMPERATURE\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_NORMAL_OX_TEMPERATURE]
    = {.token  = "+QIND: \"NORMAL OX TEMPERATURE\"",
                          .parser = parse_urc_default                                                                     },
    [URC_TYPE_NORMAL_PA_TEMPERATURE]
    = {.token  = "+QIND: \"NORMAL PA TEMPERATURE\"",
                          .parser = parse_urc_default                                                                     },

    [URC_TYPE_QESIM_OTA]
    = {.token = "+QESIM: \"ota\",",                                .parser = parse_urc_default         },
    [URC_TYPE_QESIM_TRANS]
    = {.token = "+QESIM: \"trans\",",                              .parser = parse_urc_default         },
    [URC_TYPE_QESIM_DOWNLOAD]
    = {.token = "+QESIM: \"download\",",                           .parser = parse_urc_default         },
    [URC_TYPE_KCNX_IND] = {.token = "+KCNX_IND:",                                      .parser = parse_urc_kcnx_ind        },
    [URC_TYPE_KTCP_DATA]
    = {.token = "+KTCP_DATA:",                                     .parser = parse_urc_ktcp_data       },
    [URC_TYPE_KTCP_NOTIF]
    = {.token = "+KTCP_NOTIF:",                                    .parser = parse_urc_ktcp_notif      },
    [URC_TYPE_KTCP_IND] = {.token = "+KTCP_IND:",                                      .parser = parse_urc_ktcp_ind        },
    [URC_TYPE_KTCP_SRVREQ]
    = {.token = "+KTCP_SRVREQ:",                                   .parser = parse_urc_ktcp_srvreq     },
    [URC_TYPE_KUDP_DATA]
    = {.token = "+KUDP_DATA:",                                     .parser = parse_urc_kudp_data       },
    [URC_TYPE_KUDP_NOTIF]
    = {.token = "+KUDP_NOTIF:",                                    .parser = parse_urc_kudp_notif      },
    [URC_TYPE_KUDP_IND] = {.token = "+KUDP_IND:",                                      .parser = parse_urc_kudp_ind        },
};

void
urc_set_handler_table (urc_map_handler_t * p_handlers)
{
    p_urc_handlers_table = p_handlers;
}

void
urc_parse_unsolicited_response (const char * p_line)
{
    urc_type_t urc_index = URC_TYPE_INVALID;

    urc_index            = get_urc_index(p_line);

    if ((p_urc_handlers_table != NULL) && (urc_index != URC_TYPE_INVALID)
        && (urc_parser_table[urc_index].parser != NULL))
    {
        cell_log_debug("parsing urc = %d", urc_index);
        urc_parser_table[urc_index].parser(p_line);
    }
}

bool
urc_is_without_prefix (const char * p_line)
{
    /* Normaly, the prefix of an URC will start with '+' character */
    if (p_line[0] != '+')
    {
        uint16_t incoming_urc_len = strlen(p_line);
        for (int i = 0; i < URC_TYPE_NUMBER; i++)
        {
            if (urc_parser_table[i].token != NULL
                && urc_parser_table[i].token[0] != '+')
            {
                uint16_t token_len = strlen(urc_parser_table[i].token);
                if (token_len > 0 && token_len <= incoming_urc_len
                    && memcmp(p_line, urc_parser_table[i].token, token_len)
                           == 0)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static void
set_bit (uint8_t * x, uint8_t pos)
{
    *x |= (uint8_t) (1 << pos);
}

static void
clear_bit (uint8_t * x, uint8_t pos)
{
    *x &= (uint8_t) ~(1 << (pos));
}

static int
convert_ip_str (const char * ip_str, urc_ip_addr_union_t * ip_data,
                bool is_ipv4)
{
    int parsed = 0, ret = 0;
    if (is_ipv4)
    {
        // Assumes format: "x.x.x.x"
        parsed = sscanf(ip_str,
                        "%3hhu.%3hhu.%3hhu.%3hhu",
                        &ip_data->ipv4.addr[0],
                        &ip_data->ipv4.addr[1],
                        &ip_data->ipv4.addr[2],
                        &ip_data->ipv4.addr[3]);
        if (parsed != 4)
        {
            ret = 1;
        }
    }
    else
    {
        // Assumes format: "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx"
        parsed = sscanf(ip_str,
                        "%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx:%4hx",
                        &ip_data->ipv6.addr[0],
                        &ip_data->ipv6.addr[1],
                        &ip_data->ipv6.addr[2],
                        &ip_data->ipv6.addr[3],
                        &ip_data->ipv6.addr[4],
                        &ip_data->ipv6.addr[5],
                        &ip_data->ipv6.addr[6],
                        &ip_data->ipv6.addr[7]);
        if (parsed != 8)
        {
            ret = 1;
        }
    }
    return ret;
}

static urc_cell_access_technology_t
get_rat_type (const char * rat_type_str)
{
    urc_cell_access_technology_t rat_type = URC_ACT_GSM;

    for (size_t i = 0; i < sizeof(rat_map) / sizeof(rat_map[0]); i++)
    {
        if (strcmp(rat_type_str, rat_map[i].rat_str) == 0)
        {
            rat_type = rat_map[i].rat_type;
            break;
        }
    }

    return rat_type;
}

static urc_parser_status_t
bin_string_to_uint4 (const char * str, uint8_t * u4)
{
    *u4 = 0;
    for (int i = 0; i < 4; i++)
    {
        if (*(str + i) == '1')
        {
            set_bit(u4, 3 - i);
        }
        else if (*(str + i) == '0')
        {
            clear_bit(u4, 3 - i);
        }
        else
        {
            return PARSER_FAILURE;
        }
    }
    return PARSER_SUCCESS;
}

static urc_type_t
get_urc_index (const char * urc_line)
{
    urc_type_t urc_index = URC_TYPE_INVALID;
    uint16_t   urc_len   = strlen(urc_line);
    for (int id = 0; id < URC_TYPE_NUMBER; id++)
    {
        if (urc_parser_table[id].token == NULL
            || strlen(urc_parser_table[id].token) > urc_len)
        {
            continue;
        }
        else if (memcmp(urc_parser_table[id].token,
                        urc_line,
                        strlen(urc_parser_table[id].token))
                 == 0)
        {
            urc_index = (urc_type_t) id;
            break;
        }
    }
    return urc_index;
}

static void
parse_urc_qusim (const char * p_line)
{
    urc_qusim_state_t data;
    memset(&data, 0, sizeof(urc_qusim_state_t));

    int parsed = sscanf(p_line, "+QUSIM: %2hhu", (uint8_t *) &data);
    if (parsed == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QUSIM].handler)
        {
            p_urc_handlers_table[URC_TYPE_QUSIM].handler(&data);
        }
    }
}

static void
parse_urc_cpin (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_cpin_type_t     data;
    memset(&data, 0, sizeof(data));
    if (strstr(p_line, "NOT READY") != NULL)
    {
        data = URC_CPIN_SIM_NOT_READY;
    }
    else if (strstr(p_line, "NOT INSERTED") != NULL)
    {
        data = URC_CPIN_SIM_NOT_INSERTED;
    }
    else if (strstr(p_line, "READY") != NULL)
    {
        data = URC_CPIN_SIM_READY;
    }
    else if (strstr(p_line, "SIM PIN") != NULL)
    {
        data = URC_CPIN_SIM_PIN;
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if (p_urc_handlers_table[URC_TYPE_CPIN].handler && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CPIN].handler(&data);
    }
}

static void
parse_urc_qsimstat (const char * p_line)
{
    urc_qsimstat_type_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QSIMSTAT: 1,%u", (uint32_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_SIMSTAT].handler)
        {
            p_urc_handlers_table[URC_TYPE_SIMSTAT].handler(&data);
        }
    }
}

static void
parse_urc_qind_csq (const char * p_line)
{
    urc_qind_csq_type_t data;
    memset(&data, 0, sizeof(urc_qind_csq_type_t));
    int parsed
        = sscanf(p_line, "+QIND: \"csq\",%2hhu,%2hhu", &data.rssi, &data.ber);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_CSQ].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_CSQ].handler(&data);
        }
    }
}

static void
parse_urc_cring (const char * p_line)
{
    urc_cring_type_t data;
    uint16_t         len = strlen(p_line);
    memset(&data, 0, sizeof(data));

    typedef struct
    {
        const char *     pattern;
        uint16_t         len;
        urc_cring_type_t type;
    } urc_cring_mapping_t;

    urc_cring_mapping_t mappings[] = {
        {"ASYNC",     13, URC_CRING_ASYNC   },
        {"SYNC",      12, URC_CRING_SYNC    },
        {"REL ASYNC", 17, URC_CRING_RELASYNC},
        {"REL SYNC",  16, URC_CRING_RESYNC  },
        {"FAX",       11, URC_CRING_FAX     },
        {"VOICE",     13, URC_CRING_VOICE   }
    };

    size_t num_mappings = sizeof(mappings) / sizeof(mappings[0]);
    for (size_t i = 0; i < num_mappings; ++i)
    {
        if (len == mappings[i].len
            && memcmp(
                   mappings[i].pattern, &p_line[8], strlen(mappings[i].pattern))
                   == 0)
        {
            data = mappings[i].type;

            if (p_urc_handlers_table[URC_TYPE_CRING].handler)
            {
                p_urc_handlers_table[URC_TYPE_CRING].handler(&data);
            }
            break;
        }
    }
}

static void
parse_urc_ctzv (const char * p_line)
{
    urc_ctzv_type_t data;
    memset(&data, 0, sizeof(data));
    uint32_t temp_tz = 0;
    if (strlen(p_line) >= strlen("+CTZV: \"xxx\"")
        && sscanf(&p_line[9], "%2u", &temp_tz) == 1)
    {
        data.timezone = temp_tz;
        /* The format of timezone is "±zz", expressed as a fixed width,
        two-digit integer within the range -48 to +56.*/
        if (p_line[8] == '-' && temp_tz <= 48)
        {
            data.timezone = 0 - temp_tz;
        }

        if (p_urc_handlers_table[URC_TYPE_CTZV].handler)
        {
            p_urc_handlers_table[URC_TYPE_CTZV].handler(&data);
        }
    }
}

static void
parse_urc_ctze (const char * p_line)
{
    urc_ctze_type_t data;
    uint32_t        temp_tz = 0;
    memset(&data, 0, sizeof(data));

    if (strlen(p_line) >= strlen("+CTZE: \"xxx\",x,\"xx/xx/xx,xx:xx:xx\"")
        && sscanf(&p_line[9],
                  "%2u\",%u,\"%u/%u/%u,%u:%u:%u\"",
                  &temp_tz,
                  &data.daily_saving_adjustment,
                  &data.time.year,
                  &data.time.mon,
                  &data.time.date,
                  &data.time.hour,
                  &data.time.min,
                  &data.time.sec)
               == 8)
    {
        data.timezone = temp_tz;
        /* The format of timezone is "±zz", expressed as a fixed width,
        two-digit integer within the range -48 to +56.*/
        if (p_line[8] == '-' && temp_tz <= 48)
        {
            data.timezone = 0 - temp_tz;
        }

        if (p_urc_handlers_table[URC_TYPE_CTZE].handler)
        {
            p_urc_handlers_table[URC_TYPE_CTZE].handler(&data);
        }
    }
}

static urc_parser_status_t
get_urc_mem_type (const char * buffer, urc_memory_storage_t * mem)
{
    urc_parser_status_t ret = PARSER_FAILURE;

    typedef struct
    {
        const char *         pattern;
        urc_memory_storage_t type;
    } urc_mem_mapping_t;

    urc_mem_mapping_t mappings[] = {
        {"SM", CELL_STORAGE_SIM_MSG      },
        {"ME", CELL_STORAGE_MOBILE_EQ    },
        {"MT", CELL_STORAGE_MOBILE_EQ    },
        {"SR", CELL_STORAGE_STATUS_REPORT},
    };

    size_t num_mappings = sizeof(mappings) / sizeof(mappings[0]);
    for (size_t i = 0; i < num_mappings; ++i)
    {
        if (memcmp(mappings[i].pattern, buffer, 2) == 0)
        {
            *mem = mappings[i].type;
            ret  = PARSER_SUCCESS;
            break;
        }
    }
    return ret;
}

static void
parse_urc_cmti (const char * p_line)
{
    urc_cmti_type_t data;
    uint16_t        len = strlen(p_line);

    memset(&data, 0, sizeof(data));
    if (len >= strlen("+CMTI: \"XX\",x"))
    {
        if (get_urc_mem_type(&p_line[8], &data.mem) == PARSER_SUCCESS
            && sscanf(&p_line[12], "%u", &data.index) == 1)
        {
            if (p_urc_handlers_table[URC_TYPE_CMTI].handler)
            {
                p_urc_handlers_table[URC_TYPE_CMTI].handler(&data);
            }
        }
    }
}

/*
+CMT: [<alpha>],<length><CR><LF><pdu>
+CMT: <oa>,[<alpha>],<scts>[,<tooa>,<fo>,<pid>,<dcs>,
    <sca>,<tosca>, <length>]<CR><LF><data>
*/
static void
parse_urc_cmt_pdu (const char * p_line)
{
}

static void
parse_urc_cbm_pdu (const char * p_line)
{
}

static void
parse_urc_cds_pdu (const char * p_line)
{
}

static void
parse_urc_cdsi (const char * p_line)
{
    urc_cdsi_type_t data;
    memset(&data, 0, sizeof(data));
    if (strlen(p_line) >= strlen("+CDSI: \"XX\",x"))
    {
        if (get_urc_mem_type(&p_line[8], &data.mem) == PARSER_SUCCESS
            && sscanf(&p_line[12], "%u", &data.index) == 1)
        {
            if (p_urc_handlers_table[URC_TYPE_CDSI].handler)
            {
                p_urc_handlers_table[URC_TYPE_CDSI].handler(&data);
            }
        }
    }
}

static void
parse_urc_colp (const char * p_line)
{
}

static void
parse_urc_clip (const char * p_line)
{
}

static void
parse_urc_ccwa (const char * p_line)
{
}

static void
parse_urc_cssi (const char * p_line)
{
    urc_cssi_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+CSSI: %u", (uint32_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_CSSI].handler)
        {
            p_urc_handlers_table[URC_TYPE_CSSI].handler(&data);
        }
    }
}

static void
parse_urc_cssu (const char * p_line)
{
    urc_cssu_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+CSSU: %u", (uint32_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_CSSU].handler)
        {
            p_urc_handlers_table[URC_TYPE_CSSU].handler(&data);
        }
    }
}

static void
parse_urc_cedrxp (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_cedrxp_type_t   data;
    uint8_t             len = strlen(p_line);
    memset(&data, 0, sizeof(data));
    if (len >= 10) /* 10 = strlen("+CEDRXP: x") */
    {
        data.act_type = p_line[9] - '0';
        if (len >= 17) /* 17 = strlen("+CEDRXP: x,"xxxx"") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[12], &temp);
            data.requested_eDRX_value = (uint32_t) temp;
        }

        if (len >= 24) /* 24 = strlen("+CEDRXP: x,"xxxx","xxxx"") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[19], &temp);
            data.nw_provided_eDRX_value = (uint32_t) temp;
        }

        if (len >= 31) /* 31 = strlen("+CEDRXP:
                                            x,"xxxx","xxxx","xxxx"") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[26], &temp);
            data.paging_time_window = (uint32_t) temp;
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if (p_urc_handlers_table[URC_TYPE_CEDRXP].handler && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CEDRXP].handler(&data);
    }
}

static void
parse_urc_qptwedrxp (const char * p_line)
{
    urc_parser_status_t  ret = PARSER_SUCCESS;
    urc_qptwedrxp_type_t data;
    uint8_t              len = strlen(p_line);
    memset(&data, 0, sizeof(data));
    if (len >= 13) /* 13 = strlen("+QPTWEDRXP: x") */
    {
        data.act_type = p_line[12] - '0';
        if (len >= 20) /* 20 = strlen("+QPTWEDRXP: x") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[15], &temp);
            data.requested_paging_time_window = (uint32_t) temp;
        }

        if (len >= 27) /* 27 = strlen("+QPTWEDRXP: x") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[22], &temp);
            data.requested_eDRX_value = (uint32_t) temp;
        }

        if (len >= 34) /* 34 = strlen("+QPTWEDRXP: x") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[29], &temp);
            data.nw_provided_eDRX_value = (uint32_t) temp;
        }

        if (len >= 41) /* 13 = strlen("+QPTWEDRXP: x") */
        {
            uint8_t temp = 0;
            ret |= bin_string_to_uint4(&p_line[36], &temp);
            data.paging_time_window = (uint32_t) temp;
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if (p_urc_handlers_table[URC_TYPE_QPTWEDRXP].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_QPTWEDRXP].handler(&data);
    }
}

static void
parse_urc_creg (const char * p_line)
{
    urc_creg_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+CREG: %u,\"%x\",\"%x\",%u",
               (uint32_t *) &data.status,
               &data.location_area_code,
               &data.cell_id,
               (uint32_t *) &data.access_technology)
        >= 1) /* The information after <status> might not appear*/
    {
        if (p_urc_handlers_table[URC_TYPE_CREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_CREG].handler(&data);
        }
    }
}

static void
parse_urc_cgreg (const char * p_line)
{
    urc_cgreg_type_t data;
    memset(&data, 0, sizeof(data));
    /* get <stat>[,<lac>,<ci>[,<AcT>]] */
    if (sscanf(&p_line[8],
               "%u,\"%x\",\"%x\",%u",
               (uint32_t *) &data.status,
               &data.location_area_code,
               &data.cell_id,
               (uint32_t *) &data.access_technology)
        >= 1) /* The information after <status> might not appear*/
    {
        if (p_urc_handlers_table[URC_TYPE_CGREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_CGREG].handler(&data);
        }
    }
}

static void
parse_urc_cereg (const char * p_line)
{
    urc_cereg_type_t data;
    memset(&data, 0, sizeof(data));
    /* get <stat>[,<tac>,<ci>[,<AcT>]] */
    if (sscanf(&p_line[8],
               "%u,\"%x\",\"%x\",%u",
               (uint32_t *) &data.status,
               &data.tracking_area_code,
               &data.cell_id,
               (uint32_t *) &data.access_technology)
        >= 1) /* The information after <status> might not appear*/
    {
        if (p_urc_handlers_table[URC_TYPE_CEREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_CEREG].handler(&data);
        }
    }
}

static void
parse_urc_qiopen (const char * p_line)
{
    urc_qiopen_type_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QIOPEN: %u,%hu", &data.connect_id, &data.result) == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QIOPEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIOPEN].handler(&data);
        }
    }
}

static void
parse_urc_qiurc_closed (const char * p_line)
{
    urc_qiurc_closed_type_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QIURC: \"closed\",%u", &data.connect_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIURC_CLOSED].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIURC_CLOSED].handler(&data);
        }
    }
}

static void
parse_urc_qiurc_recv_buffer_mode (const char * p_line)
{
    urc_qiurc_recv_buffer_mode_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QIURC: \"recv\",%u,%u",
               &data.connect_id,
               &data.data_length)
        >= 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIURC_RECV].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIURC_RECV].handler(&data);
        }
    }
}

static void
parse_urc_qiurc_incoming (const char * p_line)
{
    urc_parser_status_t       ret = PARSER_SUCCESS;
    urc_qiurc_incoming_type_t data;
    memset(&data, 0, sizeof(data));
    char ip_str[40] = {0};
    int  parsed     = sscanf(p_line,
                        "+QIURC: \"incoming\",%u,%u,\"%39[^\"]\",%u",
                        &data.connect_id,
                        &data.server_id,
                        ip_str,
                        &data.remote_port);
    if (parsed != 4
        || convert_ip_str(ip_str, &data.remote_ip, strchr(ip_str, ':') == NULL)
               != 0)
    {
        ret = PARSER_FAILURE;
    }

    if ((p_urc_handlers_table[URC_TYPE_QIURC_INCOMING].handler)
        && (ret == PARSER_SUCCESS))
    {
        p_urc_handlers_table[URC_TYPE_QIURC_INCOMING].handler(&data);
    }
}

static void
parse_urc_qiurc_pdp_deact (const char * p_line)
{
    urc_qiurc_pdp_deact_type_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QIURC: \"pdpdeact\",%u", &data.context_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIURC_PDPDEACT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIURC_PDPDEACT].handler(&data);
        }
    }
}

static void
parse_urc_qiurc_dnsgip (const char * p_line)
{
    urc_parser_status_t     ret         = PARSER_SUCCESS;
    void *                  data        = NULL;
    int                     parsed      = 0;
    int                     comma_count = 0;
    urc_qiurc_dnsgip_type_t dnsgip_info;
    memset(&dnsgip_info, 0, sizeof(urc_qiurc_dnsgip_type_t));
    urc_ip_addr_union_t ip_addr = {0};

    for (const char * c = p_line; *c; ++c)
    {
        if (*c == ',')
        {
            comma_count++;
        }
    }

    if (comma_count == 3)
    {
        parsed = sscanf(p_line,
                        "+QIURC: \"dnsgip\",%3hu,%5hu,%u",
                        (uint16_t *) &dnsgip_info.result,
                        &dnsgip_info.ip_count,
                        &dnsgip_info.dns_ttl);
        if (parsed != 3)
        {
            ret = PARSER_FAILURE;
        }
        else
        {
            data = &dnsgip_info;
        }
    }
    else
    {
        char ip_str[40] = {0};
        parsed = sscanf(p_line, "+QIURC: \"dnsgip\",\"%39[^\"]\"", ip_str);
        if (parsed == 1
            && convert_ip_str(ip_str, &ip_addr, strchr(ip_str, ':') == NULL)
                   == 0)
        {
            data = &ip_addr;
        }
        else
        {
            ret = PARSER_FAILURE;
        }
    }

    if ((ret == PARSER_SUCCESS) && (data != NULL))
    {
        if (p_urc_handlers_table[URC_TYPE_QIURC_DNSGIP].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIURC_DNSGIP].handler(data);
        }
    }
}

static void
parse_urc_qntp (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_qntp_type_t     data;
    char                timestamp_str[32] = {0};
    memset(&data, 0, sizeof(urc_qntp_type_t));

    int parsed = sscanf(p_line,
                        "+QNTP: %3hu,\"%31[^\"]\"",
                        (uint16_t *) &data.result,
                        timestamp_str);
    if (parsed == 2)
    {
        // Parse timestamp string "YYYY/MM/DD,hh:mm:ss±zz"
        parsed = sscanf(timestamp_str,
                        "%4hu/%2hhu/%2hhu,%2hhu:%2hhu:%2hhu%3hhd",
                        &data.time.year,
                        &data.time.month,
                        &data.time.day,
                        &data.time.hour,
                        &data.time.minute,
                        &data.time.second,
                        &data.time.tz_offset);
        if (parsed != 7)
        {
            ret = PARSER_FAILURE;
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if ((ret == PARSER_SUCCESS))
    {
        if (p_urc_handlers_table[URC_TYPE_QNTP].handler)
        {
            p_urc_handlers_table[URC_TYPE_QNTP].handler(&data);
        }
    }
}

static void
parse_urc_qping (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_qping_type_t    data;
    memset(&data, 0, sizeof(urc_qping_type_t));
    uint16_t value           = 0;
    char     remain_data[80] = {0};
    int parsed = sscanf(p_line, "+QPING: %3hu,%79[^\n]", &value, remain_data);
    if (parsed >= 1)
    {
        if (value != 0)
        {
            data.ping_result_type.single_ping.ping_result
                = (urc_tcp_ip_result_code_t) value;
        }
        else
        {
            int comma_count = 0;
            for (const char * c = remain_data; *c; ++c)
            {
                if (*c == ',')
                {
                    comma_count++;
                }
            }
            if (comma_count == 3)
            {
                char ip_str[40] = {0};
                data.ping_result_type.single_ping.ping_result
                    = (urc_tcp_ip_result_code_t) value;
                parsed = sscanf(remain_data,
                                "\"%39[^\"]\",%u,%u,%u",
                                ip_str,
                                &data.ping_result_type.single_ping.byte,
                                &data.ping_result_type.single_ping.time,
                                &data.ping_result_type.single_ping.ttl);
                if ((parsed != 4)
                    || (convert_ip_str(
                            ip_str,
                            &data.ping_result_type.single_ping.ip_addr,
                            strchr(ip_str, ':') == NULL)
                        != 0))
                {
                    ret = PARSER_FAILURE;
                }
            }
            else
            {
                data.ping_result_type.summary_ping.finresult
                    = (urc_tcp_ip_result_code_t) value;
                parsed = sscanf(remain_data,
                                "%u,%u,%u,%u,%u,%u",
                                &data.ping_result_type.summary_ping.sent,
                                &data.ping_result_type.summary_ping.received,
                                &data.ping_result_type.summary_ping.lost,
                                &data.ping_result_type.summary_ping.time_min,
                                &data.ping_result_type.summary_ping.time_max,
                                &data.ping_result_type.summary_ping.time_avg);
                if (parsed != 6)
                {
                    ret = PARSER_FAILURE;
                }
            }
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if ((ret == PARSER_SUCCESS))
    {
        if (p_urc_handlers_table[URC_TYPE_QPING].handler)
        {
            p_urc_handlers_table[URC_TYPE_QPING].handler(&data);
        }
    }
}

static void
parse_urc_qssl_open (const char * p_line)
{
    urc_qsslopen_type_t data;
    memset(&data, 0, sizeof(urc_qsslopen_type_t));

    if (sscanf(p_line,
               "+QSSLOPEN: %2hhu,%3hu",
               &data.client_id,
               (uint16_t *) &data.err)
        == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QSSLOPEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QSSLOPEN].handler(&data);
        }
    }
}

static void
parse_urc_qcscon (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_qcscon_type_t   data;
    uint16_t            len = strlen(p_line);
    memset(&data, 0, sizeof(data));
    if (len == 12) /* 12 = strlen("+QCSCON: x,x") */
    {
        if (p_line[11] == '1')
        {
            data = URC_QCSCON_MODE_CONNECTED;
        }
        else if (p_line[11] == '0')
        {
            data = URC_QCSCON_MODE_IDLE;
        }
        else
        {
            ret = PARSER_FAILURE;
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if (p_urc_handlers_table[URC_TYPE_QCSCON].handler && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_QCSCON].handler(&data);
    }
}

static void
parse_urc_qssl_buffer_mode (const char * p_line)
{
    urc_qssl_recv_buffer_mode_type_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QSSLURC: \"recv\",%2hhu", &data.client_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QSSLURC_RECV].handler)
        {
            p_urc_handlers_table[URC_TYPE_QSSLURC_RECV].handler(&data);
        }
    }
}

static void
parse_urc_qssl_closed (const char * p_line)
{
    urc_qssl_closed_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+QSSLURC: \"closed\",%2hhu", &data.client_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QSSLURC_CLOSED].handler)
        {
            p_urc_handlers_table[URC_TYPE_QSSLURC_CLOSED].handler(&data);
        }
    }
}

static urc_parser_status_t
get_ip_type (const char * buffer, urc_cgev_pdp_type_t * type)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    if (strstr(buffer, "\"IPV4V6\"") != NULL)
    {
        *type = URC_CGEV_PDP_IPV4V6;
    }
    else if (strstr(buffer, "\"PPP\"") != NULL)
    {
        *type = URC_CGEV_PDP_PPP;
    }
    else if (strstr(buffer, "\"IPV6\"") != NULL)
    {
        *type = URC_CGEV_PDP_IPV6;
    }
    else if (strstr(buffer, "\"IP\"") != NULL)
    {
        *type = URC_CGEV_PDP_IPV4;
    }
    else
    {
        ret = PARSER_FAILURE;
    }
    return ret;
}

static void
parse_urc_cgev_reject (const char * p_line)
{
    urc_parser_status_t    ret = PARSER_SUCCESS;
    urc_cgev_reject_type_t data;
    memset(&data, 0, sizeof(data));

    /* get pdp_type */
    ret = get_ip_type(p_line, &data.pdp_type);

    /* get pdp_address */
    switch (data.pdp_type)
    {
        case URC_CGEV_PDP_IPV4:
            if (sscanf(p_line,
                       "+CGEV: REJECT \"IP\",\"%u.%u.%u.%u\"",
                       &data.pdp_address[0],
                       &data.pdp_address[1],
                       &data.pdp_address[2],
                       &data.pdp_address[3])
                < 4)
            {
                ret |= PARSER_FAILURE;
            }
            break;
        default:
            ret |= PARSER_FAILURE;
            break;
    }

    if (p_urc_handlers_table[URC_TYPE_CGEV_REJECT].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CGEV_REJECT].handler(&data);
    }
}

static void
parse_urc_cgev_nw_react (const char * p_line)
{
    urc_parser_status_t      ret = PARSER_SUCCESS;
    urc_cgev_nw_react_type_t data;
    memset(&data, 0, sizeof(data));
    /* get pdp_type */
    ret = get_ip_type(p_line, &data.pdp_type);

    /* get pdp_address */
    switch (data.pdp_type)
    {
        case URC_CGEV_PDP_IPV4:
            if (sscanf(p_line,
                       "+CGEV: NW REACT "
                       "\"IP\",\"%u.%u.%u.%u\",%u",
                       &data.pdp_address[0],
                       &data.pdp_address[1],
                       &data.pdp_address[2],
                       &data.pdp_address[3],
                       &data.pdp_context_id)
                < 4)
            {
                ret |= PARSER_FAILURE;
            }
            break;
        default:
            break;
    }

    if (p_urc_handlers_table[URC_TYPE_CGEV_NW_REACT].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CGEV_NW_REACT].handler(&data);
    }
}

static void
parse_urc_cgev_nw_deact (const char * p_line)
{
    urc_parser_status_t      ret = PARSER_SUCCESS;
    urc_cgev_nw_deact_type_t data;
    memset(&data, 0, sizeof(data));

    /* get pdp_type */
    ret = get_ip_type(p_line, &data.pdp_type);

    /* get pdp_address */
    switch (data.pdp_type)
    {
        case URC_CGEV_PDP_IPV4:
            if (sscanf(p_line,
                       "+CGEV: NW DEACT "
                       "\"IP\",\"%u.%u.%u.%u\",%u",
                       &data.pdp_address[0],
                       &data.pdp_address[1],
                       &data.pdp_address[2],
                       &data.pdp_address[3],
                       &data.pdp_context_id)
                < 4)
            {
                ret |= PARSER_FAILURE;
            }
            break;
        default:
            break;
    }

    if (p_urc_handlers_table[URC_TYPE_CGEV_NW_DEACT].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CGEV_NW_DEACT].handler(&data);
    }
}

static void
parse_urc_cgev_me_deact (const char * p_line)
{
    urc_parser_status_t      ret = PARSER_SUCCESS;
    urc_cgev_me_deact_type_t data;
    memset(&data, 0, sizeof(data));

    /* get pdp_type */
    ret = get_ip_type(p_line, &data.pdp_type);

    /* get pdp_address */

    switch (data.pdp_type)
    {
        case URC_CGEV_PDP_IPV4:
            if (sscanf(p_line,
                       "+CGEV: ME DEACT "
                       "\"IP\",\"%u.%u.%u.%u\",%u",
                       &data.pdp_address[0],
                       &data.pdp_address[1],
                       &data.pdp_address[2],
                       &data.pdp_address[3],
                       &data.pdp_context_id)
                < 5)
            {
                ret |= PARSER_FAILURE;
            }
            break;
        default:
            break;
    }

    if (p_urc_handlers_table[URC_TYPE_CGEV_ME_DEACT].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_CGEV_ME_DEACT].handler(&data);
    }
}

static urc_parser_status_t
get_class_type (const char * buffer, urc_cgev_class_t * class)
{
    urc_parser_status_t ret = PARSER_FAILURE;
    uint16_t            len = strlen(buffer);

    typedef struct
    {
        const char * pattern;
        uint16_t     len;
        urc_cgev_class_t class;
    } urc_class_mapping_t;

    urc_class_mapping_t mappings[] = {
        {"A",  1, URC_CGEV_CLASS_A },
        {"B",  1, URC_CGEV_CLASS_B },
        {"C",  1, URC_CGEV_CLASS_C },
        {"CG", 2, URC_CGEV_CLASS_CG},
        {"CC", 2, URC_CGEV_CLASS_CC},
    };
    size_t num_mappings = sizeof(mappings) / sizeof(mappings[0]);

    for (size_t i = 0; i < num_mappings; ++i)
    {
        if (len == mappings[i].len
            && memcmp(mappings[i].pattern, buffer, strlen(mappings[i].pattern))
                   == 0)
        {
            *class = mappings[i].class;
            ret    = PARSER_SUCCESS;
            break;
        }
    }
    return ret;
}

static void
parse_urc_cgev_nw_class (const char * p_line)
{
    urc_parser_status_t      ret = PARSER_SUCCESS;
    urc_cgev_nw_class_type_t data;
    memset(&data, 0, sizeof(data));

    uint16_t len = strlen(p_line);
    if (len >= 17) /* 17 = strlen("+CGEV: NW CLASS X")*/
    {
        ret = get_class_type(&p_line[16], &data.class_type);
        if (p_urc_handlers_table[URC_TYPE_CGEV_NW_CLASS].handler
            && ret == PARSER_SUCCESS)
        {
            p_urc_handlers_table[URC_TYPE_CGEV_NW_CLASS].handler(&data);
        }
    }
}

static void
parse_urc_cgev_me_class (const char * p_line)
{
    urc_parser_status_t      ret = PARSER_SUCCESS;
    urc_cgev_nw_class_type_t data;
    memset(&data, 0, sizeof(data));

    uint16_t len = strlen(p_line);
    if (len >= 17) /* 17 = strlen("+CGEV: NW CLASS X")*/
    {
        ret = get_class_type(&p_line[16], &data.class_type);

        if (p_urc_handlers_table[URC_TYPE_CGEV_ME_CLASS].handler
            && ret == PARSER_SUCCESS)
        {
            p_urc_handlers_table[URC_TYPE_CGEV_ME_CLASS].handler(&data);
        }
    }
}

static void
parse_urc_cgev_pdp_act (const char * p_line)
{
    urc_cgev_pdp_act_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+CGEV: PDP ACT %u", &data.pdp_context_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_CGEV_PDP_ACT].handler)
        {
            p_urc_handlers_table[URC_TYPE_CGEV_PDP_ACT].handler(&data);
        }
    }
}

static void
parse_urc_cgev_pdp_deact (const char * p_line)
{
    urc_cgev_pdp_deact_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+CGEV: PDP DEACT %u", &data.pdp_context_id) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_CGEV_PDP_DEACT].handler)
        {
            p_urc_handlers_table[URC_TYPE_CGEV_PDP_DEACT].handler(&data);
        }
    }
}

static void
parse_urc_fota_downloading (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_HTTP_DOWNLOADING};

    if (sscanf(p_line,
               "+QIND: \"FOTA\",\"DOWNLOADING\",%u",
               &data.params.downloading_percent)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_DOWNLOADING].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_DOWNLOADING].handler(&data);
        }
    }
}

static void
parse_urc_fota_downloaded (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_LWM2M_HTTP_DOWNLOADED};

    if (sscanf(p_line,
               "+LWM2M: \"FOTA\",\"DOWNLOADED\",%u",
               &data.params.error_code)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_LWM2M_FOTA_DOWNLOADED].handler)
        {
            p_urc_handlers_table[URC_TYPE_LWM2M_FOTA_DOWNLOADED].handler(&data);
        }
    }
}

static void
parse_urc_fota_httpend (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_HTTP_END};

    if (sscanf(
            p_line, "+QIND: \"FOTA\",\"HTTPEND\",%u", &data.params.error_code)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_HTTPEND].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_HTTPEND].handler(&data);
        }
    }
}

static void
parse_urc_fota_coapend (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_COAP_END};

    if (sscanf(
            p_line, "+QIND: \"FOTA\",\"COAPEND\",%u", &data.params.error_code)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_COAPEND].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_COAPEND].handler(&data);
        }
    }
}

static void
parse_urc_fota_updating (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_UPDATING_PROGRESS};

    if (sscanf(p_line,
               "+QIND: \"FOTA\",\"UPDATING\",%u",
               &data.params.updating_percent)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_UPDATING].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_UPDATING].handler(&data);
        }
    }
}

static void
parse_urc_fota_end (const char * p_line)
{
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_UPDATING_FINISHED};

    if (sscanf(p_line, "+QIND: \"FOTA\",\"END\",%u", &data.params.error_code)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_END].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_END].handler(&data);
        }
    }
}

static void
parse_urc_fota_ftpend (const char * p_line)
{
    urc_dfota_type_t data   = {.event = URC_DFOTA_QIND_FTP_END};

    int              parsed = sscanf(
        p_line, "+QIND: \"FOTA\",\"FTPEND\",%u", &data.params.error_code);
    if (parsed == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_FTPEND].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_FOTA_FTPEND].handler(&data);
        }
    }
}

static void
parse_urc_fota_httpstart (const char * p_line)
{
    UNUSED_PARAM(p_line);
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_HTTP_DOWNLOAD_START};

    if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_HTTPSTART].handler)
    {
        p_urc_handlers_table[URC_TYPE_QIND_FOTA_HTTPSTART].handler(&data);
    }
}

static void
parse_urc_fota_coapstart (const char * p_line)
{
    UNUSED_PARAM(p_line);
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_COAP_DOWNLOAD_START};

    if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_COAPSTART].handler)
    {
        p_urc_handlers_table[URC_TYPE_QIND_FOTA_COAPSTART].handler(&data);
    }
}

static void
parse_urc_fota_ftpstart (const char * p_line)
{
    UNUSED_PARAM(p_line);
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_FTP_DOWNLOAD_START};

    if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_FTPSTART].handler)
    {
        p_urc_handlers_table[URC_TYPE_QIND_FOTA_FTPSTART].handler(&data);
    }
}

static void
parse_urc_fota_start_updating (const char * p_line)
{
    UNUSED_PARAM(p_line);
    urc_dfota_type_t data = {.event = URC_DFOTA_QIND_UPDATING_STARTED};

    if (p_urc_handlers_table[URC_TYPE_QIND_FOTA_START_UPDATING].handler)
    {
        p_urc_handlers_table[URC_TYPE_QIND_FOTA_START_UPDATING].handler(&data);
    }
}

static void
parse_urc_qhttp_get (const char * p_line)
{
    urc_qhttp_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QHTTPGET: %u,%u,%u",
               (uint32_t *) &data.result_code,
               (uint32_t *) &data.response_code,
               &data.content_length)
        == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPGET].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPGET].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_post (const char * p_line)
{
    urc_qhttp_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QHTTPPOST: %u,%u,%u",
               (uint32_t *) &data.result_code,
               (uint32_t *) &data.response_code,
               &data.content_length)
        == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPPOST].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPPOST].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_postfile (const char * p_line)
{
    urc_qhttp_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QHTTPPOSTFILE: %u,%u,%u",
               (uint32_t *) &data.result_code,
               (uint32_t *) &data.response_code,
               &data.content_length)
        == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPPOSTFILE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPPOSTFILE].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_put (const char * p_line)
{
    urc_qhttp_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QHTTPPUT: %u,%u,%u",
               (uint32_t *) &data.result_code,
               (uint32_t *) &data.response_code,
               &data.content_length)
        == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPPUT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPPUT].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_putfile (const char * p_line)
{
    urc_qhttp_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QHTTPPUTFILE: %u,%u,%u",
               (uint32_t *) &data.result_code,
               (uint32_t *) &data.response_code,
               &data.content_length)
        == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPPUTFILE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPPUTFILE].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_read (const char * p_line)
{
    urc_http_result_code_t data;
    memset(&data, 0, sizeof(urc_http_result_code_t));
    if (sscanf(p_line, "+QHTTPREAD: %3hu", (uint16_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPREAD].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPREAD].handler(&data);
        }
    }
}

static void
parse_urc_qhttp_readfile (const char * p_line)
{
    urc_http_result_code_t data;
    memset(&data, 0, sizeof(urc_http_result_code_t));
    if (sscanf(p_line, "+QHTTPREADFILE: %3hu", (uint16_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QHTTPREADFILE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QHTTPREADFILE].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_stat (const char * p_line)
{
    urc_qmtsnstat_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QMTSNSTAT: %u,%d",
               &data.client_idx,
               (int32_t *) &data.err_code)
        == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNSTAT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNSTAT].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_recv (const char * p_line)
{
    urc_qmtsnrecv_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+QMTSNRECV: %u,%u,", &data.client_idx, &data.msg_id)
        == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNRECV].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNRECV].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_sub (const char * p_line)
{
    urc_qmtsn_result_type_t data;
    memset(&data, 0, sizeof(urc_qmtsn_result_type_t));
    int parsed = sscanf(p_line,
                        "+QMTSNSUB:%2hhu,%5hu,%2hhu,%3hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result,
                        &data.value);
    if (parsed >= 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNSUB].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNSUB].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_uns (const char * p_line)
{
    urc_qmtsn_result_type_t data   = {0};
    int                     parsed = sscanf(p_line,
                        "+QMTSNUNS:%2hhu,%5hu,%2hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result);
    if (parsed == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNUNS].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNUNS].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_reg (const char * p_line)
{
    urc_qmtsn_result_type_t data   = {0};
    int                     parsed = sscanf(p_line,
                        "+QMTSNREG:%2hhu,%5hu,%2hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result);
    if (parsed == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNREG].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_pub (const char * p_line)
{
    urc_qmtsn_result_type_t data   = {0};
    int                     parsed = sscanf(p_line,
                        "+QMTSNPUB:%2hhu,%5hu,%2hhu,%3hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result,
                        &data.value);
    if (parsed >= 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNPUB].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNPUB].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_willupd (const char * p_line)
{
    urc_qmtsn_result_type_t data;
    memset(&data, 0, sizeof(urc_qmtsn_result_type_t));
    int parsed = sscanf(p_line,
                        "+QMTSNWILLUPD: %2hhu,%2hhu",
                        &data.client_idx,
                        (uint8_t *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSN_WILLUPD].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSN_WILLUPD].handler(&data);
        }
    }
}

static void
parse_urc_qmtsn_sleep (const char * p_line)
{
    urc_qmtsn_result_type_t data;
    memset(&data, 0, sizeof(urc_qmtsn_result_type_t));
    int parsed = sscanf(p_line,
                        "+QMTSNSLEEP: %2hhu,%2hhu",
                        &data.client_idx,
                        (uint8_t *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSNSLEEP].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSNSLEEP].handler(&data);
        }
    }
}

static void
parse_urc_qmtopen (const char * p_line)
{
    urc_qmtopen_type_t data;
    memset(&data, 0, sizeof(urc_qmtopen_type_t));
    int parsed = sscanf(
        p_line, "+QMTOPEN: %2hhu,%d", &data.client_idx, (int *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTOPEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTOPEN].handler(&data);
        }
    }
}

static void
parse_urc_qmtclose (const char * p_line)
{
    urc_qmtclose_type_t data;
    memset(&data, 0, sizeof(urc_qmtclose_type_t));
    int parsed = sscanf(p_line,
                        "+QMTCLOSE: %2hhu,%d",
                        (uint8_t *) &data.client_idx,
                        (int *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTCLOSE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTCLOSE].handler(&data);
        }
    }
}

static void
parse_urc_qmtstat (const char * p_line)
{
    urc_qmtstat_type_t data;
    memset(&data, 0, sizeof(urc_qmtstat_type_t));
    int parsed = sscanf(p_line,
                        "+QMTSTAT: %2hhu,%2hhu",
                        &data.client_idx,
                        (uint8_t *) &data.err_code);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSTAT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSTAT].handler(&data);
        }
    }
}

static void
parse_urc_qmtconn (const char * p_line)
{
    urc_qmtconn_type_t data;
    memset(&data, 0, sizeof(urc_qmtconn_type_t));
    int parsed = sscanf(p_line,
                        "+QMTCONN: %2hhu,%2hhu,%2hhu",
                        &data.client_idx,
                        (uint8_t *) &data.result,
                        (uint8_t *) &data.ret_code);
    if (parsed >= 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTCONN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTCONN].handler(&data);
        }
    }
}

static void
parse_urc_qmtsub (const char * p_line)
{
    urc_qmtsub_type_t data;
    memset(&data, 0, sizeof(urc_qmtsub_type_t));
    int parsed = sscanf(p_line,
                        "+QMTSUB: %2hhu,%5hu,%2hhu,%2hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result,
                        (uint8_t *) &data.value);
    if (parsed >= 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTSUB].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTSUB].handler(&data);
        }
    }
}

static void
parse_urc_qmtpub (const char * p_line)
{
    urc_qmtpub_type_t data;
    memset(&data, 0, sizeof(urc_qmtpub_type_t));
    int parsed = sscanf(p_line,
                        "+QMTPUB: %2hhu,%5hu,%2hhu,%2hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result,
                        (uint8_t *) &data.value);
    if (parsed >= 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTPUB].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTPUB].handler(&data);
        }
    }
}

static void
parse_urc_recv (const char * p_line)
{
}

static void
parse_urc_qmtdisc (const char * p_line)
{
    urc_qmtdisc_type_t data;
    memset(&data, 0, sizeof(urc_qmtdisc_type_t));
    int parsed = sscanf(
        p_line, "+QMTDISC: %2hhu,%d", &data.client_idx, (int *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTDISC].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTDISC].handler(&data);
        }
    }
}

static void
parse_urc_qmtpuns (const char * p_line)
{
    urc_qmtuns_type_t data;
    memset(&data, 0, sizeof(urc_qmtuns_type_t));
    int parsed = sscanf(p_line,
                        "+QMTUNS: %2hhu,%5hu,%2hhu",
                        &data.client_idx,
                        &data.msg_id,
                        (uint8_t *) &data.result);
    if (parsed == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTUNS].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTUNS].handler(&data);
        }
    }
}

static void
parse_urc_qmtping (const char * p_line)
{
    urc_qmtping_type_t data;
    memset(&data, 0, sizeof(urc_qmtping_type_t));
    int parsed = sscanf(p_line,
                        "+QMTPING: %2hhu,%2hhu",
                        &data.client_idx,
                        (uint8_t *) &data.result);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QMTPING].handler)
        {
            p_urc_handlers_table[URC_TYPE_QMTPING].handler(&data);
        }
    }
}

static void
parse_urc_qind_geofence (const char * p_line)
{
    urc_qind_geofence_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QIND: \"GEOFENCE\","
               "%u,%u,\"%u/%u/%u %u:%u:%u\",%f,%f,%f,%f,%f,%f,%f,%f",
               &data.geoid,
               &data.action,
               &data.time.year,
               &data.time.mon,
               &data.time.date,
               &data.time.hour,
               &data.time.min,
               &data.time.sec,
               &data.latitude,
               &data.longitude,
               &data.altitude,
               &data.course,
               &data.speed,
               &data.PDOP,
               &data.HDOP,
               &data.VDOP)
        == 16)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_GEOFENCE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_GEOFENCE].handler(&data);
        }
    }
}

static void
parse_urc_qscrub_end (const char * p_line)
{
    urc_scrub_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+QSCRUB: \"END\",%d", (int32_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QSCRUB_END].handler)
        {
            p_urc_handlers_table[URC_TYPE_QSCRUB_END].handler(&data);
        }
    }
}

static void
parse_urc_psm_timer (const char * p_line)
{
    urc_qpsmtimer_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+QPSMTIMER: %u,%u", &data.tau_timer, &data.t3324_timer)
        == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QPSMTIMER].handler)
        {
            p_urc_handlers_table[URC_TYPE_QPSMTIMER].handler(&data);
        }
    }
}

static void
parse_urc_qcellscan (const char * p_line)
{
    urc_parser_status_t       ret    = PARSER_SUCCESS;
    void *                    data   = NULL;
    int16_t                   value  = 0;
    int                       parsed = 0;
    urc_qcellscan_cell_info_t cell_info;
    memset(&cell_info, 0, sizeof(urc_qcellscan_cell_info_t));
    urc_qcellscan_type_t scan_info;
    memset(&scan_info, 0, sizeof(urc_qcellscan_type_t));
    urc_qcellscan_err_code_t err_code = URC_QCELLSCAN_FAILURE;

    if (strchr(p_line, '"') == NULL)
    {
        parsed = sscanf(p_line, "+QCELLSCAN: %hd", &value);
        if (parsed != 1)
        {
            ret = PARSER_FAILURE;
        }
        else
        {
            if (value == -1)
            {
                data = &err_code;
            }
            else
            {
                scan_info.cell_num = (uint16_t) value;
                data               = &scan_info;
            }
        }
    }
    else
    {
        char rat_str[6]     = {0};
        char remain_str[64] = {0};

        parsed              = sscanf(
            p_line, "+QCELLSCAN: \"%5[^\"]\",%63[^\n]", rat_str, remain_str);
        if (parsed != 2)
        {
            ret = PARSER_FAILURE;
        }
        else
        {
            cell_info.rat_type = get_rat_type(rat_str);
            switch (cell_info.rat_type)
            {
                case URC_ACT_GSM:
                {
                    parsed = sscanf(remain_str,
                                    "%3[^,],%3[^,],%hx,%x,%2hhu,%2hhu",
                                    cell_info.mcc,
                                    cell_info.mnc,
                                    &cell_info.rat_specific.gsm.lac,
                                    &cell_info.cid,
                                    &cell_info.rat_specific.gsm.bsic,
                                    &cell_info.rat_specific.gsm.rxlev);
                    if (parsed != 6)
                    {
                        ret = PARSER_FAILURE;
                    }
                }
                break;

                case URC_ACT_eMTC:
                case URC_ACT_NB_IOT:
                {
                    parsed = sscanf(remain_str,
                                    "%3[^,],%3[^,],%hx,%u,%u,%x,%hd,%hd,%hd",
                                    cell_info.mcc,
                                    cell_info.mnc,
                                    &cell_info.rat_specific.lte.tac,
                                    &cell_info.rat_specific.lte.earfcn,
                                    &cell_info.rat_specific.lte.pci,
                                    &cell_info.cid,
                                    &cell_info.rat_specific.lte.rsrp,
                                    &cell_info.rat_specific.lte.rsrq,
                                    &cell_info.rat_specific.lte.rssi);
                    if (parsed != 9)
                    {
                        ret = PARSER_FAILURE;
                    }
                }
                break;

                default:
                {
                    ret = PARSER_FAILURE;
                }
                break;
            }
            data = &cell_info;
        }
    }

    if ((ret == PARSER_SUCCESS) && (data != NULL))
    {
        if (p_urc_handlers_table[URC_TYPE_QCELLSCAN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QCELLSCAN].handler(data);
        }
    }
}

static void
parse_urc_crtdcp (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_crtdcp_type_t   data;
    memset(&data, 0, sizeof(urc_crtdcp_type_t));

    int parsed = sscanf(p_line,
                        "+CRTDCP:%3hhu,%4hu,\"%1358[^\"]\"",
                        &data.cid,
                        &data.cpdata_length,
                        data.cpdata);

    // Null-terminate safely
    data.cpdata[URC_CRTDCP_MAX_DATA_LEN - 1] = '\0';
    if (parsed >= 2)
    {
        if (data.cpdata_length == 0)
        {
            data.cpdata[0] = '\0';
        }
        else
        {
            if (strlen(data.cpdata) != data.cpdata_length)
            {
                ret = PARSER_FAILURE;
            }
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if (ret == PARSER_SUCCESS)
    {
        if (p_urc_handlers_table[URC_TYPE_CRTDCP].handler)
        {
            p_urc_handlers_table[URC_TYPE_CRTDCP].handler(&data);
        }
    }
}

static void
parse_urc_qlwurc_event (const char * p_line)
{
    urc_qlwurc_event_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+QLWURC: \"event\",%u,%u",
               &data.short_id,
               (uint32_t *) &data.state_code)
        == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QLWURC_EVENT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QLWURC_EVENT].handler(&data);
        }
    }
}

static void
parse_urc_qlwurc_uldata (const char * p_line)
{
    urc_qlwurc_uldata_type_t data;
    memset(&data, 0, sizeof(data));
    // Get <shortID>
    if (sscanf(p_line, "+QLWURC: \"uldata\",%u", &data.short_id) == 1)
    {
        // Get URI
        char * p_start_uri = strstr(p_line + 18, ",\"");
        char * p_fin_uri   = strstr(p_line + 18, "\",");
        if (p_start_uri != NULL && p_fin_uri != NULL)
        {
            int16_t uri_len = p_fin_uri - p_start_uri - 2;
            if (uri_len > 0)
            {
                memset(data.uri, 0, sizeof(data.uri));
                strncpy((char *) data.uri, p_start_uri + 2, uri_len);

                // Get <messageID>,<tokenID>,<response_code>
                if (sscanf(p_fin_uri,
                           "\",%u,%u,%d",
                           &data.msg_id,
                           &data.token_id,
                           (int32_t *) &data.res_code)
                    == 3)
                {
                    if (p_urc_handlers_table[URC_TYPE_QLWURC_ULDATA].handler)
                    {
                        p_urc_handlers_table[URC_TYPE_QLWURC_ULDATA].handler(
                            &data);
                    }
                }
            }
        }
    }
}

static void
parse_urc_qlwurc_recv (const char * p_line)
{
    urc_parser_status_t    ret = PARSER_SUCCESS;
    urc_qlwurc_recv_type_t data;
    memset(&data, 0, sizeof(data));
    // Get URI
    char * p_start_uri = strstr(p_line + 15, ",\"");
    char * p_fin_uri   = strstr(p_line + 15, "\",");
    if (p_start_uri != NULL && p_fin_uri != NULL)
    {
        int16_t uri_len = p_fin_uri - p_start_uri - 2;
        if (uri_len > 0)
        {
            memset(data.uri, 0, sizeof(data.uri));

            strncpy((char *) data.uri, p_start_uri + 2, uri_len);

            if (sscanf(p_fin_uri,
                       "\",%u,%u",
                       (uint32_t *) &data.data_type,
                       &data.data_length)
                < 2)
            {
                ret = PARSER_FAILURE;
            }
        }
        else
        {
            ret = PARSER_FAILURE;
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    // Read p_data->data_length bytes from buffer.
    // Not implement yet.

    if (p_urc_handlers_table[URC_TYPE_QLWURC_RECV].handler
        && ret == PARSER_SUCCESS)
    {
        p_urc_handlers_table[URC_TYPE_QLWURC_RECV].handler(&data);
    }
}

static void
parse_urc_qlwreg (const char * p_line)
{
    urc_qlwreg_err_code_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QLWREG: %2hhu", (uint8_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QLWREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_QLWREG].handler(&data);
        }
    }
}

static void
parse_urc_qlwdereg (const char * p_line)
{
    urc_qlwreg_err_code_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QLWDEREG: %2hhu", (uint8_t *) &data) == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QLWDEREG].handler)
        {
            p_urc_handlers_table[URC_TYPE_QLWDEREG].handler(&data);
        }
    }
}

void
parse_urc_qind_open (const char * p_line)
{
    urc_nidd_err_code_t data;
    memset(&data, 0, sizeof(data));

    if (sscanf(p_line, "+QIND: \"nipd\",\"open\",%3hu", (uint16_t *) &data)
        == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QIND_NIPD_OPEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QIND_NIPD_OPEN].handler(&data);
        }
    }
}

static void
parse_urc_qlwurc_observe (const char * p_line)
{
    urc_qlwurc_observe_type_t data;
    memset(&data, 0, sizeof(data));
    // Get <shortID>
    if (sscanf(p_line, "+QLWURC: \"observe\",%u", &data.short_id) == 1)
    {
        // Get URI
        char * p_start_uri = strstr(p_line + 19, ",\"");
        char * p_fin_uri   = strstr(p_line + 19, "\",");
        if (p_start_uri != NULL && p_fin_uri != NULL)
        {
            int16_t uri_len = p_fin_uri - p_start_uri - 2;
            if (uri_len > 0)
            {
                memset(data.uri, 0, sizeof(data.uri));
                strncpy((char *) data.uri, p_start_uri + 2, uri_len);

                // Get <messageID>,<tokenID>,<observe_flag>
                if (sscanf(p_fin_uri,
                           "\",%u,%u,%u",
                           &data.msg_id,
                           &data.token_id,
                           (uint32_t *) &data.observe_flag)
                    == 3)
                {
                    if (p_urc_handlers_table[URC_TYPE_QLWURC_OBSERVE].handler)
                    {
                        p_urc_handlers_table[URC_TYPE_QLWURC_OBSERVE].handler(
                            &data);
                    }
                }
            }
        }
    }
}

static void
parse_urc_xtra_dl (const char * p_line)
{
    urc_qgpsurc_xtra_dl_err_code_t data;
    memset(&data, 0, sizeof(urc_qgpsurc_xtra_dl_err_code_t));

    int parsed = sscanf(p_line, "+QGPSURC: \"XTRA_DL\",%d", (int *) &data);
    if (parsed == 1)
    {
        if (p_urc_handlers_table[URC_TYPE_QGPSURC_XTRA].handler)
        {
            p_urc_handlers_table[URC_TYPE_QGPSURC_XTRA].handler(&data);
        }
    }
}

static void
parse_urc_jamming_ind (const char * p_line)
{
    urc_qgpsurc_jamming_ind_type_t data;
    memset(&data, 0, sizeof(urc_qgpsurc_jamming_ind_type_t));
    int parsed = sscanf(p_line,
                        "+QGPSURC: \"jamming_ind\",%2hhu,%3hhu,%d",
                        (uint8_t *) &data.state,
                        &data.rnl,
                        &data.metric_val);
    if (parsed == 3)
    {
        if (p_urc_handlers_table[URC_TYPE_QGPSURC_JAMMING_IND].handler)
        {
            p_urc_handlers_table[URC_TYPE_QGPSURC_JAMMING_IND].handler(&data);
        }
    }
}

static void
parse_urc_qcoapopen (const char * p_line)
{
    urc_qcoap_type_t data;
    memset(&data, 0, sizeof(urc_qcoap_type_t));
    int parsed = sscanf(
        p_line, "+QCOAPOPEN: %2hhu,%d", &data.client_id, (int *) &data.result);

    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QCOAPOPEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QCOAPOPEN].handler(&data);
        }
    }
}

static void
parse_urc_qcoapclose (const char * p_line)
{
    urc_qcoap_type_t data;
    memset(&data, 0, sizeof(urc_qcoap_type_t));
    int parsed = sscanf(
        p_line, "+QCOAPCLOSE: %2hhu,%d", &data.client_id, (int *) &data.result);

    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QCOAPCLOSE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QCOAPCLOSE].handler(&data);
        }
    }
}

static void
parse_urc_qcoapack (const char * p_line)
{
    urc_qcoapack_type_t data;
    memset(&data, 0, sizeof(urc_qcoapack_type_t));
    int parsed = sscanf(p_line,
                        "+QCOAPACK: %2hhu,%2hhu,%5hu,%d",
                        &data.client_id,
                        (uint8_t *) &data.type,
                        &data.msg_id,
                        (int *) &data.result);

    if (parsed == 4)
    {
        if (p_urc_handlers_table[URC_TYPE_QCOAPACK].handler)
        {
            p_urc_handlers_table[URC_TYPE_QCOAPACK].handler(&data);
        }
    }
}

static void
parse_urc_qcoaprecv (const char * p_line)
{
}

static void
parse_urc_qcoapind (const char * p_line)
{
    urc_qcoapind_type_t data;
    memset(&data, 0, sizeof(urc_qcoapind_type_t));
    int parsed = sscanf(p_line,
                        "+QCOAPIND: %2hhu,%2hhu,%5hu,%5hu,%2hhu",
                        &data.client_id,
                        (uint8_t *) &data.type,
                        &data.msg_id,
                        &data.retry_times,
                        (uint8_t *) &data.result);

    if (parsed == 5)
    {
        if (p_urc_handlers_table[URC_TYPE_QCOAPIND].handler)
        {
            p_urc_handlers_table[URC_TYPE_QCOAPIND].handler(&data);
        }
    }
}

static void
parse_urc_qftpopen (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPOPEN: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPOPEN].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPOPEN].handler(&data);
    }
}

static void
parse_urc_qftpclose (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPCLOSE: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPCLOSE].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPCLOSE].handler(&data);
    }
}

static void
parse_urc_qftpdel (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPDEL: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPDEL].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPDEL].handler(&data);
    }
}

static void
parse_urc_qftpcwd (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPCWD: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPCWD].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPCWD].handler(&data);
    }
}

static void
parse_urc_qftpmkdir (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPMKDIR: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPMKDIR].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPMKDIR].handler(&data);
    }
}

static void
parse_urc_qftprmdir (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPRMDIR: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPRMDIR].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPRMDIR].handler(&data);
    }
}

static void
parse_urc_qftprename (const char * p_line)
{
    urc_ftp_error_type_t data;
    memset(&data, 0, sizeof(urc_ftp_error_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPRENAME: %3hu,%3hu",
                        (uint16_t *) &data.err_code,
                        (uint16_t *) &data.protocol_err_code);
    if (parsed == 2 && p_urc_handlers_table[URC_TYPE_QFTPRENAME].handler)
    {
        p_urc_handlers_table[URC_TYPE_QFTPRENAME].handler(&data);
    }
}

static void
parse_urc_qftpsize (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPSIZE: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPSIZE].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPSIZE].handler(&data);
        }
    }
}

static void
parse_urc_qftpput (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPPUT: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPPUT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPPUT].handler(&data);
        }
    }
}

static void
parse_urc_qftpget (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPGET: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPGET].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPGET].handler(&data);
        }
    }
}

static void
parse_urc_qftpmlsd (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPMLSD: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPMLSD].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPMLSD].handler(&data);
        }
    }
}

static void
parse_urc_qftplist (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPLIST: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPLIST].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPLIST].handler(&data);
        }
    }
}

static void
parse_urc_qftpnlst (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPNLST: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPNLST].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPNLST].handler(&data);
        }
    }
}

static void
parse_urc_qftplen (const char * p_line)
{
    urc_ftp_result_type_t data;
    memset(&data, 0, sizeof(urc_ftp_result_type_t));
    uint32_t value  = 0;
    int      parsed = sscanf(
        p_line, "+QFTPLEN: %3hu,%u", (uint16_t *) &data.err_code, &value);
    if (parsed == 2)
    {
        if (data.err_code == 0)
        {
            data.result.value = value;
        }
        else
        {
            data.result.protocol_err_code = (urc_ftp_protocol_err_code_t) value;
        }

        if (p_urc_handlers_table[URC_TYPE_QFTPLEN].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPLEN].handler(&data);
        }
    }
}

static void
parse_urc_qftpstat (const char * p_line)
{
    urc_qftpstat_type_t data;
    memset(&data, 0, sizeof(urc_qftpstat_type_t));
    int parsed = sscanf(p_line,
                        "+QFTPSTAT: %3hu,%2hhu",
                        (uint16_t *) &data.err_code,
                        (uint8_t *) &data.ftpstat);
    if (parsed == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_QFTPSTAT].handler)
        {
            p_urc_handlers_table[URC_TYPE_QFTPSTAT].handler(&data);
        }
    }
}

static void
parse_urc_qftppwd (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_qftppwd_type_t  data;
    memset(&data, 0, sizeof(urc_qftppwd_type_t));

    char remain[260] = {0};
    int  parsed      = sscanf(p_line,
                        "+QFTPPWD: %3hu,%259[^\r\n]",
                        (uint16_t *) &data.err_code,
                        remain);
    if (parsed == 2)
    {
        if (data.err_code == URC_FTP_SUCCESS)
        {
            parsed = sscanf(remain, "\"%255[^\"]\"", data.result.path_name);
            if (parsed != 1)
            {
                ret = PARSER_FAILURE;
            }
        }
        else
        {
            parsed = sscanf(
                remain, "%hu", (uint16_t *) &data.result.protocol_err_code);
            if (parsed != 1)
            {
                ret = PARSER_FAILURE;
            }
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if ((ret == PARSER_SUCCESS)
        && (p_urc_handlers_table[URC_TYPE_QFTPPWD].handler))
    {
        p_urc_handlers_table[URC_TYPE_QFTPPWD].handler(&data);
    }
}

static void
parse_urc_qftpmdtm (const char * p_line)
{
    urc_parser_status_t ret = PARSER_SUCCESS;
    urc_qftpmdtm_type_t data;
    memset(&data, 0, sizeof(urc_qftpmdtm_type_t));

    char remain[25] = {0};
    int  parsed     = sscanf(p_line,
                        "+QFTPMDTM: %3hu,%24[^\r\n]",
                        (uint16_t *) &data.err_code,
                        remain);
    if (parsed == 2)
    {
        if (data.err_code == URC_FTP_SUCCESS)
        {
            char modify_time[21] = {0};
            parsed               = sscanf(remain, "\"%20[^\"]\"", modify_time);
            if (parsed != 1)
            {
                ret = PARSER_FAILURE;
            }
            else
            {
                // Parse time into value struct
                char         base[15]    = {0};
                char         fraction[4] = {0};

                const char * dot_pos     = strchr(modify_time, '.');
                if (dot_pos)
                {
                    if ((size_t) (dot_pos - modify_time) != 14)
                    {
                        ret = PARSER_FAILURE;
                    }
                    else
                    {
                        strncpy(base, modify_time, 14);
                        strncpy(fraction, dot_pos + 1, 3);
                    }
                }
                else
                {
                    if (strlen(modify_time) != 14)
                    {
                        ret = PARSER_FAILURE;
                    }
                    else
                    {
                        strncpy(base, modify_time, 14);
                    }
                }

                if (ret == PARSER_SUCCESS)
                {
                    int result = sscanf(base,
                                        "%4hu%2hhu%2hhu%2hhu%2hhu%2hhu",
                                        &data.result.timestamp.year,
                                        &data.result.timestamp.month,
                                        &data.result.timestamp.day,
                                        &data.result.timestamp.hour,
                                        &data.result.timestamp.minute,
                                        &data.result.timestamp.second);
                    if (result != 6)
                    {
                        ret = PARSER_FAILURE;
                    }
                    if (fraction[0] != '\0')
                    {
                        parsed = sscanf(fraction,
                                        "%hu",
                                        &data.result.timestamp.millisecond);
                        if (parsed != 1)
                        {
                            ret = PARSER_FAILURE;
                        }
                    }
                    else
                    {
                        data.result.timestamp.millisecond = 0;
                    }
                }
            }
        }
        else
        {
            parsed = sscanf(
                remain, "%hu", (uint16_t *) &data.result.protocol_err_code);
            if (parsed != 1)
            {
                ret = PARSER_FAILURE;
            }
        }
    }
    else
    {
        ret = PARSER_FAILURE;
    }

    if ((ret == PARSER_SUCCESS)
        && (p_urc_handlers_table[URC_TYPE_QFTPMDTM].handler))
    {
        p_urc_handlers_table[URC_TYPE_QFTPMDTM].handler(&data);
    }
}

/**
 * @brief Parses a URC line that serves only as a notification without carrying
 * any data.
 *
 * @param[in] p_line Pointer to the URC line received.
 */
static void
parse_urc_default (const char * p_line)
{
    urc_type_t urc_index = URC_TYPE_INVALID;

    urc_index            = get_urc_index(p_line);
    if (urc_index != URC_TYPE_INVALID
        && p_urc_handlers_table[urc_index].handler)
    {
        p_urc_handlers_table[urc_index].handler(NULL);
    }
}

static void
parse_urc_kcnx_ind (const char * p_line)
{
    urc_kcnx_ind_type_t data;
    memset(&data, 0, sizeof(data));

    bool         correct_parsed = false;
    int          pos            = 0;

    unsigned int cnx_cnf        = 0U;
    unsigned int status         = 0U;

    if (p_line == NULL)
    {
        return;
    }

    int parsed = sscanf(p_line, "+KCNX_IND: %u,%u%n", &cnx_cnf, &status, &pos);

    if (parsed != 2)
    {
        return;
    }
    size_t len = strlen(p_line);

    /* Ensure pointer arithmetic remains valid */
    if ((pos < 0) || ((size_t) pos > len))
    {
        return;
    }

    data.cnx_cnf      = (uint32_t) cnx_cnf;
    data.status       = (urc_kcnx_ind_pdp_conn_status_type_t) status;

    const char * tail = p_line + pos;

    switch (data.status)
    {
        case URC_KCNX_IND_DISCONNECTED:
        case URC_KCNX_IND_CONNECTED:
        {
            unsigned int af = 0U;

            if (sscanf(tail, ",%u", &af) == 1)
            {
                data.af        = (uint32_t) af;
                correct_parsed = true;
            }
            break;
        }

        case URC_KCNX_IND_FAILED_TO_CONNECT:
        {
            unsigned int attempt = 0U;
            unsigned int nbtrial = 0U;
            unsigned int tim1    = 0U;

            if (sscanf(tail, ",%u,%u,%u", &attempt, &nbtrial, &tim1) == 3)
            {
                data.attempt   = (uint32_t) attempt;
                data.nbtrial   = (uint32_t) nbtrial;
                data.tim1      = (uint32_t) tim1;

                correct_parsed = true;
            }
            break;
        }

        case URC_KCNX_IND_CONNECTING:
        {
            unsigned int attempt = 0U;

            if (sscanf(tail, ",%u", &attempt) == 1)
            {
                data.attempt   = (uint32_t) attempt;
                correct_parsed = true;
            }
            break;
        }

        case URC_KCNX_IND_IDLE_TIME_DOWN_COUNT_FOR_DISCONNECT:
        {
            unsigned int idletime = 0U;

            if (sscanf(tail, ",%u", &idletime) == 1)
            {
                data.idletime  = (uint32_t) idletime;
                correct_parsed = true;
            }
            break;
        }

        case URC_KCNX_IND_CLOSED:
        case URC_KCNX_IND_IDLE_TIME_DOWN_COUNT_CANCEL:
        {
            correct_parsed = true;
            break;
        }

        default:
        {
            break;
        }
    }

    if (correct_parsed
        && (p_urc_handlers_table[URC_TYPE_KCNX_IND].handler != NULL))
    {
        p_urc_handlers_table[URC_TYPE_KCNX_IND].handler(&data);
    }
}

static void
parse_urc_ktcp_data (const char * p_line)
{
    urc_ktcp_data_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+KTCP_DATA: %u,%u", &data.session_id, &data.ndata) == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_KTCP_DATA].handler)
        {
            p_urc_handlers_table[URC_TYPE_KTCP_DATA].handler(&data);
        }
    }
}

static void
parse_urc_ktcp_ind (const char * p_line)
{
    urc_ktcp_ind_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+KTCP_IND: %u,%u", &data.session_id, &data.stat) == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_KTCP_IND].handler)
        {
            p_urc_handlers_table[URC_TYPE_KTCP_IND].handler(&data);
        }
    }
}

static void
parse_urc_ktcp_notif (const char * p_line)
{
    urc_ktcp_notif_type_t data;
    memset(&data, 0, sizeof(data));
    unsigned int tcp_notif = 0U;
    if (sscanf(p_line, "+KTCP_NOTIF: %u,%u", &data.session_id, &tcp_notif) == 2)
    {
        data.tcp_notif = (urc_ktcp_notif_conn_failure_type_t) tcp_notif;
        if (p_urc_handlers_table[URC_TYPE_KTCP_NOTIF].handler)
        {
            p_urc_handlers_table[URC_TYPE_KTCP_NOTIF].handler(&data);
        }
    }
}

static void
parse_urc_ktcp_srvreq (const char * p_line)
{
    urc_ktcp_srvreq_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line,
               "+KTCP_SRVREQ: %u,%u,\"%63[^\"]\",%u",
               &data.session_id,
               &data.subsession_id,
               data.client_ip,
               &data.client_port)
        == 4)
    {
        if (p_urc_handlers_table[URC_TYPE_KTCP_SRVREQ].handler)
        {
            p_urc_handlers_table[URC_TYPE_KTCP_SRVREQ].handler(&data);
        }
    }
}

static void
parse_urc_kudp_ind (const char * p_line)
{
    urc_kudp_ind_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+KUDP_IND: %u,%u", &data.session_id, &data.stat) == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_KUDP_IND].handler)
        {
            p_urc_handlers_table[URC_TYPE_KUDP_IND].handler(&data);
        }
    }
}

static void
parse_urc_kudp_notif (const char * p_line)
{
    urc_kudp_notif_type_t data;
    memset(&data, 0, sizeof(data));
    unsigned int udp_notif = 0U;
    if (sscanf(p_line, "+KUDP_NOTIF: %u,%u", &data.session_id, &udp_notif) == 2)
    {
        data.udp_notif = (urc_ktcp_notif_conn_failure_type_t) udp_notif;
        if (p_urc_handlers_table[URC_TYPE_KUDP_NOTIF].handler)
        {
            p_urc_handlers_table[URC_TYPE_KUDP_NOTIF].handler(&data);
        }
    }
}

static void
parse_urc_kudp_data (const char * p_line)
{
    urc_kudp_data_type_t data;
    memset(&data, 0, sizeof(data));
    if (sscanf(p_line, "+KUDP_DATA: %u,%u", &data.session_id, &data.ndata) == 2)
    {
        if (p_urc_handlers_table[URC_TYPE_KUDP_DATA].handler)
        {
            p_urc_handlers_table[URC_TYPE_KUDP_DATA].handler(&data);
        }
    }
}
