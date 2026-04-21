/**
 * PCD T31 - libconfig loader public API
 */

#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "server.h"

/**
 * Load a libconfig .cfg file into server_config_t.
 * On parse error, defaults are kept and 0 is returned (non-fatal).
 * @return 0 on success or benign error (defaults used), -1 on fatal error.
 */
int  config_load(const char*path, server_config_t*out);

/**
 * Release resources held by server_config_t.
 * Currently a no-op; present for future expansion.
 */
void config_free(server_config_t*cfg);

#endif /* CONFIG_LOADER_H */