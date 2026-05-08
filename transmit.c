//
// Shared transmit path for CLI (`acars_sim`) and GUI (`acars_gui`).
//

#include "transmit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ad936x.h"
#include "usrp.h"

int transmit_run(message_format *mf, DEVICE dev, const char *device_arg, int64_t freq, uint32_t gain,
                 uint32_t samp_rate, uint32_t rf_bw, bool rf_repeat, const char *ad936_path) {
    if (!mf)
        return 1;

    switch (dev) {
        case USRP:
#ifdef HAVE_UHD
        {
            usrp_args_t *usrp = calloc(1, sizeof(*usrp));
            if (!usrp)
                return 1;

            const char *da          = (device_arg && device_arg[0]) ? device_arg : "";
            usrp->device_args       = (char *)da;
            usrp->channel           = 0;
            usrp->gain              = gain;
            usrp->freq              = freq;
            usrp->rate              = SAMP_RATE;
            usrp->is_repeat         = rf_repeat;

            usrp->tune_request.target_freq     = (double)freq;
            usrp->tune_request.rf_freq_policy  = UHD_TUNE_REQUEST_POLICY_AUTO;
            usrp->tune_request.dsp_freq_policy = UHD_TUNE_REQUEST_POLICY_AUTO;

            usrp->stream_args.cpu_format    = "fc32";
            usrp->stream_args.otw_format      = "sc16";
            usrp->stream_args.args            = "";
            usrp->stream_args.channel_list    = &usrp->channel;
            usrp->stream_args.n_channels      = 1;

            usrp_transfer_data(usrp, mf);
            const at_error te = usrp_transmit(usrp);
            free(usrp);
            return (te == AT_OK) ? 0 : 1;
        }
#else
            fprintf(stderr, "USRP backend was not built (UHD not found).\n");
            return 1;
#endif

        case AD936X: {
            ad936x_args_t ad = {0};
            ad.uri            = device_arg ? device_arg : "";
            ad.path           = (char *)(ad936_path ? ad936_path : "");

            ad.is_repeat      = rf_repeat;
            ad.gain_db        = (int32_t)gain;
            ad.freq_hz        = freq;
            ad.sample_rate_hz = samp_rate;
            ad.rf_bw_hz       = rf_bw ? rf_bw : samp_rate;

            ad936x_transfer_data(&ad, mf);
            return ad936x_transmit(&ad);
        }

        default:
            return 1;
    }
}

void rf_tx_request_stop(void) {
    usrp_request_stop();
    ad936x_request_stop();
}
