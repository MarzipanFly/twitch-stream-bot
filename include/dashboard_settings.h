#ifndef DASHBOARD_SETTINGS_H
#define DASHBOARD_SETTINGS_H
#include "config.h"
/* Load only whitelisted overrides. Secrets are Windows-DPAPI encrypted. */
void dashboard_settings_load(AppConfig *config);
/* Validate/apply single setting and persist. 0: invalid or save error. */
int dashboard_settings_apply(AppConfig *config, const char *key,
                             const char *value);
#endif
