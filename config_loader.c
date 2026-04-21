/**
 * PCD T31 - libconfig-based Configuration Loading
 * 
 * Loads server configuration from config files using libconfig.
 * All fields have sensible defaults so the server can start
 * even with minimal or missing configuration.
 */

#include <stdio.h>  /* Standard I/O */
#include <string.h> /* String functions */
#include <libconfig.h>

#include "server.h"
#include "config_loader.h"

/* Load configuration from a libconfig file */
int config_load(const char*path, server_config_t*out)
{
    config_t cfg;
    config_init(&cfg);

    /* Set defaults first */
    out->port=DEFAULT_PORT;
    out->max_workers=MAX_WORKERS_DEFAULT;
    out->log_level=1;
    strncpy(out->output_dir,    "./output",                   MAX_PATH_LEN - 1);
    strncpy(out->soap_endpoint, "http://localhost:8080/soap", MAX_PATH_LEN - 1);

    if (config_read_file(&cfg, path)==CONFIG_FALSE) {
        fprintf(stderr, "[config] Warning: %s:%d - %s  (using defaults)\n",
                config_error_file(&cfg),
                config_error_line(&cfg),
                config_error_text(&cfg));
        config_destroy(&cfg);
        return 0; /* Non-fatal: run with defaults */
    }

    /* Server.port */
    int port_val;
    if (config_lookup_int(&cfg, "server.port", &port_val)==CONFIG_TRUE)
        out->port=port_val;

    /* Server.max_workers */
    int workers_val;
    if (config_lookup_int(&cfg, "server.max_workers", &workers_val)==CONFIG_TRUE)
        out->max_workers=workers_val;

    /* Server.log_level */
    int log_val;
    if (config_lookup_int(&cfg, "server.log_level", &log_val)==CONFIG_TRUE)
        out->log_level=log_val;

    /* Server.output_dir */
    const char*odir;
    if (config_lookup_string(&cfg, "server.output_dir", &odir)==CONFIG_TRUE)
        strncpy(out->output_dir, odir, MAX_PATH_LEN - 1);

    /* Server.soap_endpoint */
    const char*ep;
    if (config_lookup_string(&cfg, "server.soap_endpoint", &ep)==CONFIG_TRUE)
        strncpy(out->soap_endpoint, ep, MAX_PATH_LEN - 1);

    config_destroy(&cfg);
    return 0;
}

/* Release resources held by config */
void config_free(server_config_t*cfg)
{
    /* Nothing heap-allocated in struct; kept for future expansion */
    (void)cfg;
}