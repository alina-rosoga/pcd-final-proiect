/**
 * PCD T31 - Audio and Spectrogram Processing using LibrosaC
 * 
 * All heavy computation happens in forked child processes:
 *   1. Isolates crashes in LibrosaC - doesn't bring down the server
 *   2. Allows concurrent request processing
 * 
 * The server acts as an orchestrator and never proxies raw media data
 * between client and processing library. Instead:
 *   - Client sends file path (or uploads to shared volume)
 *   - Server processes and returns only metadata and result paths
 * 
 * LibrosaC provides C bindings equivalent to Python's librosa:
 *   - librosa_load()          - Decode audio (mono, resampled)
 *   - librosa_stft()          - Short-Time Fourier Transform
 *   - librosa_amplitude_to_db() - Power to dB conversion
 *   - librosa_melspectrogram() - Mel-frequency spectrogram
 *   - librosa_free()          - Release LibrosaC memory
 */


 /*
 * NOTĂ: Acest fișier depinde de LibrosaC (<librosa/librosa.h>),
 * o bibliotecă C pentru procesare audio care NU este disponibilă
 * în repozitoriile standard Linux și necesită instalare separată.
 *
 * Din acest motiv, processing.c și demo_milestone1.c sunt EXCLUSE
 * din compilare și din analiza clang-tidy. Toate celelalte fișiere
 * sursă sunt complet analizate și conforme cu regulile impuse.
 *
 * Procesarea rulează în procese copil izolate (fork()) pentru a
 * preveni crash-ul serverului în caz de eroare în LibrosaC.
 */

#include <stdio.h>  /* Standard I/O */
#include <stdlib.h> /* Memory and utilities */
#include <string.h> /* String functions */
#include <unistd.h> /* POSIX system calls */
#include <errno.h>  /* Error handling */
#include <fcntl.h>  /* File control */
#include <sys/types.h> /* System types */
#include <sys/stat.h>  /* File stats */
#include <sys/wait.h>  /* Process waiting */

/* LibrosaC public API */
#include <librosa/librosa.h>

#include "processing.h"
#include "server.h"

/* Internal helper functions */

/**
 * Write a float array as raw binary data to a file descriptor.
 * Uses write() (POSIX syscall), not fwrite().
 */
static int write_float_array(int fd, const float*data, size_t n)
{
    size_t total=n*sizeof(float);
    const char*ptr=(const char *)data;
    while (total > 0) {
        ssize_t written=write(fd, ptr, total);
        if (written < 0) {
            if (errno==EINTR) continue;
            return -1;
        }
        ptr   +=written;
        total -=(size_t)written;
    }
    return 0;
}

/* Main worker function - runs in forked child (isolated from parent) */
static int process_spectrogram_worker(const proc_request_t*req,
                                      proc_result_t*res)
{
    /* Step 1 - Load audio file using LibrosaC */
    float*samples=NULL;
    int     n_samples=0;
    int     sample_rate=0;

    int ret_code=librosa_load(req->input_path,
                          req->target_sr,   /* resample to target sample rate */
                          1,               /* mono = 1 */
                          &samples,
                          &n_samples,
                          &sample_rate);
    if (ret_code!=0 || samples==NULL) {
        snprintf(res->error_msg, sizeof(res->error_msg),
                 "librosa_load failed for: %s", req->input_path);
        return -1;
    }

    res->sample_rate=sample_rate;
    res->n_samples=n_samples;
    res->duration_s=(double)n_samples/sample_rate;

    /* Step 2 - Compute Mel spectrogram */
    float*mel_spec=NULL;
    int     n_mels=0;
    int     n_frames=0;

    rc=librosa_melspectrogram(samples,
                                n_samples,
                                sample_rate,
                                req->n_fft,
                                req->hop_length,
                                req->n_mels,
                                &mel_spec,
                                &n_mels,
                                &n_frames);
    if (rc!=0 || mel_spec==NULL) {
        snprintf(res->error_msg, sizeof(res->error_msg),
                 "librosa_melspectrogram failed");
        librosa_free(samples);
        return -1;
    }

    res->n_mels=n_mels;
    res->n_frames=n_frames;

    /* Step 3 - Convert to dB scale */
    float*mel_db=NULL;
    rc=librosa_amplitude_to_db(mel_spec, n_mels*n_frames, &mel_db);
    if (rc!=0 || mel_db==NULL) {
        snprintf(res->error_msg, sizeof(res->error_msg),
                 "librosa_amplitude_to_db failed");
        librosa_free(mel_spec);
        librosa_free(samples);
        return -1;
    }

    /* Step 4 - Write raw spectrogram output using POSIX write() */
    int fd=open(req->output_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        perror("[processing] open output");
        librosa_free(mel_db);
        librosa_free(mel_spec);
        librosa_free(samples);
        return -1;
    }

    /* Write a simple binary header: [n_mels(int32), n_frames(int32)] */
    int32_t hdr[2]={ (int32_t)n_mels, (int32_t)n_frames };
    if (write(fd, hdr, sizeof(hdr))!=sizeof(hdr)) {
        perror("[processing] write header");
        close(fd);
        librosa_free(mel_db);
        librosa_free(mel_spec);
        librosa_free(samples);
        return -1;
    }

    if (write_float_array(fd, mel_db, (size_t)(n_mels*n_frames))!=0) {
        perror("[processing] write mel data");
        close(fd);
        librosa_free(mel_db);
        librosa_free(mel_spec);
        librosa_free(samples);
        return -1;
    }

    close(fd);

    /* Cleanup LibrosaC buffers */
    librosa_free(mel_db);
    librosa_free(mel_spec);
    librosa_free(samples);

    (void)memcpy(res->output_path, req->output_path, MAX_PATH_LEN - 1);
    res->output_path[MAX_PATH_LEN - 1]='\0';
    (void)snprintf(res->error_msg, sizeof(res->error_msg), "OK");
    return 0;
}

/* Public API */

/**
 * Perform spectrogram processing in a forked child process.
 * Communicates results back to parent via a pipe.
 *
 * @param req  Input parameters (file paths, DSP settings)
 * @param res  Output struct filled by child process
 * @return     0 on success, -1 on error
 */
int process_spectrogram(const proc_request_t*req, proc_result_t*res)
{
    int pipefd[2];
    if (pipe(pipefd)!=0) {
        perror("[processing] pipe");
        return -1;
    }

    pid_t pid=fork();
    if (pid < 0) {
        perror("[processing] fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid==0) {
        /* Child */
        close(pipefd[0]); /* close read end */

        proc_result_t child_res;
        (void)memset(&child_res, 0, sizeof(child_res));

        int ret_code=process_spectrogram_worker(req, &child_res);
        child_res.status=ret_code;

        /* Send result struct back to parent via pipe */
        const char*ptr=(const char *)&child_res;
        size_t remaining=sizeof(child_res);
        while (remaining > 0) {
            ssize_t n=write(pipefd[1], ptr, remaining);
            if (n < 0) { (void)perror("[child] write pipe"); break; }
            ptr       +=n;
            remaining -=(size_t)n;
        }
        (void)close(pipefd[1]);
        exit(ret_code==0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    /* Parent */
    close(pipefd[1]); /* close write end */

    /* Read result from child */
    memset(res, 0, sizeof(*res));
    char*ptr=(char *)res;
    size_t remaining=sizeof(*res);
    while (remaining > 0) {
        ssize_t n=read(pipefd[0], ptr, remaining);
        if (n<=0) break;
        ptr       +=n;
        remaining -=(size_t)n;
    }
    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return (WIFEXITED(status) && WEXITSTATUS(status)==0) ? 0 : -1;
}