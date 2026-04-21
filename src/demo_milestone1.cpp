/**
 * @fisier demo_milestone1.cpp
 * @pe scurt PCD T31 - milestone 1 standalone Demo
 *
 * Demonstreaza toate cerintele M1:
 *   1. libconfig  - citeste Server.cfg
 *   2. getopt     - parseaza argumente linie de comanda
 *   3. getenv     - citeste variabile de mediu
 *   4. fork/asteapta  - proces copil izolat
 *   5. LibrosaCpp - Mel spectrograma in copil
 *   6. POSIX I/O  - deschide/scrie/inchide (nu fopen/fwrite)
 *   7. pipe       - IPC intre parinte si copil
 *
 * Compilat cu g++ (LibrosaCpp necesita C++).
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <unistd.h>
/* pentru functii POSIX de sistem */
#include <fcntl.h>
/* pentru controlul descriptorilor de fisiere */
#include <sys/stat.h>
/* pentru informatii despre fisiere */
#include <sys/types.h>
/* pentru tipuri de date sistem */
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */
#include <getopt.h>

/* LibrosaCpp - C++ cu Eigen3 */
#include <librosa/librosa.h>

extern "C" {
#include <libconfig.h>
#include "server.h"
/* pentru definitii ale serverului */
#include "config_loader.h"
/* pentru incarcarea configuratiilor */
#include "processing.h"
/* pentru functii de procesare audio */
}

/* print_env_info: demonstreaza getenv                                */
static void print_env_info(void)
{
    const char*home=getenv("HOME");
    const char*user=getenv("USER");
    const char*pcd=getenv("PCD_SERVER");
    const char*path=getenv("PATH");

    printf("\n--- Environment Variables (getenv) ---\n");
    printf("  HOME       = %s\n", home ? home : "(not set)");
    printf("  USER       = %s\n", user ? user : "(not set)");
    printf("  PCD_SERVER = %s\n", pcd  ? pcd  : "(not set)");
    printf("  PATH       = %.80s...\n", path ? path : "(not set)");
}

/* Citire WAV PCM 16-bit (fara dependente extra)                      */
static int read_wav(const char*path, float **out, int*n, int*sr)
{
    FILE*f=fopen(path, "rb");
    if (!f) { perror("fopen wav"); return -1; }

    /* Cauta chunk-urile RIFF corect - sare peste sub-chunk-uri necunoscute */
    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f)!=12) { fclose(f); return -1; }
    if (hdr[0]!='R'||hdr[1]!='I'||hdr[2]!='F'||hdr[3]!='F') { fclose(f); return -1; }

    int ch=1, rate=22050, bits=16;
    long data_offset=-1;
    int  data_size=0;

    /* Parcurge chunk-urile pana gaseste "fmt " si "data" */
    uint8_t chunk_id[4];
    uint32_t chunk_size;
    while (fread(chunk_id, 1, 4, f)==4) {
        fread(&chunk_size, 4, 1, f);
        if (memcmp(chunk_id, "fmt ", 4)==0) {
            uint8_t fmt[16];
            fread(fmt, 1, 16, f);
            if (chunk_size > 16) fseek(f, chunk_size - 16, SEEK_CUR);
            ch=fmt[2] | (fmt[3]<<8);
            rate=fmt[4]|(fmt[5]<<8)|(fmt[6]<<16)|(fmt[7]<<24);
            bits=fmt[14] | (fmt[15]<<8);
        } else if (memcmp(chunk_id, "data", 4)==0) {
            data_size=chunk_size;
            data_offset=ftell(f);
            break;
        } else {
            fseek(f, chunk_size, SEEK_CUR);
        }
    }

    if (data_offset < 0 || bits!=16) {
        fprintf(stderr, "WAV invalid sau nu e 16-bit\n");
        fclose(f); return -1;
    }

    fseek(f, data_offset, SEEK_SET);
    int tot=data_size/2;
    int frm=tot/ch;
    int16_t*raw=(int16_t*)malloc(tot*sizeof(int16_t));
    fread(raw, 2, tot, f);
    fclose(f);

    float*s=(float*)malloc(frm*sizeof(float));
    for (int i=0; i < frm; i++) {
        float v=0;
        for (int c=0; c < ch; c++) v +=raw[i*ch+c]/32768.f;
        s[i]=v/ch;
    }
    free(raw);
    *out=s; *n=frm; *sr=rate;
    return 0;
}

/* Worker - ruleaza in procesul copil                                 */
static void demo_worker(const char*audio_path,
                        const char*output_path,
                        int         pipe_write)
{
    proc_result_t res;
    memset(&res, 0, sizeof(res));

    printf("[child %d] Incarc audio: %s\n", (int)getpid(), audio_path);

    /* 1. Citeste WAV */
    float*samples=NULL;
    int n_samples=0, sr=0;
    if (read_wav(audio_path, &samples, &n_samples, &sr)!=0) {
        snprintf(res.error_msg, sizeof(res.error_msg), "read_wav failed");
        res.status=-1;
        goto done;
    }
    printf("[child %d] %d samples @ %d Hz (%.2f s)\n",
           (int)getpid(), n_samples, sr, (double)n_samples/sr);

    {
        /* 2. Eigen Vectorf */
        librosa::Vectorf x(n_samples);
        for (int i=0; i < n_samples; i++) x[i]=samples[i];
        free(samples); samples=NULL;

        /* 3. Mel spectrograma */
        librosa::Matrixf mel=librosa::internal::melspectrogram(
            x, sr, DEFAULT_N_FFT, DEFAULT_HOP_LENGTH,
            "hann", true, "reflect", 2.f, DEFAULT_N_MELS, 0, sr/2);

        /* 4. Power la dB */
        librosa::Matrixf mel_db=librosa::internal::power2db(mel);

        int rows=(int)mel_db.rows();
        int cols=(int)mel_db.cols();
        printf("[child %d] Mel spectrogram: %d bande x %d frames\n",
               (int)getpid(), rows, cols);

        /* 5. Scrie binar cu POSIX deschide/scrie/inchide */
        int fd=open(output_path,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
            perror("[child] open");
            snprintf(res.error_msg, sizeof(res.error_msg),
                     "open failed: %s", output_path);
            res.status=-1;
            goto done;
        }

        int32_t hdr[2]={ (int32_t)rows, (int32_t)cols };
        write(fd, hdr, sizeof(hdr));

        for (int r=0; r < rows; r++)
            for (int c=0; c < cols; c++) {
                float v=mel_db(r, c);
                write(fd, &v, sizeof(float));
            }
        close(fd);

        printf("[child %d] Scris: %s\n", (int)getpid(), output_path);

        res.status=0;
        res.n_mels=rows;
        res.n_frames=cols;
        res.sample_rate=sr;
        res.n_samples=n_samples;
        strncpy(res.output_path, output_path, MAX_PATH_LEN - 1);
        strncpy(res.error_msg, "OK", sizeof(res.error_msg) - 1);
    }

done:
    if (samples) free(samples);

    /* 6. Trimite rezultat la parinte via pipe */
    const char*p=(const char *)&res;
    size_t rem=sizeof(res);
    while (rem > 0) {
        ssize_t n=write(pipe_write, p, rem);
        if (n < 0) break;
        p +=n; rem -=(size_t)n;
    }
    close(pipe_write);
}

/* principal()                                                              */
int main(int argc, char*argv[])
{
    const char*cfg_path="config/server.cfg";
    const char*audio_path=NULL;

    /* 1. getopt - parsare argumente */
    int opt;
    while ((opt=getopt(argc, argv, "c:f:h"))!=-1) {
        switch (opt) {
        case 'c': cfg_path=optarg; break;
        case 'f': audio_path=optarg; break;
        case 'h':
            printf("Usage: %s -c config.cfg -f audio.wav\n", argv[0]);
            return 0;
        default: return 1;
        }
    }

    if (!audio_path) {
        fprintf(stderr, "Eroare: -f <audio.wav> obligatoriu\n");
        return 1;
    }

    /* 2. libconfig - incarca configuratia */
    server_config_t cfg;
    config_load(cfg_path, &cfg);

    printf("=== PCD T31 - Milestone 1 Demo ===\n");
    printf("Config: port=%d  output_dir=%s  log_level=%d\n",
           cfg.port, cfg.output_dir, cfg.log_level);

    /* 3. getenv - afiseaza variabile de mediu */
    print_env_info();

    /* Creeaza directorul iesire daca nu exista */
    mkdir(cfg.output_dir, 0755);

    /* Construieste calea fisierului iesire */
    char output_path[MAX_PATH_LEN];
    snprintf(output_path, sizeof(output_path),
             "%s/demo_%d.bin", cfg.output_dir, (int)getpid());

    /* 4. pipe - canal IPC intre parinte si copil */
    int pipefd[2];
    if (pipe(pipefd)!=0) { perror("pipe"); return 1; }

    /* 5. fork - proces copil izolat */
    printf("\n[parent %d] Forking worker...\n", (int)getpid());
    pid_t pid=fork();

    if (pid < 0) { perror("fork"); return 1; }

    if (pid==0) {
        /* Copil */
        close(pipefd[0]);
        demo_worker(audio_path, output_path, pipefd[1]);
        _exit(0);
    }

    /* Parinte: citeste rezultat din pipe */
    close(pipefd[1]);

    proc_result_t res;
    memset(&res, 0, sizeof(res));
    char*p=(char *)&res;
    size_t rem=sizeof(res);
    while (rem > 0) {
        ssize_t n=read(pipefd[0], p, rem);
        if (n<=0) break;
        p +=n; rem -=(size_t)n;
    }
    close(pipefd[0]);

    /* Asteapta copilul */
    int wstatus;
    waitpid(pid, &wstatus, 0);

    /* Afiseaza rezultatul */
    printf("\n--- Rezultat (de la copil via pipe) ---\n");
    printf("  status      : %d (%s)\n", res.status, res.error_msg);
    if (res.status==0) {
        printf("  output_file : %s\n", res.output_path);
        printf("  sample_rate : %d Hz\n", res.sample_rate);
        printf("  n_mels      : %d\n",    res.n_mels);
        printf("  n_frames    : %d\n",    res.n_frames);
        printf("  duration    : %.2f s\n",res.duration_s);
    }

    config_free(&cfg);
    return (res.status==0) ? 0 : 1;
}