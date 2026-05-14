/**
 * PCD T31 - Raw TCP/INET Socket Server (no gSOAP)
 * 
 * A lightweight alternative transport layer: accepts binary
 * proc_request_t / proc_result_t messages over plain TCP.
 * Mirrors the inetds pattern from the PCD lab skeleton.
 * 
 * Frame format: [uint32_t payload_len][payload bytes]
 * 
 * The main SOAP service runs in server.c.
 * This module is useful for integration tests and benchmarking.
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
#include <netinet/in.h>
/* pentru socket-uri internet */
#include <arpa/inet.h>
/* pentru conversii adrese IP */
#include <sys/socket.h>
/* pentru operatii cu socket-uri */
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */
#include <stdint.h>
/* pentru uint32_t si uint16_t */

#include "processing.h"
/* pentru functii de procesare audio */

/* Constants                                                           */
#define INET_RAW_PORT    9090   /**< brut TCP port (separate from soap) */
#define INET_BACKLOG     16

/* read_all / write_all: fiabil flux I/O via POSIX syscalls        */
static ssize_t read_all(int sock_fd, void*buf, size_t len)
{
    char*ptr=(char *)buf;
    size_t  rem=len;
    while (rem > 0) {
        ssize_t num_bytes=read(sock_fd, ptr, rem);
        if (num_bytes < 0) {
            if (errno==EINTR) { continue; }
            return -1;
        }
        if (num_bytes==0) { return (ssize_t)(len - rem); } /* EOF */
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
            if (errno==EINTR) { continue; }
            return -1;
        }
        ptr +=num_bytes;
        rem -=(size_t)num_bytes;
    }
    return (ssize_t)len;
}

/* handle_inet_client                                                  */
static void handle_inet_client(int client_fd, const char*client_ip)
{
    (void)fprintf(stdout, "[inetds] Client connected: %s\n", client_ip);

    /* citeste lungime-prefixat cerere */
    uint32_t req_len=0;
    if (read_all(client_fd, &req_len, sizeof(req_len))!=sizeof(req_len)) {
        (void)fprintf(stderr, "[inetds] Failed to read request length\n");
        return;
    }
    req_len=ntohl(req_len); /* retea byte ordine → gazda */

    if (req_len!=sizeof(proc_request_t)) {
        (void)fprintf(stderr, "[inetds] Bad request size: %u (expected %zu)\n",
                req_len, sizeof(proc_request_t));
        return;
    }

    proc_request_t req;
    if (read_all(client_fd, &req, sizeof(req))!=sizeof(req)) {
        perror("[inetds] read request");
        return;
    }

    /* aplica implicit pentru unset fields */
    if (req.target_sr<=0) { req.target_sr=DEFAULT_SR; }
    if (req.n_fft<=0) { req.n_fft=DEFAULT_N_FFT; }
    if (req.hop_length<=0) { req.hop_length=DEFAULT_HOP_LENGTH; }
    if (req.n_mels<=0) { req.n_mels=DEFAULT_N_MELS; }

    /* proces (fork intern) */
    proc_result_t res;
    (void)memset(&res, 0, sizeof(res));
    (void)process_spectrogram(&req, &res);

    /* trimite lungime-prefixat raspuns */
    uint32_t res_len_net=htonl((uint32_t)sizeof(proc_result_t));
    (void)write_all(client_fd, &res_len_net, sizeof(res_len_net));
    (void)write_all(client_fd, &res, sizeof(res));

    (void)fprintf(stdout, "[inetds] Response sent: status=%d  msg=%s\n",
            res.status, res.error_msg);
}

/* run_inet_raw_server()                                              */

/**
 * @pe scurt Start a brut TCP binar Server pentru direct proc_request_t I/O.
 *
 * Each accepted client este handled in a forked copil proces.
 * Uses POSIX citeste/scrie syscalls exclusively (no stdio).
 *
 * @param port  TCP port la asculta on (0 = use INET_RAW_PORT implicit)
 * @return 0 on clean shutdown, -1 on eroare
 */
int run_inet_raw_server(int port)
{
    if (port<=0) { port=INET_RAW_PORT; }

    int server_fd=socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[inetds] socket");
        return -1;
    }

    /* Allow reuse de port immediately after Server restarts */
    int opt=1;
    (void)setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr))!=0) {
        perror("[inetds] bind");
        (void)close(server_fd);
        return -1;
    }

    if (listen(server_fd, INET_BACKLOG)!=0) {
        perror("[inetds] listen");
        (void)close(server_fd);
        return -1;
    }

    (void)fprintf(stdout, "[inetds] Raw TCP server on port %d\n", port);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len=sizeof(client_addr);

        int client_fd=accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            if (errno==EINTR) { continue; }
            perror("[inetds] accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        (void)inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));

        pid_t pid=fork();
        if (pid < 0) {
            perror("[inetds] fork");
            (void)close(client_fd);
            continue;
        }
        if (pid==0) {
            /* copil */
            (void)close(server_fd);
            handle_inet_client(client_fd, client_ip);
            (void)close(client_fd);
            _exit(EXIT_SUCCESS);
        }
        /* parinte */
        (void)close(client_fd);
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            ;
        }
    }

    (void)close(server_fd);
    return 0;
}
