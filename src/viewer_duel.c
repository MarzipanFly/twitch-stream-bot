#include "viewer_duel.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define VIEWER_DUEL_MAX_PENDING 128
#define VIEWER_DUEL_ID_SIZE 64
#define VIEWER_DUEL_LOGIN_SIZE 64
#define VIEWER_DUEL_NAME_SIZE 128

typedef struct
{
    int active;
    char challenger_user_id[VIEWER_DUEL_ID_SIZE];
    char challenger_name[VIEWER_DUEL_NAME_SIZE];
    char target_login[VIEWER_DUEL_LOGIN_SIZE];
    long long bet;
    time_t created_at;
} ViewerDuel;

static ViewerDuel g_duels[VIEWER_DUEL_MAX_PENDING];

static int ascii_equals_ignore_case(const char *left, const char *right)
{
    unsigned char a;
    unsigned char b;

    if (left == NULL || right == NULL)
    {
        return 0;
    }

    while (*left != '\0' && *right != '\0')
    {
        a = (unsigned char)*left;
        b = (unsigned char)*right;

        if (tolower(a) != tolower(b))
        {
            return 0;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}

static void cleanup_expired(void)
{
    size_t i;
    time_t now = time(NULL);

    for (i = 0; i < VIEWER_DUEL_MAX_PENDING; ++i)
    {
        if (
            g_duels[i].active &&
            difftime(now, g_duels[i].created_at) >=
                VIEWER_DUEL_TIMEOUT_SECONDS)
        {
            memset(&g_duels[i], 0, sizeof(g_duels[i]));
        }
    }
}

void viewer_duel_init(void)
{
    memset(g_duels, 0, sizeof(g_duels));
}

int viewer_duel_create(
    const char *challenger_user_id,
    const char *challenger_name,
    const char *target_login,
    long long bet
)
{
    size_t i;
    ViewerDuel *free_slot = NULL;

    if (
        challenger_user_id == NULL ||
        challenger_name == NULL ||
        target_login == NULL ||
        target_login[0] == '\0' ||
        bet <= 0)
    {
        return 0;
    }

    cleanup_expired();

    for (i = 0; i < VIEWER_DUEL_MAX_PENDING; ++i)
    {
        if (!g_duels[i].active)
        {
            if (free_slot == NULL)
            {
                free_slot = &g_duels[i];
            }

            continue;
        }

        if (
            strcmp(
                g_duels[i].challenger_user_id,
                challenger_user_id
            ) == 0)
        {
            return 0;
        }

        if (
            ascii_equals_ignore_case(
                g_duels[i].target_login,
                target_login
            ))
        {
            return 0;
        }
    }

    if (free_slot == NULL)
    {
        return 0;
    }

    memset(free_slot, 0, sizeof(*free_slot));

    free_slot->active = 1;
    snprintf(
        free_slot->challenger_user_id,
        sizeof(free_slot->challenger_user_id),
        "%s",
        challenger_user_id
    );
    snprintf(
        free_slot->challenger_name,
        sizeof(free_slot->challenger_name),
        "%s",
        challenger_name
    );
    snprintf(
        free_slot->target_login,
        sizeof(free_slot->target_login),
        "%s",
        target_login
    );

    free_slot->bet = bet;
    free_slot->created_at = time(NULL);

    return 1;
}

int viewer_duel_accept(
    const char *target_user_id,
    const char *target_login,
    const char *target_name,
    char *challenger_user_id,
    size_t challenger_user_id_size,
    char *challenger_name,
    size_t challenger_name_size,
    long long *bet
)
{
    size_t i;

    (void)target_user_id;
    (void)target_name;

    if (
        target_login == NULL ||
        challenger_user_id == NULL ||
        challenger_name == NULL ||
        bet == NULL)
    {
        return 0;
    }

    cleanup_expired();

    for (i = 0; i < VIEWER_DUEL_MAX_PENDING; ++i)
    {
        if (
            g_duels[i].active &&
            ascii_equals_ignore_case(
                g_duels[i].target_login,
                target_login
            ))
        {
            snprintf(
                challenger_user_id,
                challenger_user_id_size,
                "%s",
                g_duels[i].challenger_user_id
            );
            snprintf(
                challenger_name,
                challenger_name_size,
                "%s",
                g_duels[i].challenger_name
            );

            *bet = g_duels[i].bet;

            memset(
                &g_duels[i],
                0,
                sizeof(g_duels[i])
            );

            return 1;
        }
    }

    return 0;
}

int viewer_duel_decline(
    const char *target_login,
    char *challenger_name,
    size_t challenger_name_size,
    long long *bet
)
{
    size_t i;

    if (
        target_login == NULL ||
        challenger_name == NULL ||
        bet == NULL)
    {
        return 0;
    }

    cleanup_expired();

    for (i = 0; i < VIEWER_DUEL_MAX_PENDING; ++i)
    {
        if (
            g_duels[i].active &&
            ascii_equals_ignore_case(
                g_duels[i].target_login,
                target_login
            ))
        {
            snprintf(
                challenger_name,
                challenger_name_size,
                "%s",
                g_duels[i].challenger_name
            );

            *bet = g_duels[i].bet;

            memset(
                &g_duels[i],
                0,
                sizeof(g_duels[i])
            );

            return 1;
        }
    }

    return 0;
}
