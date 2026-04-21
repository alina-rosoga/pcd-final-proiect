/**
 * PCD T31 - libconfig loader public API
 */
#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include "server.h"

int  config_load(const char*path, server_config_t*out);
void config_free(server_config_t*cfg);

#endif /* CONFIG_LOADER_H */