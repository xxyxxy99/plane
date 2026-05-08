//
// AD936x (AD9361/AD9363) TX backend via libiio
//

#include "ad936x.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <stddef.h>

#ifdef HAVE_LIBIIO
  #include <iio.h>
#endif

static volatile bool g_stop = false;

static void sigint_handler(int code) {
    (void)code;
    g_stop = true;
}

static int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void ad936x_transfer_data(ad936x_args_t *args, const message_format *mf) {
    if (!args || !mf) return;

    // mf->out_* are in roughly [-128, 128] range (see hackrf/usrp scaling).
    // Convert to sc16, keep some headroom.
    const size_t n = (mf->complex_length > 0) ? (size_t)mf->complex_length : 0;
    args->num_samples = (n > SAMPLE_MAX_LEN) ? SAMPLE_MAX_LEN : n;

    const float scale = 256.0f; // 128 * 256 = 32768
    for (size_t i = 0; i < args->num_samples; i++) {
        const int32_t i32 = (int32_t)lroundf(mf->out_r[i] * scale);
        const int32_t q32 = (int32_t)lroundf(mf->out_i[i] * scale);
        args->data[i * 2]     = clamp_i16(i32);
        args->data[i * 2 + 1] = clamp_i16(q32);
    }
}

#ifdef HAVE_LIBIIO

static int iio_attr_write_ll(struct iio_channel *chn, const char *attr, long long val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", val);
    return iio_channel_attr_write(chn, attr, buf);
}

static int iio_attr_write_long(struct iio_channel *chn, const char *attr, long val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", val);
    return iio_channel_attr_write(chn, attr, buf);
}

static struct iio_device *find_dev(struct iio_context *ctx, const char *name) {
    struct iio_device *dev = iio_context_find_device(ctx, name);
    return dev;
}

static int configure_ad936x_tx(
    struct iio_context *ctx,
    const ad936x_args_t *args,
    struct iio_device **out_tx_dev,
    struct iio_channel **out_tx_i,
    struct iio_channel **out_tx_q
) {
    // Common naming across Pluto/AD936x IIO:
    // - PHY: "ad9361-phy"
    // - TX streaming: "cf-ad9361-dds-core-lpc" (Pluto) or "axi-ad9361-tx-hpc"/"axi-ad9361-tx-lpc"
    struct iio_device *phy = find_dev(ctx, "ad9361-phy");
    if (!phy) {
        fprintf(stderr, "AD936x: cannot find device \"ad9361-phy\" in IIO context.\n");
        return -1;
    }

    struct iio_device *tx = find_dev(ctx, "cf-ad9361-dds-core-lpc");
    if (!tx) tx = find_dev(ctx, "axi-ad9361-tx-hpc");
    if (!tx) tx = find_dev(ctx, "axi-ad9361-tx-lpc");
    if (!tx) {
        fprintf(stderr, "AD936x: cannot find TX streaming device (tried cf/axi ad9361 tx cores).\n");
        return -1;
    }

    struct iio_channel *lo = iio_device_find_channel(phy, "altvoltage1", true);
    if (!lo) lo = iio_device_find_channel(phy, "altvoltage0", true);
    if (!lo) {
        fprintf(stderr, "AD936x: cannot find TX LO channel altvoltage{0,1}.\n");
        return -1;
    }

    // LO frequency
    if (iio_attr_write_ll(lo, "frequency", (long long)args->freq_hz) < 0) {
        fprintf(stderr, "AD936x: failed to set LO frequency.\n");
        return -1;
    }

    // TX baseband channel attributes are on phy: voltage0/voltage1 (output = true)
    struct iio_channel *tx0 = iio_device_find_channel(phy, "voltage0", true);
    struct iio_channel *tx1 = iio_device_find_channel(phy, "voltage1", true);
    if (!tx0 || !tx1) {
        fprintf(stderr, "AD936x: cannot find TX phy channels voltage0/voltage1.\n");
        return -1;
    }

    // sample rate / RF bandwidth (Hz)
    if (iio_attr_write_long(tx0, "sampling_frequency", (long)args->sample_rate_hz) < 0 ||
        iio_attr_write_long(tx1, "sampling_frequency", (long)args->sample_rate_hz) < 0) {
        fprintf(stderr, "AD936x: failed to set sampling_frequency.\n");
        return -1;
    }

    if (args->rf_bw_hz) {
        if (iio_attr_write_long(tx0, "rf_bandwidth", (long)args->rf_bw_hz) < 0 ||
            iio_attr_write_long(tx1, "rf_bandwidth", (long)args->rf_bw_hz) < 0) {
            fprintf(stderr, "AD936x: failed to set rf_bandwidth.\n");
            return -1;
        }
    }

    // gain control
    // Many AD936x expose: "hardwaregain" (dB) and "gain_control_mode" = "manual"
    (void)iio_channel_attr_write(tx0, "gain_control_mode", "manual");
    (void)iio_channel_attr_write(tx1, "gain_control_mode", "manual");
    {
        char gbuf[32];
        snprintf(gbuf, sizeof(gbuf), "%d", (int)args->gain_db);
        (void)iio_channel_attr_write(tx0, "hardwaregain", gbuf);
        (void)iio_channel_attr_write(tx1, "hardwaregain", gbuf);
    }

    // Streaming channels on tx core: typically "voltage0"/"voltage1" (output = true)
    struct iio_channel *s_i = iio_device_find_channel(tx, "voltage0", true);
    struct iio_channel *s_q = iio_device_find_channel(tx, "voltage1", true);
    if (!s_i || !s_q) {
        fprintf(stderr, "AD936x: cannot find streaming channels voltage0/voltage1 on TX core.\n");
        return -1;
    }

    iio_channel_enable(s_i);
    iio_channel_enable(s_q);

    *out_tx_dev = tx;
    *out_tx_i = s_i;
    *out_tx_q = s_q;
    return 0;
}

static void print_ad936x_settings(const ad936x_args_t *args) {
    fprintf(stderr, "AD936x Settings:\n");
    fprintf(stderr, "URI: %s\n", (args->uri && args->uri[0]) ? args->uri : "(local)");
    fprintf(stderr, "Frequency: %.6f MHz\n", (double)args->freq_hz / 1e6);
    fprintf(stderr, "Sample rate: %u Hz\n", args->sample_rate_hz);
    fprintf(stderr, "RF bandwidth: %u Hz\n", args->rf_bw_hz);
    fprintf(stderr, "TX gain: %d dB\n", (int)args->gain_db);
    fprintf(stderr, "Repeat: %d\n\n", args->is_repeat);
}

int ad936x_transmit(const ad936x_args_t *args) {
    if (!args) return EXIT_FAILURE;
    if (args->num_samples == 0) {
        fprintf(stderr, "AD936x: no samples to transmit.\n");
        return EXIT_FAILURE;
    }

    print_ad936x_settings(args);
    {
        double burst_ms = (args->sample_rate_hz > 0)
            ? (1000.0 * (double)args->num_samples / (double)args->sample_rate_hz)
            : 0.0;
        fprintf(stderr,
                "IQ burst: %zu complex samples (~%.2f ms @ %u Hz). %s\n",
                args->num_samples, burst_ms, args->sample_rate_hz,
                args->is_repeat
                    ? "Repeating until Ctrl+C (-R)."
                    : "Single shot only — start acarsdec BEFORE launch or use -R.");
    }

    struct iio_context *ctx = NULL;
    if (args->uri && args->uri[0]) ctx = iio_create_context_from_uri(args->uri);
    else ctx = iio_create_default_context();

    if (!ctx) {
        fprintf(stderr, "AD936x: failed to create IIO context.\n");
        return EXIT_FAILURE;
    }

    struct iio_device *tx_dev = NULL;
    struct iio_channel *tx_i = NULL;
    struct iio_channel *tx_q = NULL;
    if (configure_ad936x_tx(ctx, args, &tx_dev, &tx_i, &tx_q) != 0) {
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    // Allocate TX buffer in samples (complex)
    struct iio_buffer *buf = iio_device_create_buffer(tx_dev, (size_t)args->num_samples, false);
    if (!buf) {
        fprintf(stderr, "AD936x: failed to create TX buffer.\n");
        iio_context_destroy(ctx);
        return EXIT_FAILURE;
    }

    signal(SIGINT, &sigint_handler);
    signal(SIGTERM, &sigint_handler);

    fprintf(stderr, "Start transmitting... Stop with Ctrl-C\n");

    do {
        // Interleaved sc16 layout is typical; libiio will interleave enabled channels.
        // We memcpy into buffer start; for most AD936x TX cores this is correct.
        void *p = iio_buffer_start(buf);
        const ptrdiff_t step = iio_buffer_step(buf);
        const ptrdiff_t sample_stride = step; // bytes per sample across enabled channels

        // Conservative: write using stepping so we don't assume exact interleave layout.
        for (size_t n = 0; n < args->num_samples; n++) {
            char *s = (char *)p + (ptrdiff_t)n * sample_stride;
            // voltage0 (I) then voltage1 (Q) packed as int16 each
            memcpy(s, &args->data[n * 2], sizeof(int16_t));
            memcpy(s + sizeof(int16_t), &args->data[n * 2 + 1], sizeof(int16_t));
        }

        const ssize_t pushed = iio_buffer_push(buf);
        if (pushed < 0) {
            fprintf(stderr, "AD936x: buffer push failed (%zd).\n", pushed);
            break;
        }

        if (!args->is_repeat) break;
    } while (!g_stop);

    iio_buffer_destroy(buf);
    iio_context_destroy(ctx);
    return EXIT_SUCCESS;
}

#else

int ad936x_transmit(const ad936x_args_t *args) {
    (void)args;
    fprintf(stderr, "AD936x backend was not built (libiio not found). Rebuild with libiio installed.\n");
    return EXIT_FAILURE;
}

#endif

void ad936x_request_stop(void) {
#ifdef HAVE_LIBIIO
    g_stop = true;
#else
    (void)0;
#endif
}

