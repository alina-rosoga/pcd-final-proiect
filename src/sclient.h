/**
 * PCD T31 - Shared Client Utilities Header
 * 
 * Common definitions used by both the SOAP client (client.c) and
 * the raw TCP client (inetsample2.c).
 */

#ifndef SCLIENT_H
#define SCLIENT_H

#include <stdint.h>
/* fixed-width integer types */
#include "processing.h"

/* Raw TCP client API (inetds2 protocol) */

/**
 * Send a proc_request_t to an inetds raw TCP server and
 * receive the proc_result_t response.
 *
 * @param host    Server hostname or IP string
 * @param port    Server port
 * @param req     Request to send
 * @param res     Response populated on success
 * @return        0 on success, -1 on error
 */
int raw_client_call(const char*host,
                    int                  port,
                    const proc_request_t*req,
                    proc_result_t*res);

/* Common CLI helpers */

/**
 * Display proc_result_t to stdout in human-readable format.
 * @param res   Result to display
 */
void print_proc_result(const proc_result_t*res);

#endif /* SCLIENT_H */
