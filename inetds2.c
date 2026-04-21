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
#include <signal.h>
/* pentru gestionarea semnalelor */
#include <netinet/in.h>
/* pentru socket-uri internet */
#include <arpa/inet.h>
/* pentru conversii adrese IP */
#include <sys/types.h>
/* pentru tipuri de date sistem */
#include <sys/socket.h>
/* pentru operatii cu socket-uri */
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */

#include "processing.h"
/* pentru functii de procesare audio */
#include "server.h"
/* pentru definitii ale serverului */

/* Constants                                                           */
#define INET_RAW_PORT    9090   /**< brut TCP port (separate from soap) */
#define INET_BACKLOG     16

/* read_all / write_all: fiabil flux I/O via POSIX syscalls        */
static ssize_t read_all(int fd, void*buf, size_t len)
{
    char*ptr=(char *)buf;
    size_t  rem=len;
    while (rem > 0) {
        ssize_t n=read(fd, ptr, rem);
        if (n < 0) {
            if (errno==EINTR) continue;
            return -1;
        }
        if (n==0) return (ssize_t)(len - rem); /* EOF */
        ptr +=n;
        rem -=(size_t)n;
    }
    return (ssize_t)len;
}

static ssize_t write_all(int fd, const void*buf, size_t len)
{
    const char*ptr=(const char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t n=write(fd, ptr, rem);
        if (n < 0) {
            if (errno==EINTR) continue;
            return -1;
        }
        ptr +=n;
        rem -=(size_t)n;
    }
    return (ssize_t)len;
}

/* handle_inet_client                                                  */
static void handle_inet_client(int client_fd, const char*client_ip)
{
    fprintf(stdout, "[inetds] Client connected: %s\n", client_ip);

    /* citeste lungime-prefixat cerere */
    uint32_t req_len=0;
    if (read_all(client_fd, &req_len, sizeof(req_len))!=sizeof(req_len)) {
        fprintf(stderr, "[inetds] Failed to read request length\n");
        return;
    }
    req_len=ntohl(req_len); /* retea byte ordine → gazda */

    if (req_len!=sizeof(proc_request_t)) {
        fprintf(stderr, "[inetds] Bad request size: %u (expected %zu)\n",
                req_len, sizeof(proc_request_t));
        return;
    }

    proc_request_t req;
    if (read_all(client_fd, &req, sizeof(req))!=sizeof(req)) {
        perror("[inetds] read request");
        return;
    }

    /* aplica implicit pentru unset fields */
    if (req.target_sr<=0) req.target_sr=DEFAULT_SR;
    if (req.n_fft<=0) req.n_fft=DEFAULT_N_FFT;
    if (req.hop_length<=0) req.hop_length=DEFAULT_HOP_LENGTH;
    if (req.n_mels<=0) req.n_mels=DEFAULT_N_MELS;

    /* proces (fork intern) */
    proc_result_t res;
    memset(&res, 0, sizeof(res));
    process_spectrogram(&req, &res);

    /* trimite lungime-prefixat raspuns */
    uint32_t res_len_net=htonl((uint32_t)sizeof(proc_result_t));
    write_all(client_fd, &res_len_net, sizeof(res_len_net));
    write_all(client_fd, &res, sizeof(res));

    fprintf(stdout, "[inetds] Response sent: status=%d  msg=%s\n",
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
    if (port<=0) port=INET_RAW_PORT;

    int server_fd=socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[inetds] socket");
        return -1;
    }

    /* Allow reuse de port immediately after Server restarts */
    int opt=1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_addr.s_addr=INADDR_ANY;
    addr.sin_port=htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr))!=0) {
        perror("[inetds] bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, INET_BACKLOG)!=0) {
        perror("[inetds] listen");
        close(server_fd);
        return -1;
    }

    fprintf(stdout, "[inetds] Raw TCP server on port %d\n", port);

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len=sizeof(client_addr);

        int client_fd=accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);
        if (client_fd < 0) {
            if (errno==EINTR) continue;
            perror("[inetds] accept");
            break;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));

        pid_t pid=fork();
        if (pid < 0) {
            perror("[inetds] fork");
            close(client_fd);
            continue;
        }
        if (pid==0) {
            /* copil */
            close(server_fd);
            handle_inet_client(client_fd, client_ip);
            close(client_fd);
            exit(EXIT_SUCCESS);
        }
        /* parinte */
        close(client_fd);
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }

    close(server_fd);
    return 0;
}