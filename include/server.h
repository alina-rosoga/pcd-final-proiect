/**
 * PCD T31 - Server shared constants and types
 */
#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
/* fixed-width integer types */

#define DEFAULT_PORT         8080
#define DEFAULT_CONFIG_PATH  "config/server.cfg"
#define MAX_PATH_LEN         512
#define MAX_WORKERS_DEFAULT  4
#define SOAP_BACKLOG         10
#define SOAP_TIMEOUT_S       30
#define SOAP_MAX_KEEP_ALIVE  100

typedef struct {
    int  port;
    int  max_workers;
    int  log_level;
    char output_dir[MAX_PATH_LEN];
    char soap_endpoint[MAX_PATH_LEN];
} server_config_t;

#endif /* SERVER_H */
