/**
 * PCD T31 - Raw TCP Client for inetds2 Binary Protocol
 * 
 * Complements inetds2.c (server side).
 * Sends a proc_request_t over a length-prefixed TCP stream and prints
 * the proc_result_t response received from the server.
 * 
 * Usage:
 *   ./inetsample2 -h 127.0.0.1 -p 9090 -f audio.wav
 *   PCD_RAW_SERVER=192.168.1.10 ./inetsample2 -f audio.wav
 */

#include <stdio.h>
/* pentru functii de intrare/iesire standard */
#include <stdlib.h>
/* pentru alocare de memorie si functii utilitare */
#include <string.h>
/* pentru manipularea sirurilor de caractere */
#include <unistd.h>
/* pentru functii POSIX de sistem */
#include <errno.h>
/* pentru gestionarea erorilor */
#include <getopt.h>
#include <arpa/inet.h>
/* pentru conversii adrese IP */
#include <netinet/in.h>
/* pentru socket-uri internet */
#include <sys/socket.h>
/* pentru operatii cu socket-uri */
#include <netdb.h>

#include "processing.h"
/* pentru functii de procesare audio */
#include "sclient.h"
/* pentru clientul soap */
#include "server.h"
/* pentru definitii ale serverului */

/* mediu variabila keys                                           */
#define ENV_RAW_SERVER "PCD_RAW_SERVER"
#define ENV_RAW_PORT   "PCD_RAW_PORT"

/* read_all / write_all helpers (POSIX syscalls only)                 */
static ssize_t read_all(int fd, void*buf, size_t len)
{
    char*ptr=(char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t n=read(fd, ptr, rem);
        if (n<=0) return (ssize_t)(len - rem);
        ptr +=n; rem -=(size_t)n;
    }
    return (ssize_t)len;
}

static ssize_t write_all(int fd, const void*buf, size_t len)
{
    const char*ptr=(const char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t n=write(fd, ptr, rem);
        if (n < 0) return -1;
        ptr +=n; rem -=(size_t)n;
    }
    return (ssize_t)len;
}

/* raw_client_call() - public API (declared in sclient.h)            */
int raw_client_call(const char*host,
                    int                  port,
                    const proc_request_t*req,
                    proc_result_t*res)
{
    /* Resolve gazda */
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &ai)!=0) {
        perror("[raw_client] getaddrinfo");
        return -1;
    }

    int fd=socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        perror("[raw_client] socket");
        freeaddrinfo(ai);
        return -1;
    }

    if (connect(fd, ai->ai_addr, ai->ai_addrlen)!=0) {
        perror("[raw_client] connect");
        close(fd);
        freeaddrinfo(ai);
        return -1;
    }
    freeaddrinfo(ai);

    /* trimite lungime-prefixat cerere */
    uint32_t len_net=htonl((uint32_t)sizeof(proc_request_t));
    write_all(fd, &len_net, sizeof(len_net));
    write_all(fd, req, sizeof(*req));

    /* primeste lungime-prefixat raspuns */
    uint32_t res_len_net=0;
    if (read_all(fd, &res_len_net, sizeof(res_len_net))!=sizeof(res_len_net)) {
        fprintf(stderr, "[raw_client] Failed to read response length\n");
        close(fd);
        return -1;
    }
    uint32_t res_len=ntohl(res_len_net);

    if (res_len!=sizeof(proc_result_t)) {
        fprintf(stderr, "[raw_client] Bad response size: %u\n", res_len);
        close(fd);
        return -1;
    }

    memset(res, 0, sizeof(*res));
    read_all(fd, res, sizeof(*res));
    close(fd);
    return 0;
}

/* print_proc_result - public API (declared in sclient.h)            */
void print_proc_result(const proc_result_t*res)
{
    fprintf(stdout, "\n--- proc_result_t ---\n");
    fprintf(stdout, "  status     : %d\n",    res->status);
    fprintf(stdout, "  error_msg  : %s\n",    res->error_msg);
    fprintf(stdout, "  output     : %s\n",    res->output_path);
    fprintf(stdout, "  sample_rate: %d Hz\n", res->sample_rate);
    fprintf(stdout, "  n_samples  : %d\n",    res->n_samples);
    fprintf(stdout, "  duration   : %.2f s\n",res->duration_s);
    fprintf(stdout, "  n_mels     : %d\n",    res->n_mels);
    fprintf(stdout, "  n_frames   : %d\n",    res->n_frames);
}

/* principal()                                                              */
int main(int argc, char*argv[])
{
    /* implicit from env vars (fallback la hardcoded) */
    const char*host=getenv(ENV_RAW_SERVER);
    if (!host) host="127.0.0.1";

    const char*port_env=getenv(ENV_RAW_PORT);
    int port=port_env ? atoi(port_env) : 9090;

    const char*file_path=NULL;
    int n_mels=DEFAULT_N_MELS;
    int sr=DEFAULT_SR;

    /* CLI args suprascrie env vars */
    int opt;
    while ((opt=getopt(argc, argv, "h:p:f:m:r:?"))!=-1) {
        switch (opt) {
        case 'h': host=optarg;       break;
        case 'p': port=atoi(optarg); break;
        case 'f': file_path=optarg;       break;
        case 'm': n_mels=atoi(optarg); break;
        case 'r': sr=atoi(optarg); break;
        default:
            fprintf(stderr,
                "Usage: %s -h host -p port -f file [-m mels] [-r samplerate]\n"
                "Env: %s, %s\n",
                argv[0], ENV_RAW_SERVER, ENV_RAW_PORT);
            return 1;
        }
    }

    if (!file_path) {
        fprintf(stderr, "[inetsample2] Error: -f <file> required\n");
        return 1;
    }

    fprintf(stdout, "[inetsample2] Connecting to %s:%d\n", host, port);

    proc_request_t req;
    memset(&req, 0, sizeof(req));
    strncpy(req.input_path, file_path, MAX_PATH_LEN - 1);

    /* construieste iesire cale on Server side (Server fills acest in) */
    snprintf(req.output_path, MAX_PATH_LEN, "./output/raw_%d.bin", (int)getpid());

    req.target_sr=sr;
    req.n_mels=n_mels;
    req.n_fft=DEFAULT_N_FFT;
    req.hop_length=DEFAULT_HOP_LENGTH;

    proc_result_t res;
    if (raw_client_call(host, port, &req, &res)!=0) {
        fprintf(stderr, "[inetsample2] Call failed\n");
        return 1;
    }

    print_proc_result(&res);
    return (res.status==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}