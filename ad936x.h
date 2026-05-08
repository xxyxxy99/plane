//
// AD936x (AD9361/AD9363) TX backend via libiio
//

#ifndef ACARSTRANS_AD936X_H
#define ACARSTRANS_AD936X_H

#include <stdint.h>
#include <stdbool.h>
#include "pkt.h"

typedef struct {
    bool is_repeat;
    const char *uri;     // IIO context URI, e.g. "ip:192.168.2.1" or "usb:1.2.3" (may be "")
    char *path;          // optional IQ file path

    int32_t gain_db;     // TX hardware gain in dB (interpretation depends on device)
    int64_t freq_hz;     // TX LO frequency
    uint32_t sample_rate_hz;
    uint32_t rf_bw_hz;

    int16_t data[SAMPLE_MAX_LEN * 2]; // interleaved I,Q (sc16)
    size_t num_samples;               // complex samples in data[]
} ad936x_args_t;

void ad936x_transfer_data(ad936x_args_t *args, const message_format *mf);
int ad936x_transmit(const ad936x_args_t *args);

/** Stop repeating AD936x TX early (Ctrl+C equivalent). */
void ad936x_request_stop(void);

#endif //ACARSTRANS_AD936X_H

