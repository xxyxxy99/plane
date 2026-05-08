//
// Created by jiaxv on 2026/2/1.
//

#ifndef ACARSTRANS_USRP_H
#define ACARSTRANS_USRP_H
#include "pkt.h"

#ifdef HAVE_UHD
#include <uhd.h>

typedef struct {
    char *device_args;
    bool is_repeat;
    uint32_t gain;
    int64_t freq;
    double rate;

    float data[SAMPLE_MAX_LEN * 2];
    /** IQ samples per burst (equals mf->complex_length after usrp_transfer_data). */
    size_t tx_num_samps;

    size_t channel;
    uhd_usrp_handle usrp;
    uhd_tx_streamer_handle tx_streamer;
    uhd_tx_metadata_handle md;
    uhd_tune_request_t tune_request;
    uhd_stream_args_t stream_args;
    uhd_tune_result_t tune_result;
}usrp_args_t;

void usrp_transfer_data(usrp_args_t *args, const message_format *mf);

at_error usrp_transmit(usrp_args_t *args);

/** Interrupt an ongoing USRP TX loop (GNU Radio-style stop). */
void usrp_request_stop(void);

void test();

#else

// UHD not available at build time; keep API surface but disable implementation.
typedef struct {
    char *device_args;
    bool is_repeat;
    uint32_t gain;
    int64_t freq;
    double rate;
    float data[SAMPLE_MAX_LEN * 2];
    size_t tx_num_samps;
} usrp_args_t;

void usrp_transfer_data(usrp_args_t *args, const message_format *mf);
at_error usrp_transmit(usrp_args_t *args);

void usrp_request_stop(void);

#endif


#endif //ACARSTRANS_USRP_H