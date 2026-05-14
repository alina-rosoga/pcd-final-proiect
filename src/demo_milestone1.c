/**
 * PCD T31 - Milestone 1 Demo
 * 
 * Demonstrates all mandatory milestone 1 requirements in one
 * self-contained program:
 * 
 *   1. libconfig  - Load Server.cfg
 *   2. getopt     - Parse command-line options
 *   3. getenv     - Read environment variables
 *   4. fork/wait  - Isolated worker subprocess
 *   5. LibrosaC   - Compute Mel spectrogram in child
 *   6. POSIX I/O  - open/write/close (no fopen/fwrite)
 *   7. pipe       - IPC between parent and child
 * 
 * Build (standalone, no gSOAP needed for this demo):
 *   gcc -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L \
 *       -Iinclude demo_milestone1.c src/config_loader.c   \
 *       -lconfig -lrosacpp -lm -o demo_milestone1
 * 
 * Run:
 *   ./demo_milestone1 -c config/server.cfg -f audio.wav
 */

#include <stdio.h>  /* Standard I/O */
#include <stdlib.h> /* Memory and utilities */
#include <string.h> /* String functions */
#include <unistd.h> /* POSIX system calls */
#include <errno.h>  /* Error handling */
#include <fcntl.h>  /* File control */
#include <sys/stat.h> /* File stats */
#include <sys/types.h> /* System types */
#include <sys/wait.h> /* Process waiting */
#include <getopt.h>

#include <libconfig.h>
#include <librosa/librosa.h>

#include "server.h"
#include "config_loader.h"
/* pentru incarcarea configuratiilor */
#include "processing.h"
/* pentru functii de procesare audio */

#define DIR_PERMISSIONS 0755

/* print_env_info: demonstrate getenv utilizare                           */
static void print_env_info(void)
{
    const char*home=getenv("HOME");
    const char*user=getenv("USER");
    const char*pcd_env=getenv("PCD_SERVER");
    const char*path_env=getenv("PATH");

    (void)fprintf(stdout, "\n--- Environment Variables ---\n");
    (void)fprintf(stdout, "  HOME       = %s\n", home     ? home     : "(not set)");
    (void)fprintf(stdout, "  USER       = %s\n", user     ? user     : "(not set)");
    (void)fprintf(stdout, "  PCD_SERVER = %s\n", pcd_env  ? pcd_env  : "(not set)");
    (void)fprintf(stdout, "  PATH       = %.60s...\n", path_env ? path_env : "(not set)");
}

/* librosa_demo_worker: runs inside fork()ed copil                    */
static void librosa_demo_worker(const char*audio_path,
                                const char*output_path,
                                int         pipefd_write)
{
    proc_result_t res;
    (void)memset(&res, 0, sizeof(res));

    (void)fprintf(stdout, "[child %d] Loading audio: %s\n", (int)getpid(), audio_path);

    /* LibrosaC: incarca audio */
    float*samples=NULL;
    int    n_samples=0;
    int    sr=0;

    int rc=librosa_load(audio_path, 22050, 1, &samples, &n_samples, &sr);
    if (rc!=0 || !samples) {
        snprintf(res.error_msg, sizeof(res.error_msg),
                 "librosa_load failed (rc=%d)", rc);
        res.status=-1;
        goto send_result;
    }

    (void)fprintf(stdout, "[child %d] Loaded %d samples @ %d Hz (%.2f s)\n",
            (int)getpid(), n_samples, sr, (double)n_samples/sr);

    /* LibrosaC: Mel spectrograma */
    float*mel_spec=NULL;
    int    n_mels=0;
    int    n_frames=0;

    rc=librosa_melspectrogram(samples, n_samples, sr,
                                2048, 512, 128,
                                &mel_spec, &n_mels, &n_frames);
    librosa_free(samples);

    if (rc!=0 || !mel_spec) {
        snprintf(res.error_msg, sizeof(res.error_msg),
                 "librosa_melspectrogram failed (rc=%d)", rc);
        res.status=-1;
        goto send_result;
    }

    (void)fprintf(stdout, "[child %d] Mel spectrogram: %d bands x %d frames\n",
            (int)getpid(), n_mels, n_frames);

    /* LibrosaC: convert la dB */
    float*mel_db=NULL;
    rc=librosa_amplitude_to_db(mel_spec, n_mels*n_frames, &mel_db);
    librosa_free(mel_spec);

    if (rc!=0 || !mel_db) {
        snprintf(res.error_msg, sizeof(res.error_msg),
                 "librosa_amplitude_to_db failed (rc=%d)", rc);
        res.status=-1;
        goto send_result;
    }

    /* scrie iesire using POSIX deschide/scrie/inchide */
    int fd=open(output_path,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        perror("[child] open output");
        snprintf(res.error_msg, sizeof(res.error_msg),
                 "open(%s) failed: %s", output_path, strerror(errno));
        librosa_free(mel_db);
        res.status=-1;
        goto send_result;
    }

    /* binar antet: [n_mels(int32), n_frames(int32)] */
    int32_t hdr[2]={ (int32_t)n_mels, (int32_t)n_frames };
    write(fd, hdr, sizeof(hdr));

    /* scrie float data in chunks */
    size_t total=(size_t)(n_mels*n_frames)*sizeof(float);
    const char*ptr=(const char *)mel_db;
    while (total > 0) {
        ssize_t w=write(fd, ptr, total);
        if (w < 0) { perror("[child] write"); break; }
        ptr   +=w;
        total -=(size_t)w;
    }
    close(fd);
    librosa_free(mel_db);

    res.status=0;
    res.n_mels=n_mels;
    res.n_frames=n_frames;
    res.sample_rate=sr;
    (void)memcpy(res.output_path, output_path, MAX_PATH_LEN - 1);
    res.output_path[MAX_PATH_LEN - 1]='\0';
    (void)memcpy(res.error_msg, "OK", strlen("OK"));
    res.error_msg[strlen("OK")]='\0';

    (void)fprintf(stdout, "[child %d] Written: %s\n", (int)getpid(), output_path);

send_result:
    /* trimite proc_result_t la parinte via pipe */
    {
        const char*p=(const char *)&res;
        size_t rem=sizeof(res);
        while (rem > 0) {
            ssize_t n=write(pipefd_write, p, rem);
            if (n < 0) break;
            p   +=n;
            rem -=(size_t)n;
        }
    }
    close(pipefd_write);
}

/* principal()                                                              */
int main(int argc, char*argv[])
{
    const char*cfg_path="config/server.cfg";
    const char*audio_path=NULL;

    /* argument parsare */
    int opt;
    while ((opt=getopt(argc, argv, "c:f:h"))!=-1) {
        switch (opt) {
        case 'c': cfg_path=optarg; break;
        case 'f': audio_path=optarg; break;
        case 'h':
            (void)fprintf(stdout,
                "Usage: %s -c config.cfg -f audio.wav\n", argv[0]);
            return 0;
        default: return 1;
        }
    }

    if (!audio_path) {
        (void)fprintf(stderr, "Error: -f <audio.wav> required\n");
        return 1;
    }

    /* incarca libconfig */
    server_config_t cfg;
    config_load(cfg_path, &cfg);

    (void)fprintf(stdout, "PCD T31 - Milestone 1 Demo \n");
    (void)fprintf(stdout, "Config: port=%d  output_dir=%s  log_level=%d\n",
            cfg.port, cfg.output_dir, cfg.log_level);

    /* Afiseaza mediu variabile */
    print_env_info();
(void)mkdir(cfg.output_dir, DIR_PERMISSIONS);

    /* construieste iesire cale */
    char output_path[MAX_PATH_LEN];
    (void)/* construieste iesire cale */
    char output_path[MAX_PATH_LEN];
    snprintf(output_path, sizeof(output_path),
             "%s/demo_%d.bin", cfg.output_dir, (int)getpid());

    /* Create pipe pentru IPC */
    int pipefd[2];
    if (pipe(pipefd)!=0) { perror("pipe"); return 1; }

    /* fork worker */
    (void)fprintf(stdout, "\n[parent %d] Forking worker...\n", (int)getpid());
    pid_t pid=fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid==0) {
        /* copil */
        close(pipefd[0]); /* inchide citeste end */
        librosa_demo_worker(audio_path, output_path, pipefd[1]);
        exit(EXIT_SUCCESS);
    }

    /* parinte: citeste rezultat from pipe */
    close(pipefd[1]); /* inchide scrie end */

    proc_result_t res;
    memset(&res, 0, sizeof(res));
    char*p=(char *)&res;
    size_t rem=sizeof(res);
    while (rem > 0) {
        ssize_t n=read(pipefd[0], p, rem);
        if (n<=0) break;
        p   +=n;
        rem -=(size_t)n;
    }
    close(pipefd[0]);

    /* asteapta pentru copil */
    int wstatus;
    waitpid(pid, &wstatus, 0);

    /* afiseaza summary */
    (void)fprintf(stdout, "\n--- Result (from child via pipe) ---\n");
    (void)fprintf(stdout, "  Status      : %d (%s)\n", res.status, res.error_msg);
    if (res.status==0) {
        (void)fprintf(stdout, "  Output file : %s\n", res.output_path);
        (void)fprintf(stdout, "  Sample rate : %d Hz\n", res.sample_rate);
        (void)fprintf(stdout, "  Mel bands   : %d\n",    res.n_mels);
        (void)fprintf(stdout, "  Time frames : %d\n",    res.n_frames);
        (void)fprintf(stdout, "  Duration    : %.2f s\n", res.duration_s);
    }

    config_free(&cfg);
    return (res.status==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
