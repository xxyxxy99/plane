
#include "pkt.h"
#include "modulator.h"
#include "acarstrans.h"
#include "transmit.h"
#include "util.h"



void show_version() {
    printf("Acars Simulator version %.1f\n", VERSION);
    printf("Copyright (C) 2022-2026 Civil Aviation University of China (CAUC).\n");
    printf("This is free software, and you are welcome to redistribute it.\n");
}

void usage() {
    printf("Usage:\n");
    printf("\t-h # this help\n");
    printf("\t-v show software version.\n");
    printf("\t-d device_arg # AD936x IIO URI (examples: ip:192.168.2.1, usb:1.2.3) or empty for local.\n");
    printf("\t-F <filename> # Transmit from data file (use '-' for manually input).\n");
    printf("\t-f freq_hz # Frequency in Hz [127.0MHz to 139.0MHz] (default is 131450000Hz / 131.450Hz).\n");
    printf("\t-x gain_db # TX VGA (IF) gain, 0-47dB, 1dB steps (default is 20).\n");
    printf("\t-r samp_rate_hz # Sample rate in Hz (AD936x only, default is %d).\n", SAMP_RATE);
    printf("\t-b rf_bw_hz # RF bandwidth in Hz (AD936x only, default is samp_rate).\n");
    printf("\t[-R] # Repeat TX mode (default is off).\n");
    printf("\t[-U] # Using USRP (if built with UHD)\n");
    printf("\t[-A] # Using AD936x (AD9361/AD9363 via libiio, default)\n");
};

int main(int argc, char **argv) {
    // test();
    // exit(0);

    int mode = MANUAL_MODE;
    DEVICE dev = AD936X;

    int64_t freq = 131450000;
    uint32_t gain = 30;
    uint32_t samp_rate = SAMP_RATE;
    uint32_t rf_bw = 0;
    message_format *mf = malloc(sizeof(message_format));
    char device_arg[128] = {0};
    char path[256] = {0};
    bool is_repeat = false;

    int c;
    while ((c = getopt(argc, argv, "d:f:x:F:r:b:RvhUA")) != EOF) {
        switch (c) {
            case 'U':
                dev = USRP;
                break;
            case 'A':
                dev = AD936X;
                break;
            case 'd':
                snprintf(device_arg, sizeof(device_arg), "%s", optarg);
                break;
            case 'F':
                snprintf(path, sizeof(path), "%s", optarg);
                if(strcmp(path, "-") != 0){
                    mode = FILE_MODE;
                } else{
                    mode = MANUAL_MODE;
                }
                break;
            case 'f':
                parse_frequency_i64(optarg, NULL, &freq);
                break;
            case 'x':
                parse_u32(optarg, &gain);
                break;
            case 'r':
                parse_u32(optarg, &samp_rate);
                break;
            case 'b':
                parse_u32(optarg, &rf_bw);
                break;
            case 'R':
                is_repeat = true;
                break;
            case 'v':
                show_version();
                return 0;
            case 'h':
                usage();
                return 0;
            default:
                show_version();
                usage();
                return 0;
        }
    }

    if (mode == MANUAL_MODE) {
        generate_pkt(mf);
        merge_elements(mf);
        modulate(mf);
    } else if (mode == FILE_MODE) {
        if (generate_pkt_from_file(mf, path) != AT_OK) {
            fprintf(stderr, "Failed to generate ACARS packet from file: %s\n", path);
            free(mf);
            return 1;
        }
        merge_elements(mf);
        modulate(mf);
    }

    /* USRP: keep infinite burst loop until Ctrl+C (same as before). AD936x: honor -R. */
    const bool rf_repeat = (dev == USRP) ? true : is_repeat;
    transmit_run(mf, dev, device_arg, freq, gain, samp_rate, rf_bw, rf_repeat, path);

    free(mf);

    return 0;
}
