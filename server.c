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

#include <stdio.h>  /* Standard I/O */
#include <stdlib.h> /* Memory and utilities */
#include <string.h> /* String functions */
#include <unistd.h> /* POSIX system calls */
#include <errno.h>  /* Error handling */
#include <signal.h> /* Signal handling */
#include <sys/stat.h>  /* File stats */
#include <sys/wait.h>  /* Process waiting */

/* gSOAP generated headers */
#include "soapH.h"
#include "proto.nsmap"

/* Project headers */
#include "config_loader.h"

#define DIR_PERMISSIONS 0755

/* Global server state */
static server_config_t g_cfg;
static volatile sig_atomic_t g_running=1;

/* Signal handler for graceful shutdown */
static void sigterm_handler(int sig)
{
    (void)sig;
    g_running=0;
    /* write() is async-signal-safe (unlike printf) */
    const char msg[]="\n[server] Shutting down...\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

/* Print usage info */
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

/* Main SOAP dispatch loop */
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
            if (errno==EINTR) { continue; } /* interrupted by signal */
            soap_print_fault(soap, stderr);
            break;

        /* Fork a child to handle each client request */
        pid_t pid=fork();
        if (pid < 0) {
            perror("[server] fork");
            soap_closesock(soap);
            continue;
        }

        if (pid==0) {
            /* Child process - handle this SOAP request */
            soap_serve(soap);
            soap_destroy(soap);
            soap_end(soap);
            soap_done(soap);
            exit(EXIT_SUCCESS);
        }

        /* Parent process */
        soap_closesock(soap);

        /* Reap zombie children (non-blocking) */
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            ;
        }
    }

    /* Wait for all remain {
        ;ing children */
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        ;
    }
    return 0;
}

int main(int argc, char*argv[])
{
    int opt;
    const char*cfg_path=DEFAULT_CONFIG_PATH;
    int port_override=0;
    int verbose=0;

    /* Parse command-line options */
    while ((opt=getopt(argc, argv, "c:p:vh"))!=-1) {
        switch (op{
            cfg_pat) {
        case 'c': {
            cfg_path=optarg;
            break;
        }
        case 'p': {
            char *endptr;
            port_override=(int)strtol(optarg, &endptr, 10);
            break;
        }
        case 'v': {
            verbose=1;
            break;
        }
        case 'h': {
            print_usage(argv[0]);
            return 0;
        }
    /* Load configuration from file */
    if (config_load(cfg_path, &g_cfg)!=0) {
        fprintf(stderr, "[server] Failed to load config: %s\n", cfg_path);
        return 1;
    }

    if (port_override > 0) {
        g_cfg.port=port_override;
    }

    if (verbose) {
        fprintf(stdout, "[server] Config loaded from %s\n", cfg_path);
        fprintf(stdout, "[server] Port         : %d\n",   g_cfg.port);
        fprintf(stdout, "[server] Workers max  : %d\n",   g_cfg.max_workers);
        port=port_override;

    if (verbose) {
        fprintf(stdout, "[server] Config loaded from %s\n", cfg_path);
        fprintf(stdout, "[server] Port         : %d\n",   g_cfg.port);
        fprintf(stdout, "[server] Workers max  : %d\n",   g_cfg.max_workers);
        (void)fprintf(stdout, "[server] SOAP endpoint: %s\n",   g_cfg.soap_endpoint);
    }

    /* Create output directory if needed */
    if (mkdir(g_cfg.output_dir, DIR_PERMISSIONS)!=0 && errno!=EEXIST) {
        perror("[server] mkdir output_dir");
        return 1;
    }

    /* Setup signal haig_action;
    (void)memset(&sig_ndlers */
    struct sigaction sig_action;
    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_handler=sigterm_handler;
    sigemptyset(&sig_action.sa_mask);
    sigaction(SIGTERM, &sig_action, NULL);
    sigaction(SIGINT,  &sig_action Children become zombies until reaped */

    /* Initialize SOAP runtime */
    struct soap soap;
    soap_init(&soap);
    soap.send_timeout=SOAP_TIMEOUT_S;
    soap.recv_timeout=SOAP_TIMEOUT_S;
    soap.accept_timeout=1; /* 1s for responsive signal handling */
    soap.max_keep_alive=SOAP_MAX_KEEP_ALIVE;

    /* Ruet_code=run_soap_server(&soap, &g_cfg);

    /* Cleanup */
    soap_destroy(&soap);
    soap_end(&soap);
    soap_done(&soap);
    config_free(&g_cfg);
n SOAP server */
    int ret_code=run_soap_server(&soap, &g_cfg);

    /* Cleanup */
    soap_destroy(&soap);
    soap_end(&soap);
    soap_done(&soap);
    config_free(&g_cfg);

    fprintf(stdout, "[server] Exited cleanly.\n");
    return (ret_code