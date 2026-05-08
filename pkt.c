//
// Created by jiaxv on 2022/10/27.
//

#include "pkt.h"

#include <stdio.h>


#define append_char(head, offset, src) \
    *(head+offset) = src;\
    offset++;

#define append_char_parity(head, offset, src) \
    *(head+offset) = parity_char(src);\
    offset++;

#define append_str(head, offset, src, n) \
    memcpy(head+offset, src, n);\
    offset += n;

#define append_str_parity(head, offset, src, n){\
    uint8_t tmp[256] = {0};\
    parity_str(tmp, src, n);\
    memcpy(head+offset, tmp, n);\
    offset += n;\
    } \


#define UPLINK_PRELEN (PREKEY_LEN + SYNC_LEN + SOH_LEN + MODE_LEN + ARN_LEN + ACK_LEN + LABEL_LEN + BI_LEN + STX_LEN)
#define DOWNLINK_PRELEN (PREKEY_LEN + SYNC_LEN + SOH_LEN + MODE_LEN + LABEL_LEN + ARN_LEN + BI_LEN + ACK_LEN + STX_LEN + SERIAL_LEN + FLIGHT_ID_LEN)
#define TAIL_LEN  (SUFFIX_LEN + BCS_LEN + BCS_SUF_LEN)



void parity_str(uint8_t *dst, const uint8_t *src, int msg_len) ;

uint8_t parity_char(uint8_t value);

void lsb(uint8_t *dst, const uint8_t *src, size_t len);

at_error num2bits(int num, uint8_t *str, int bits, bool is_lsb);

void merge_elements(message_format *u) {

    const int pre_len = u->is_uplink == true ? UPLINK_PRELEN : DOWNLINK_PRELEN;

    u->total_bits = (pre_len + u->text_len + TAIL_LEN) * 8;
    uint8_t *rawMsg = malloc(sizeof(uint8_t) * (pre_len + u->text_len + TAIL_LEN));

    int duplen = 0;
    bool is_ack = false;
    if(u->ack != NAK_TAG){
        is_ack = true;
        u->label[0] = 0x5F;
        u->label[1] = 0x7F;
        u->text[0] = 0x00;
        u->text_len = 0;
        u->crc[0] = 0x22;
        u->crc[1] = 0x33;
    }

    append_str(rawMsg, duplen, prekey, PREKEY_LEN);
    append_str_parity(rawMsg, duplen, sync_seq, SYNC_LEN);
    append_char_parity(rawMsg, duplen, SOH);
    append_char_parity(rawMsg, duplen, u->mode);
    append_str_parity(rawMsg, duplen, u->arn, ARN_LEN);
    append_char_parity(rawMsg, duplen, u->ack);
    append_str_parity(rawMsg, duplen, u->label, LABEL_LEN);
    append_char_parity(rawMsg, duplen, u->bi);

    if(!is_ack){
        append_char_parity(rawMsg, duplen, STX);
        if (!u->is_uplink) {
            append_str_parity(rawMsg, duplen, u->serial, SERIAL_LEN);
            append_str_parity(rawMsg, duplen, u->flight, FLIGHT_ID_LEN);
        }
        append_str_parity(rawMsg, duplen, u->text, u->text_len);
        append_char_parity(rawMsg, duplen, u->suffix);

        get_crc(rawMsg + PREKEY_LEN + SYNC_LEN + SOH_LEN, u->crc, duplen - (PREKEY_LEN + SYNC_LEN + SOH_LEN));
    }else{
        if(u->is_uplink == false){
            append_char_parity(rawMsg, duplen, STX);
            append_str_parity(rawMsg, duplen, u->serial, SERIAL_LEN);
            append_str_parity(rawMsg, duplen, u->flight, FLIGHT_ID_LEN);
        }
        append_char_parity(rawMsg, duplen, u->suffix);
    }
    append_str(rawMsg, duplen, u->crc, CRC_LEN);
    append_char_parity(rawMsg, duplen, DEL);

    lsb(u->lsb_with_crc_msg, rawMsg, u->total_bits);
    free(rawMsg);
}

void parity_str(uint8_t *dst, const uint8_t *src, int msg_len) {
    while (msg_len--) {
        *dst++ = parity_char(*src++);
    }
}

uint8_t parity_char(uint8_t value) {
    /* ACARS: odd parity over 7 data bits (MSB is parity bit). */
    const uint8_t d = (uint8_t)(value & 0x7Fu);
    unsigned count = 0;
    for (int i = 0; i < 7; i++) {
        if (d & (1u << i))
            count++;
    }
    return (count % 2u == 0u) ? (uint8_t)(d | 0x80u) : d;
}

void lsb(uint8_t *dst, const uint8_t *src, const size_t len) {
    for (int i = 0; i < len / BITS_PER_BYTE; i++) {
        uint8_t out[BITS_PER_BYTE];
        num2bits(src[i], out, BITS_PER_BYTE, true);
        memcpy(dst + i * BITS_PER_BYTE, out, 8);
    }
}

at_error mf_fill_manual(message_format *mf, const mf_manual_params *p, char *errbuf, size_t errbuf_len) {
#define MF_FAIL(...)                                                                                           \
    do {                                                                                                       \
        if (errbuf && errbuf_len)                                                                              \
            snprintf(errbuf, errbuf_len, __VA_ARGS__);                                                         \
        return AT_ERROR_INVALID_PARAM;                                                                         \
    } while (0)

    if (!mf || !p)
        return AT_ERROR_NULL;

    memset(mf, 0, sizeof(*mf));

    if (p->mode != '2')
        MF_FAIL("Mode must be '2' (Mode A).");

    if (!p->arn_input)
        MF_FAIL("Address is required.");

    if (regex_string("[A-Z0-9.-]", p->arn_input) != AT_OK)
        MF_FAIL("Address may only contain A-Z, 0-9, '.' and '-'.");

    const size_t alen = strlen(p->arn_input);
    if (alen > (size_t)ARN_LEN)
        MF_FAIL("Address too long.");

    for (int r = 0; r < ARN_LEN; r++)
        mf->arn[r] = '.';
    memcpy(mf->arn + ARN_LEN - alen, p->arn_input, alen);
    mf->arn[ARN_LEN] = '\0';

    mf->is_uplink = p->is_uplink;
    mf->mode       = p->mode;

    if (!p->ack_input || p->ack_input[0] == '\0') {
        mf->ack = NAK_TAG;
    } else {
        if (strlen(p->ack_input) != 1)
            MF_FAIL("ACK must be empty (NAK) or exactly one character.");
        const char c = p->ack_input[0];
        if (p->is_uplink) {
            if (regex_char("[0-9]", c) != AT_OK)
                MF_FAIL("Uplink ACK must be 0-9 or NAK.");
        } else {
            if (regex_char("[A-Za-z]", c) != AT_OK)
                MF_FAIL("Downlink ACK must be A-Z / a-z or NAK.");
        }
        mf->ack = (uint8_t)c;
    }

    if (!p->label || strlen(p->label) != 2)
        MF_FAIL("Label must be exactly two characters.");
    if (regex_string("[A-Z0-9]{2}", p->label) != AT_OK)
        MF_FAIL("Label must be two A-Z or 0-9 characters.");
    memcpy(mf->label, p->label, 2);
    mf->label[2] = '\0';

    if (!p->bi_input || p->bi_input[0] == '\0') {
        mf->bi = NAK_TAG;
    } else {
        if (strlen(p->bi_input) != 1)
            MF_FAIL("BI must be empty (NAK) or one character.");
        const char b = p->bi_input[0];
        if (p->is_uplink) {
            if (regex_char("[A-Za-z]", b) != AT_OK)
                MF_FAIL("Uplink BI must be A-Za-z or NAK.");
        } else {
            if (regex_char("[0-9]", b) != AT_OK)
                MF_FAIL("Downlink BI must be 0-9 or NAK.");
        }
        mf->bi = (uint8_t)b;
    }

    if (!p->text)
        MF_FAIL("TEXT is required (may be empty string).");
    const size_t tlen = strlen(p->text);
    if (tlen > TEXT_MAX_LEN)
        MF_FAIL("TEXT too long.");
    memcpy(mf->text, p->text, tlen);
    mf->text[tlen]    = '\0';
    mf->text_len      = tlen;

    if (!p->is_uplink) {
        if (!p->flight_id || strlen(p->flight_id) != (size_t)FLIGHT_ID_LEN)
            MF_FAIL("Downlink requires Flight ID of exactly six characters.");
        if (regex_string("[A-Z0-9]{6}", p->flight_id) != AT_OK)
            MF_FAIL("Flight ID must be six A-Z or 0-9 characters.");
        memcpy(mf->flight, p->flight_id, FLIGHT_ID_LEN);
        mf->flight[FLIGHT_ID_LEN] = '\0';
        if (!p->serial_input || strlen(p->serial_input) != (size_t)SERIAL_LEN)
            MF_FAIL("Downlink requires Msg No (serial) of exactly four characters.");
        if (regex_string("[A-Z0-9]{4}", p->serial_input) != AT_OK)
            MF_FAIL("Msg No must be four A-Z or 0-9 characters.");
        memcpy(mf->serial, p->serial_input, SERIAL_LEN);
        mf->serial[SERIAL_LEN] = '\0';
    }

    mf->suffix = ETX;
    return AT_OK;
#undef MF_FAIL
}

int generate_pkt(message_format *mf) {
    bool is_uplink = false;
    while (true) {
        uint8_t isUplink[2] = {0};
        get_input(LIGHT_GREEN"是否为上行链路（Uplink）[Y/N]: "RESET_COLOR, 1, isUplink);

        if (regex_char("[YyNn]", *isUplink) == AT_OK) {
            is_uplink = (*isUplink == 'Y' || *isUplink == 'y');
            break;
        }
    }

    uint8_t mode_c = '2';
    while (true) {
        uint8_t mode[MODE_LEN + 1] = {0};
        get_input(LIGHT_GREEN"模式（Mode，仅支持 A 类填 2）: "RESET_COLOR, MODE_LEN, mode);
        if (*mode == '2') {
            mode_c = *mode;
            break;
        }
        printf(LIGHT_RED"仅支持 Mode A，请输入字符 '2'。\n"RESET_COLOR);
    }

    uint8_t temp_arn[ARN_LEN + 1] = {0};
    while (true) {
        get_input(LIGHT_GREEN"飞机地址（Address，≤7 位）: "RESET_COLOR, ARN_LEN, temp_arn);

        if (regex_string("[A-Z0-9.-]", temp_arn) == AT_OK) {
            break;
        }
        printf(LIGHT_RED"允许字符：A-Z、0-9、'.'、'-'。\n"RESET_COLOR);
    }

    char ack_str[8] = {0};
    while (true) {
        uint8_t ack[ACK_LEN + 1] = {0};
        get_input(LIGHT_GREEN"确认字符（ACK），直接回车表示 NAK: "RESET_COLOR, ACK_LEN, ack);

        if (!strlen((char *)ack)) {
            break;
        }

        if (is_uplink) {
            if (regex_char("[0-9]", *ack) == AT_OK) {
                ack_str[0] = (char)*ack;
                ack_str[1] = '\0';
                break;
            }
            printf(LIGHT_RED"上行 ACK 须为 0-9，或留空表示 NAK。\n"RESET_COLOR);
        } else {
            if (regex_char("[A-Za-z]", *ack) == AT_OK) {
                ack_str[0] = (char)*ack;
                ack_str[1] = '\0';
                break;
            }
            printf(LIGHT_RED"下行 ACK 须为 A-Z / a-z，或留空表示 NAK。\n"RESET_COLOR);
        }
    }

    char label_buf[LABEL_LEN + 1] = {0};
    while (true) {
        get_input(LIGHT_GREEN"报文标签（Label，2 位）: "RESET_COLOR, LABEL_LEN, (uint8_t *)label_buf);

        if (regex_string("[A-Z0-9]{2}", label_buf) == AT_OK) {
            break;
        }
        printf(LIGHT_RED"Label 须为 2 个 A-Z 或 0-9。\n"RESET_COLOR);
    }

    char bi_str[8] = {0};
    while (true) {
        uint8_t bi[BI_LEN + 1] = {0};
        if (is_uplink) {
            get_input(LIGHT_GREEN"上行块标识（BI），回车=NAK: "RESET_COLOR, BI_LEN, bi);
            if (!strlen((char *)bi)) {
                break;
            }
            if (regex_char("[A-Za-z]", *bi) == AT_OK) {
                bi_str[0] = (char)*bi;
                bi_str[1] = '\0';
                break;
            }
            printf(LIGHT_RED"上行 BI 须为 A-Za-z，或留空表示 NAK。\n"RESET_COLOR);
        } else {
            get_input(LIGHT_GREEN"下行块标识（BI，数字 0-9），回车=NAK: "RESET_COLOR, BI_LEN, bi);
            if (!strlen((char *)bi)) {
                break;
            }
            if (regex_char("[0-9]", *bi) == AT_OK) {
                bi_str[0] = (char)*bi;
                bi_str[1] = '\0';
                break;
            }
            printf(LIGHT_RED"下行 BI 须为 0-9（与 acarsdec 下行判别一致），或留空表示 NAK。\n"RESET_COLOR);
        }
    }

    uint8_t text_buf[TEXT_MAX_LEN + 1] = {0};
    get_input(LIGHT_GREEN"报文正文（TEXT）: "RESET_COLOR, TEXT_MAX_LEN, text_buf);

    char flight_buf[FLIGHT_ID_LEN + 1] = {0};
    char serial_buf[SERIAL_LEN + 1] = {0};
    if (!is_uplink) {
        while (true) {
            get_input(LIGHT_GREEN"航班号（Flight ID，6 位，仅下行）: "RESET_COLOR, FLIGHT_ID_LEN, (uint8_t *)flight_buf);
            if (regex_string("[A-Z0-9]{6}", flight_buf) == AT_OK) {
                break;
            }
            printf(LIGHT_RED"Flight ID 须为 6 位 A-Z 或 0-9。\n"RESET_COLOR);
        }
        while (true) {
            get_input(LIGHT_GREEN"报文序号（MsgNo，4 位，仅下行）: "RESET_COLOR, SERIAL_LEN, (uint8_t *)serial_buf);
            if (regex_string("[A-Z0-9]{4}", serial_buf) == AT_OK) {
                break;
            }
            printf(LIGHT_RED"MsgNo 须为 4 位 A-Z 或 0-9（例如 M95A）。\n"RESET_COLOR);
        }
    }

    const mf_manual_params mp = {.is_uplink    = is_uplink,
                                 .mode         = mode_c,
                                 .arn_input    = (const char *)temp_arn,
                                 .ack_input    = ack_str,
                                 .label        = label_buf,
                                 .bi_input     = bi_str,
                                 .text         = (const char *)text_buf,
                                 .flight_id    = is_uplink ? "" : flight_buf,
                                 .serial_input = is_uplink ? "" : serial_buf};

    return mf_fill_manual(mf, &mp, NULL, 0) == AT_OK ? AT_OK : AT_ERROR_INVALID_PARAM;
}

static void mf_set_defaults(message_format *mf) {
    memset(mf, 0, sizeof(*mf));
    mf->is_uplink = false;
    mf->mode = '2'; // Mode A
    memset(mf->arn, '.', ARN_LEN);
    mf->arn[ARN_LEN] = '\0';
    mf->ack = NAK_TAG;
    memcpy(mf->label, "00", 2);
    mf->label[2] = '\0';
    mf->bi = NAK_TAG;
    memcpy(mf->serial, "M01A", SERIAL_LEN);
    mf->serial[SERIAL_LEN] = '\0';
    memcpy(mf->flight, "TEST01", FLIGHT_ID_LEN);
    mf->flight[FLIGHT_ID_LEN] = '\0';
    mf->suffix = ETX;
}

at_error generate_pkt_from_file(message_format *mf, const char *path) {
    if (!mf || !path || !path[0]) return AT_ERROR_NULL;

    FILE *f = fopen(path, "rb");
    if (!f) return AT_ERROR_INVALID_FILE;

    mf_set_defaults(mf);

    uint8_t buf[TEXT_MAX_LEN + 1] = {0};
    const size_t n = fread(buf, 1, TEXT_MAX_LEN, f);
    fclose(f);

    // Normalize newlines; keep printable bytes as-is.
    size_t out = 0;
    for (size_t i = 0; i < n && out < TEXT_MAX_LEN; i++) {
        if (buf[i] == '\r' || buf[i] == '\n') continue;
        mf->text[out++] = buf[i];
    }
    mf->text[out] = '\0';
    mf->text_len = out;
    return AT_OK;
}

