/**
 * @fisier procesare.cpp
 * @pe scurt PCD T31 - spectrograma procesare using LibrosaCpp (ewan-xu/LibrosaCpp)
 *
 * Compilat cu g++ deoarece LibrosaCpp este C++ (Eigen3).
 * Toate apelurile sistem raman POSIX: deschide/scrie/inchide/fork/pipe/asteapta.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <unistd.h>
/* pentru functii POSIX de sistem */
#include <fcntl.h>
/* pentru controlul descriptorilor de fisiere */
#include <sys/types.h>
/* pentru tipuri de date sistem */
#include <sys/stat.h>
/* pentru informatii despre fisiere */
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */

/* LibrosaCpp - antet only, C++ cu Eigen3 */
#include <librosa/librosa.h>

extern "C" {
#include "processing.h"
/* pentru functii de procesare audio */
#include "server.h"
/* pentru definitii ale serverului */
}

#define DIR_PERMISSIONS 0755

/* Citire WAV simplu via sndfile sau fallback manual                   */
/* Folosim librosa::incarca daca exista, altfel citim WAV manual          */

/* Citire WAV PCM 16-bit mono/stereo - fallback fara dependente extra */
static int read_wav_samples(const char*path, float **out_samples,
                             int*out_n, int*out_sr)
{
    FILE*f=fopen(path, "rb");
    if (!f) { return -1; }

    /* WAV antet - 44 bytes standard */
    uint8_t hdr[44];
    if (fread(hdr, 1, 44, f)!=44) { fclose(f); return -1; }

    /* Verifica "RIFF" si "WAVE" */
    if (hdr[0]!='R'||hdr[1]!='I'||hdr[2]!='F'||hdr[3]!='F') {
        fclose(f); return -1;
    }

    int channels=hdr[22] | (hdr[23]<<8);
    int sr=hdr[24] | (hdr[25]<<8) | (hdr[26]<<16) | (hdr[27]<<24);
    int bits=hdr[34] | (hdr[35]<<8);
    int data_size=hdr[40] | (hdr[41]<<8) | (hdr[42]<<16) | (hdr[43]<<24);

    if (bits!=16) { fclose(f); return -1; }

    int total_samples=data_size/2; /* 16-bit = 2 bytes per esantion */
    int n_frames=total_samples/channels;

    int16_t*raw=(int16_t*)malloc(total_samples*sizeof(int16_t));
    if (!raw) { fclose(f); return -1; }

    fread(raw, 2, total_samples, f);
    fclose(f);

    /* Converteste la float mono [-1, 1] */
    float*samples=(float*)malloc(n_frames*sizeof(float));
    if (!samples) { free(raw); return -1; }

    for (int i=0; i < n_frames; i++) {
        float s=0.f;
        for (int c=0; c < channels; c++)
            s +=raw[i*channels+c]/32768.f;
        samples[i]=s/channels;
    }
    free(raw);

    *out_samples=samples;
    *out_n=n_frames;
    *out_sr=sr;
    return 0;
}

/* write_float_array - POSIX scrie(), nu fwrite()                     */
static int write_float_array(int fd, const float*data, size_t n)
{
    size_t total=n*sizeof(float);
    const char*ptr=(const char *)data;
    while (total > 0) {
        ssize_t w=write(fd, ptr, total);
        if (w < 0) {
            if (errno==EINTR) continue;
            return -1;
        }
        ptr   +=w;
        total -=(size_t)w;
    }
    return 0;
}

/* Worker - ruleaza in procesul copil (fork)                          */
static int process_spectrogram_worker(const proc_request_t*req,
                                      proc_result_t*res)
{
    /* 1. Incarca audio */
    float*samples=NULL;
    int    n_samples=0;
    int    sr=0;

    if (read_wav_samples(req->input_path, &samples, &n_samples, &sr)!=0) {
        snprintf(res->error_msg, sizeof(res->error_msg),
                 "read_wav failed: %s", req->input_path);
        return -1;
    }

    res->sample_rate=sr;
    res->n_samples=n_samples;
    res->duration_s=(double)n_samples/sr;

    /* 2. Copiaza in Eigen Vectorf */
    librosa::Vectorf x(n_samples);
    for (int i=0; i < n_samples; i++) x[i]=samples[i];
    free(samples);

    /* 3. Mel spectrograma via LibrosaCpp */
    int n_fft=req->n_fft      > 0 ? req->n_fft      : DEFAULT_N_FFT;
    int hop_length=req->hop_length > 0 ? req->hop_length : DEFAULT_HOP_LENGTH;
    int n_mels=req->n_mels     > 0 ? req->n_mels     : DEFAULT_N_MELS;

    librosa::Matrixf mel=librosa::internal::melspectrogram(x, sr, n_fft, hop_length,
                                                    "hann", true, "reflect",
                                                    2.f, n_mels, 0, sr/2);

    /* 4. Power la dB */
    librosa::Matrixf mel_db=librosa::internal::power2db(mel);

    int rows=(int)mel_db.rows(); /* n_mels  */
    int cols=(int)mel_db.cols(); /* n_frames */

    res->n_mels=rows;
    res->n_frames=cols;

    /* 5. Scrie binar cu POSIX deschide/scrie/inchide */
    int fd=open(req->output_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        snprintf(res->error_msg, sizeof(res->error_msg),
                 "open failed: %s", req->output_path);
        return -1;
    }

    int32_t hdr[2]={ (int32_t)rows, (int32_t)cols };
    (void)write(fd, hdr, sizeof(hdr));

    /* Eigen stocheaza column-major, scriem row-major */
    for (int r=0; r < rows; r++) {
        for (int c=0; c < cols; c++) {
            float v=mel_db(r, c);
            (void)write(fd, &v, sizeof(float));
        }
    }
    (void)close(fd);

    (void)memcpy(res->output_path, req->output_path, MAX_PATH_LEN - 1);
    res->output_path[MAX_PATH_LEN - 1]='\0';
    (void)snprintf(res->error_msg, sizeof(res->error_msg), "OK");
    return 0;
}

/* API public - process_spectrogram()                                 */
extern "C"
int process_spectrogram(const proc_request_t*req, proc_result_t*res)
{
    int pipefd[2];
    if (pipe(pipefd)!=0) { (void)perror("[processing] pipe"); return -1; }

    pid_t pid=fork();
    if (pid < 0) {
        (void)perror("[processing] fork");
        (void)close(pipefd[0]); (void)close(pipefd[1]);
        return -1;
    }

    if (pid==0) {
        /* copil */
        (void)close(pipefd[0]);

        proc_result_t child_res;
        (void)memset(&child_res, 0, sizeof(child_res));

        int ret_code=process_spectrogram_worker(req, &child_res);
        child_res.status=ret_code;

        const char*ptr=(const char *)&child_res;
        size_t rem=sizeof(child_res);
        while (rem > 0) {
            ssize_t n=write(pipefd[1], ptr, rem);
            if (n < 0) { break; }
            ptr +=n; rem -=(size_t)n;
        }
        (void)close(pipefd[1]);
        _exit(ret_code==0 ? 0 : 1);
    }

    /* parinte */
    (void)close(pipefd[1]);

    (void)memset(res, 0, sizeof(*res));
    char*ptr=(char *)res;
    size_t rem=sizeof(*res);
    while (rem > 0) {
        ssize_t n=read(pipefd[0], ptr, rem);
        if (n<=0) { break; }
        ptr +=n; rem -=(size_t)n;
    }
    (void)close(pipefd[0]);

    int status;
    (void)waitpid(pid, &status, 0);
    return (WIFEXITED(status) && WEXITSTATUS(status)==0) ? 0 : -1;
}