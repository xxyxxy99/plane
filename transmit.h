//
// Shared TX path for CLI and GUI.
//

#ifndef ACARSTRANS_TRANSMIT_H
#define ACARSTRANS_TRANSMIT_H

#include "pkt.h"
#include "acarstrans.h"

/**
 * Modulate \a mf must already be computed (merge_elements + modulate).
 * \a rf_repeat: AD936x uses libiio repeat flag; USRP loops bursts until stop when true.
 * CLI keeps USRP looping by passing true; GUI may pass false for a single burst.
 */
int transmit_run(message_format *mf, DEVICE dev, const char *device_arg, int64_t freq, uint32_t gain,
                 uint32_t samp_rate, uint32_t rf_bw, bool rf_repeat, const char *ad936_path);

/** Ask ongoing USRP or AD936x transmit to stop (for GUI Stop button / cleanup). */
void rf_tx_request_stop(void);

#endif
