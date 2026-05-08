//
// Created by jiaxv on 2022/10/27.
//

#ifndef ACARS_SIM_C_GENMSG_H
#define ACARS_SIM_C_GENMSG_H

#include <stddef.h>

#include "acarstrans.h"
#include "util.h"

const static char prekey[16] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                0x01, 0x01};
const static uint8_t sync_seq[4] = {0x2b, 0x2a, 0x16, 0x16};
const static char SOH = 0x01;
const static char STX = 0x02;
const static char ETX = 0x03;
const static char ETB = 0x17;
const static char DEL = 0X7F;
#define NAK_TAG 0x15

#define PREKEY_LEN 16
#define SYNC_LEN  4
#define SOH_LEN  1
#define MODE_LEN 1
#define ARN_LEN  7
#define ACK_LEN  1
#define LABEL_LEN 2
#define BI_LEN  1
#define STX_LEN  1
#define SERIAL_LEN  4
#define FLIGHT_ID_LEN  6
#define SUFFIX_LEN 1
#define BCS_LEN 2
#define BCS_SUF_LEN 1
#define TEXT_MAX_LEN 220
#define CRC_LEN 2

#define SAMPLE_MAX_LEN (BAUD * F_S * F_S_2 * RESAMPLE) // 等于SAMP_RATE 1152000


typedef struct message_format {
    bool is_uplink;
    uint8_t mode;
    uint8_t arn[ARN_LEN + 1];
    uint8_t ack;
    uint8_t label[LABEL_LEN + 1];
    uint8_t bi;
    uint8_t serial[SERIAL_LEN + 1];
    uint8_t flight[FLIGHT_ID_LEN + 1];
    uint8_t text[TEXT_MAX_LEN + 1];
    uint8_t crc[CRC_LEN];
    uint8_t suffix;
    size_t text_len;
    uint8_t lsb_with_crc_msg[MAX_BUFFER_LEN << 3];
    size_t total_bits;
    int complex_length;
    float *cpfsk;
    // char complex_i8[SAMPLE_MAX_LEN * 2]; //signed char  <--> __int_8
    float out_r[SAMPLE_MAX_LEN];
    float out_i[SAMPLE_MAX_LEN];
} message_format;


void merge_elements(message_format *);

int generate_pkt(message_format *mf);

/** Parameters for building a manual-mode ACARS frame (CLI or GUI). */
typedef struct mf_manual_params {
    bool is_uplink;
    uint8_t mode;           /**< Mode character; must be '2' (Mode A). */
    const char *arn_input;  /**< Aircraft reg / address (see interactive rules). */
    const char *ack_input;  /**< Empty string for NAK, else one character. */
    const char *label;      /**< Exactly two [A-Z0-9] characters. */
    const char *bi_input;   /**< Empty for NAK; else one character per link direction rules. */
    const char *text;       /**< Message text (may be empty). */
    const char *flight_id;  /**< Downlink only: six [A-Z0-9] characters. Ignored if uplink. */
    const char *serial_input; /**< Downlink only: four [A-Z0-9] (Msg No). Ignored if uplink. */
} mf_manual_params;

/**
 * Fill \a mf from validated manual parameters (parity, CRC, LSB prep done by merge_elements later).
 * Optional \a errbuf receives a short English reason when validation fails.
 */
at_error mf_fill_manual(message_format *mf, const mf_manual_params *p, char *errbuf, size_t errbuf_len);

// Build a single ACARS packet from a text file (file content becomes TEXT).
// This keeps the on-air framing consistent with manual mode.
at_error generate_pkt_from_file(message_format *mf, const char *path);

#endif //ACARS_SIM_C_GENMSG_H
