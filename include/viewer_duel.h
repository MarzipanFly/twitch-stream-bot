#ifndef VIEWER_DUEL_H
#define VIEWER_DUEL_H

#include <stddef.h>

#define VIEWER_DUEL_TIMEOUT_SECONDS 60

void viewer_duel_init(void);

int viewer_duel_create(
    const char *challenger_user_id,
    const char *challenger_name,
    const char *target_login,
    long long bet
);

int viewer_duel_accept(
    const char *target_user_id,
    const char *target_login,
    const char *target_name,
    char *challenger_user_id,
    size_t challenger_user_id_size,
    char *challenger_name,
    size_t challenger_name_size,
    long long *bet
);

int viewer_duel_decline(
    const char *target_login,
    char *challenger_name,
    size_t challenger_name_size,
    long long *bet
);

#endif
