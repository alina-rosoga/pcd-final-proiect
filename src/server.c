/**
 * PCD T31 - Audio Analysis SOAP Server
 * 
 * Architecture:
 *   - SOAP web service endpoint (gSOAP based)
 *   - Runtime configuration via libconfig
 *   - Audio/spectrogram processing with LibrosaC
 *   - Forked child processes for isolated work
 *   - POSIX syscalls for file operations (not stdio)
 * 
 * Build: see Makefile
 * Usage: ./serverds [-c config.cfg] [-p port]
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
#include <sys/types.h>
/* pentru tipuri de date sistem */
#include <sys/stat.h>
/* pentru informatii despre fisiere */
#include <sys/wait.h>
/* pentru asteptarea proceselor copil */
#include <fcntl.h>
/* pentru controlul descriptorilor de fisiere */
#include <getopt.h>

/* libconfig - runtime config parsare (mandatory) */
#include <libconfig.h>

/* gSOAP generated headers (from proto.h / soapH.h skeleton) */
#include "soapH.h"
/* pentru anteturile generate de gSOAP */
#include "proto.nsmap"
/* pentru maparea namespace-urilor in gSOAP */

/* Project headers */
#include "server.h"
/* pentru definitii ale serverului */
#include "processing.h"
/* pentru functii de procesare audio */
#include "config_loader.h"
/* pentru incarcarea configuratiilor */

/* Global Server state (set from config + CLI args)                   */
static server_config_t g_cfg;
static volatile sig_atomic_t g_running=1;

/* semnal handler - graceful shutdown                                  */
static void sigterm_handler(int sig)
{
    (void)sig;
    g_running=0;
    /* scrie() este async-semnal-safe; printf este NOT */
    const char msg[]="\n[server] Shutting down...\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

/* afiseaza utilizare                                                         */
static void print_usage(const char*prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -c FILE    Path to configuration file (default: server.cfg)\n"
        "  -p PORT    Listening port (overrides config)\n"
        "  -v         Verbose logging\n"
        "  -h         Show this help\n",
        prog);
}

/* soap serviciu dispatch loop                                          */
static int run_soap_server(struct soap*soap, const server_config_t*cfg)
{
    SOAP_SOCKET master, slave;

    master=soap_bind(soap, NULL, cfg->port, SOAP_BACKLOG);
    if (!soap_valid_socket(master)) {
        soap_print_fault(soap, stderr);
        return -1;
    }

    fprintf(stdout, "[server] Listening on port %d (SOAP endpoint: %s)\n",
            cfg->port, cfg->soap_endpoint);

    while (g_running) {
        slave=soap_accept(soap);
        if (!soap_valid_socket(slave)) {
            if (errno==EINTR) continue; /* interrupted by semnal */
            soap_print_fault(soap, stderr);
            break;
        }

        /* fork a copil la maneaza each client cerere independently */
        pid_t pid=fork();
        if (pid < 0) {
            perror("[server] fork");
            soap_closesock(soap);
            continue;
        }

        if (pid==0) {
            /* copil proces */
            soap_serve(soap);   /* dispatch soap cerere */
            soap_destroy(soap);
            soap_end(soap);
            soap_done(soap);
            exit(EXIT_SUCCESS);
        }

        /* parinte proces */
        soap_closesock(soap);

        /* Reap zombie children (non-blocking) */
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }

    /* Graceful Curatare: asteapta pentru all children */
    while (wait(NULL) > 0)
        ;

    return 0;
}

/* principal()                                                              */
int main(int argc, char*argv[])
{
    int opt;
    const char*cfg_path=DEFAULT_CONFIG_PATH;
    int port_override=0;
    int verbose=0;

    /* parseaza comanda-linie options */
    while ((opt=getopt(argc, argv, "c:p:vh"))!=-1) {
        switch (opt) {
        case 'c': cfg_path=optarg;            break;
        case 'p': port_override=atoi(optarg);      break;
        case 'v': verbose=1;                 break;
        case 'h': print_usage(argv[0]); return 0;
        default:  print_usage(argv[0]); return 1;
        }
    }

    /* incarca libconfig configuratie */
    if (config_load(cfg_path, &g_cfg)!=0) {
        fprintf(stderr, "[server] Failed to load config: %s\n", cfg_path);
        return 1;
    }

    if (port_override > 0)
        g_cfg.port=port_override;

    if (verbose) {
        fprintf(stdout, "[server] Config loaded from %s\n", cfg_path);
        fprintf(stdout, "[server] Port         : %d\n",   g_cfg.port);
        fprintf(stdout, "[server] Workers max  : %d\n",   g_cfg.max_workers);
        fprintf(stdout, "[server] Output dir   : %s\n",   g_cfg.output_dir);
        fprintf(stdout, "[server] SOAP endpoint: %s\n",   g_cfg.soap_endpoint);
    }

    /* Ensure iesire director exists */
    if (mkdir(g_cfg.output_dir, 0755)!=0 && errno!=EEXIST) {
        perror("[server] mkdir output_dir");
        return 1;
    }

    /* Install semnal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler=sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    signal(SIGCHLD, SIG_DFL); /* implicit SIGCHLD - children become zombies until reaped */

    /* Initialise gSOAP runtime */
    struct soap soap;
    soap_init(&soap);
    soap.send_timeout=SOAP_TIMEOUT_S;
    soap.recv_timeout=SOAP_TIMEOUT_S;
    soap.accept_timeout=1; /* 1s so SIGINT este responsive */
    soap.max_keep_alive=SOAP_MAX_KEEP_ALIVE;

    /* Run soap dispatch loop */
    int rc=run_soap_server(&soap, &g_cfg);

    /* Curatare */
    soap_destroy(&soap);
    soap_end(&soap);
    soap_done(&soap);
    config_free(&g_cfg);

    fprintf(stdout, "[server] Exited cleanly.\n");
    return (rc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
}