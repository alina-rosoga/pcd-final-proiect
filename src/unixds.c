/**
 * PCD T31 - Unix Domain Socket Server Variant
 * 
 * Provides a local IPC interface for the processing service,
 * useful for co-located clients on the same machine.
 * Mirrors the unixds pattern from the PCD lab skeleton.
 * 
 * Protocol: simple binary framing over a UNIX socket
 *   [uint32_t msg_len][proc_request_t or proc_result_t payload]
 * 
 * The main SOAP-over-TCP server in server.c is the primary interface.
 * This module is a secondary local interface / internal IPC channel.
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
#include <sys/socket.h>
/* pentru operatii cu socket-uri */
#include <sys/un.h>
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */
#include <stdint.h>
/* pentru uint32_t */

#include "processing.h"
/* pentru functii de procesare audio */

/* Constants                                                           */
#define UNIX_SOCKET_PATH  "/tmp/pcd_t31.sock"
#define UNIX_BACKLOG      8
#define LOG_BUFSIZE       512

#define SAFE_MEMSET(dst, val, sz) (void)memset((dst), (val), (sz))
#define SAFE_MEMCPY(dst, src, sz) (void)memcpy((dst), (src), (sz))

/* unix_recv_all: fiabil primeste over a flux socket               */
static int unix_recv_all(int sock_fd, void*buf, size_t len)
{
    char*ptr=(char *)buf;
    size_t rem=len;
    while (rem > 0) {
        ssize_t num_bytes=read(sock_fd, ptr, rem);
        if (num_bytes < 0) {
            if (errno==EINTR) { continue; }
            return -1;
        }
        if (num_bytes==0) { return -1; } /* EOF - client disconnected */
        ptr +=num_bytes;
        rem -=(size_t)num_bytes;
    }
    return 0;
}

/* unix_send_all: fiabil trimite over a flux socket                  */
static int unix_send_all(int sock_fd, const void*buf, size_t len)
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
    return 0;
}

/* handle_unix_client: proces one client conexiune                  */
/* Called in a forked copil.                                          */
static void handle_unix_client(int client_fd)
{
    /* primeste cerere lungime prefix */
    uint32_t msg_len=0;
    if (unix_recv_all(client_fd, &msg_len, sizeof(msg_len))!=0) {
        perror("[unixds] recv msg_len");
        return;
    }

    if (msg_len!=sizeof(proc_request_t)) {
        char buf[LOG_BUFSIZE];
        (void)snprintf(buf, sizeof(buf), "[unixds] Unexpected msg_len %u (expected %zu)\n",
                msg_len, sizeof(proc_request_t));
        fputs(buf, stderr);
        return;
    }

    /* primeste proc_request_t */
    proc_request_t req;
    if (unix_recv_all(client_fd, &req, sizeof(req))!=0) {
        perror("[unixds] recv request");
        return;
    }

    /* proces */
    proc_result_t res;
    (void)memset(&res, 0, sizeof(res));
    (void)process_spectrogram(&req, &res);

    /* trimite rezultat lungime prefix + rezultat */
    uint32_t res_len=(uint32_t)sizeof(proc_result_t);
    (void)unix_send_all(client_fd, &res_len, sizeof(res_len));
    (void)unix_send_all(client_fd, &res,     sizeof(res));
}

/* run_unix_domain_server()                                           */

/**
 * @pe scurt Start a Unix Domain socket Server pentru local IPC.
 *
 * Accepts proc_request_t messages si responds with proc_result_t.
 * Each client este handled in a forked copil proces.
 *
 * @return 0 on clean shutdown, -1 on setup eroare
 */
int run_unix_domain_server(void)
{
    /* Remove stale socket fisier */
    (void)unlink(UNIX_SOCKET_PATH);

    int server_fd=socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        (void)perror("[unixds] socket");
        return -1;
    }

    struct sockaddr_un addr;
    (void)memset(&addr, 0, sizeof(addr));
    addr.sun_family=AF_UNIX;
    (void)memcpy(addr.sun_path, UNIX_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1]='\0';

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr))!=0) {
        perror("[unixds] bind");
        (void)close(server_fd);
        return -1;
    }

    if (listen(server_fd, UNIX_BACKLOG)!=0) {
        perror("[unixds] listen");
        (void)close(server_fd);
        return -1;
    }

    fputs("[unixds] Listening on ", stdout);
    fputs(UNIX_SOCKET_PATH, stdout);
    fputs("\n", stdout);

    for (;;) {
        int client_fd=accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno==EINTR) { continue; }
            perror("[unixds] accept");
            break;
        }

        pid_t pid=fork();
        if (pid < 0) {
            perror("[unixds] fork");
            (void)close(client_fd);
            continue;
        }
        if (pid==0) {
            /* copil */
            (void)close(server_fd);
            handle_unix_client(client_fd);
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
    (void)unlink(UNIX_SOCKET_PATH);
    return 0;
}
