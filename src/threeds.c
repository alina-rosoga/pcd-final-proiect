/**
 * PCD T31 - Optional Threaded SOAP Server Variant
 * 
 * An alternative to the fork-per-client model in server.c.
 * Uses a fixed-size thread pool to handle concurrent SOAP requests
 * without the overhead of fork().
 * 
 * This mirrors the threeds pattern from the PCD lab skeleton.
 * 
 * NOTE: For milestone 1, the fork() model (server.c) is primary.
 *       This file is provided as reference / future expansion.
 * 
 * Compile with: -lpthread
 */

#include <stdio.h>
/* pentru functii de intrare/iesire standard */
#include <stdlib.h>
/* pentru alocare de memorie si functii utilitare */
#include <string.h>
/* pentru manipularea sirurilor de caractere */
#include <pthread.h>
/* pentru fir-uri POSIX */

#include "soapH.h"
/* pentru anteturile generate de gSOAP */
#include "proto.nsmap"
/* pentru maparea namespace-urilor in gSOAP */

#define LOG_BUFSIZE 512

#define SAFE_MEMSET(dst, val, sz) (void)memset((dst), (val), (sz))
#define SAFE_MEMCPY(dst, src, sz) (void)memcpy((dst), (src), (sz))

/* fir pool worker argument                                         */
typedef struct {
    struct soap*soap;  /**< Per-fir soap copy with accepted socket */
} thread_arg_t;

/* thread_worker: soap cerere handler running in a pthread           */
static void*thread_worker(void*arg)
{
    thread_arg_t*targ=(thread_arg_t *)arg;
    struct soap*soap=targ->soap;
    free(targ);

    /* Detach so no join needed - resources released on iesire */
    (void)pthread_detach(pthread_self());

    soap_serve(soap);
    soap_destroy(soap);
    soap_end(soap);
    soap_free(soap);   /* elibereaza implicitul copy allocated in principal loop */

    return NULL;
}

/* run_threaded_soap_server()                                         */

/**
 * @pe scurt Run implicitul soap Server using one fir per accepted conexiune.
 *
 * @param master_soap   Initialised soap struct (already bound + listening)
 * @param max_threads   Upper bound on concurrent threads (not enforced
 *                      in acest simple variant — use a semaphore pentru that)
 * @return 0 on clean shutdown, -1 on eroare
 */
int run_threaded_soap_server(struct soap*master_soap, int max_threads)
{
    (void)max_threads;  /* TODO: implement semaphore-based throttle */

    (void)fprintf(stdout, "[threeds] Thread-per-request SOAP server started\n");

    for (;;) {
        /* Block until a client Conecteaza */
        SOAP_SOCKET slave=soap_accept(master_soap);
        if (!soap_valid_socket(slave)) {
            soap_print_fault(master_soap, stderr);
            return -1;
        }

        /* aloca a soap copy pentru implicitul new fir */
        struct soap*tsoap=soap_copy(master_soap);
        if (!tsoap) {
            fputs("[threeds] soap_copy failed\n", stderr);
            soap_closesock(master_soap);
            continue;
        }

        thread_arg_t*targ=malloc(sizeof(thread_arg_t));
        if (!targ) {
            perror("[threeds] malloc");
            soap_free(tsoap);
            continue;
        }
        targ->soap=tsoap;

        pthread_t tid;
        if (pthread_create(&tid, NULL, thread_worker, targ)!=0) {
            perror("[threeds] pthread_create");
            free(targ);
            soap_free(tsoap);
        }
        /* pthread_detach called inside thread_worker */
    }

    return 0;
}
