/**
 * PCD T31 - DSP Processing types and public API
 */
#ifndef PROCESSING_H
#define PROCESSING_H

#include "server.h"

#define DEFAULT_SR          22050
#define DEFAULT_N_FFT       2048
#define DEFAULT_HOP_LENGTH  512
#define DEFAULT_N_MELS      128

typedef struct {
    char input_path[MAX_PATH_LEN];
    char output_path[MAX_PATH_LEN];
    int  target_sr;
    int  n_fft;
    int  hop_length;
    int  n_mels;
} proc_request_t;

typedef struct {
    int    status;
    char   error_msg[256];
    char   output_path[MAX_PATH_LEN];
    int    sample_rate;
    int    n_samples;
    double duration_s;
    int    n_mels;
    int    n_frames;
} proc_result_t;

int process_spectrogram(const proc_request_t*req, proc_result_t*res);

#endif /* PROCESSING_H */