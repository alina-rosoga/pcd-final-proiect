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
#include <stdint.h>
/* pentru uint32_t si uint16_t */
#include <netinet/in.h>
/* pentru socket-uri internet */
#include <sys/socket.h>
/* pentru operatii cu socket-uri */
#include <netdb.h>

#include "processing.h"
/* pentru functii de procesare audio */
#include "sclient.h"
/* pentru clientul soap */

/* mediu variabila keys                                           */
#define ENV_RAW_SERVER "PCD_RAW_SERVER"
#define ENV_RAW_PORT   "PCD_RAW_PORT"
#define PORT_STR_LEN   8
#define STRTOL_BASE    10
#define DEFAULT_RAW_PORT 9090

/* read_all / write_all helpers (POSIX syscalls only)                 */
static ssize_t read_all(int sock_fd, void*buf, size_t len)
{
    char*ptr=(char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t num_bytes=read(sock_fd, ptr, rem);
        if (num_bytes<=0) {
            return (ssize_t)(len - rem);
        }
        ptr +=num_bytes;
        rem -=(size_t)num_bytes;
    }
    return (ssize_t)len;
}

static ssize_t write_all(int sock_fd, const void*buf, size_t len)
{
    const char*ptr=(const char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t num_bytes=write(sock_fd, ptr, rem);
        if (num_bytes < 0) {
            return -1;
        }
        ptr +=num_bytes;
        rem -=(size_t)num_bytes;
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
    struct addrinfo hints;
    struct addrinfo *addr_info;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;

    char port_str[PORT_STR_LEN];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host, port_str, &hints, &addr_info)!=0) {
        perror("[raw_client] getaddrinfo");
        return -1;
    }

    int sock_fd=socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
    if (sock_fd < 0) {
        perror("[raw_client] socket");
        freeaddrinfo(addr_info);
        return -1;
    }

    if (connect(sock_fd, addr_info->ai_addr, addr_info->ai_addrlen)!=0) {
        perror("[raw_client] connect");
        close(sock_fd);
        freeaddrinfo(addr_info);
        return -1;
    }
    freeaddrinfo(addr_info);

    /* trimite lungime-prefixat cerere */
    uint32_t len_net=htonl((uint32_t)sizeof(proc_request_t));
    (void)write_all(sock_fd, &len_net, sizeof(len_net));
    (void)write_all(sock_fd, req, sizeof(*req));

    /* primeste lungime-prefixat raspuns */
    uint32_t res_len_net=0;
    if (read_all(sock_fd, &res_len_net, sizeof(res_len_net))!=sizeof(res_len_net)) {
        (void)fprintf(stderr, "[raw_client] Failed to read response length\n");
        close(sock_fd);
        return -1;
    }
    uint32_t res_len=ntohl(res_len_net);

    if (res_len!=sizeof(proc_result_t)) {
        (void)fprintf(stderr, "[raw_client] Bad response size: %u\n", res_len);
        close(sock_fd);
        return -1;
    }

    (void)memset(res, 0, sizeof(*res));
    (void)read_all(sock_fd, res, sizeof(*res));
    close(sock_fd);
    return 0;
}

/* print_proc_result - public API (declared in sclient.h)            */
void print_proc_result(const proc_result_t*res)
{
    (void)fprintf(stdout, "\n--- proc_result_t ---\n");
    (void)fprintf(stdout, "  status     : %d\n",    res->status);
    (void)fprintf(stdout, "  error_msg  : %s\n",    res->error_msg);
    (void)fprintf(stdout, "  output     : %s\n",    res->output_path);
    (void)fprintf(stdout, "  sample_rate: %d Hz\n", res->sample_rate);
    (void)fprintf(stdout, "  n_samples  : %d\n",    res->n_samples);
    (void)fprintf(stdout, "  duration   : %.2f s\n",res->duration_s);
    (void)fprintf(stdout, "  n_mels     : %d\n",    res->n_mels);
    (void)fprintf(stdout, "  n_frames   : %d\n",    res->n_frames);
}
int main(int argc, char*argv[])
{
    /* implicit from env vars (fallback la hardcoded) */
    const char*host=getenv(ENV_RAW_SERVER);
    if (!host) {
        host="127.0.0.1";
    }

    const char*port_env=getenv(ENV_RAW_PORT);
    int port=port_env ? (int)strtol(port_env, NULL, STRTOL_BASE) : DEFAULT_RAW_PORT;

    const char*file_path=NULL;
    int n_mels=DEFAULT_N_MELS;
    int sample_rate=DEFAULT_SR;

    /* CLI args suprascrie env vars */
    int opt;
    while ((opt=getopt(argc, argv, "h:p:f:m:r:?"))!=-1) {
        switch (opt) {
        case 'h': {
            host=optarg;
            break;
        }
        case 'p': {
            char *endptr;
            port=(int)strtol(optarg, &endptr, STRTOL_BASE);
            break;
        }
        case 'f': {
            file_path=optarg;
            break;
        }
        case 'm': {
            char *endptr;
            n_mels=(int)strtol(optarg, &endptr, STRTOL_BASE);
            break;
        }
        case 'r': {
            char *endptr;
            sample_rate=(int)strtol(optarg, &endptr, STRTOL_BASE);
            break;
        }
        case '?': {
            break;
        }
        default: {
            break;
        }
        }
    }

    if (!file_path) {
        (void)fprintf(stderr, "[inetsample2] Error: -f <file> required\n");
        return 1;
    }

    (void)fprintf(stdout, "[inetsample2] Connecting to %s:%d\n", host, port);

    proc_request_t req;
    (void)memset(&req, 0, sizeof(req));
    (void)memcpy(req.input_path, file_path, strlen(file_path));
    req.input_path[MAX_PATH_LEN - 1]='\0';

    /* construieste iesire cale on Server side (Server fills acest in) */
    (void)snprintf(req.output_path, MAX_PATH_LEN, "./output/raw_%d.bin", (int)getpid());

    req.target_sr=sample_rate;
    req.n_mels=n_mels;
    req.n_fft=DEFAULT_N_FFT;
    req.hop_length=DEFAULT_HOP_LENGTH;

    proc_result_t res;
    if (raw_client_call(host, port, &req, &res)!=0) {
        (void)raw_client_call(host, port, &req, &res)!=0) {
        fprintf(stderr, "[inetsample2] Call failed\n");
        return 1;
    }

    print_proc_result(&res);
    return (res.status==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}