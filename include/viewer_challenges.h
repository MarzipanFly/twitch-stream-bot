#ifndef VIEWER_CHALLENGES_H
#define VIEWER_CHALLENGES_H

#include "viewer_profile.h"
#include <stddef.h>

#define CHALLENGE_QUEUE_MAX 8
#define CHALLENGE_ITEMS_MAX (CHALLENGE_QUEUE_MAX + 1)
#define CHALLENGE_PATH "data/challenges.dat"

typedef enum {
    CHALLENGE_GAME_NONE = 0,
    CHALLENGE_GAME_ETS2 = 1,
    CHALLENGE_GAME_FC = 2,
    CHALLENGE_GAME_PHASMO = 3
} ChallengeGame;

typedef struct {
    unsigned int id;
    ChallengeGame game;
    unsigned int task; /* 0-3: fixed, safe challenge catalog */
    int active;
    int paid;
    char user_id[VIEWER_PROFILE_ID_SIZE];
    char name[VIEWER_PROFILE_NAME_SIZE];
} ViewerChallenge;

typedef struct {
    ViewerChallenge items[CHALLENGE_ITEMS_MAX];
    size_t count;
    unsigned int next_id;
} ViewerChallenges;

typedef enum {
    CHALLENGE_ERROR = 0,
    CHALLENGE_OK = 1,
    CHALLENGE_FULL = -1,
    CHALLENGE_DUPLICATE = -2,
    CHALLENGE_NO_BALANCE = -3,
    CHALLENGE_NO_TASK = -4,
    CHALLENGE_ALREADY_ACTIVE = -5,
    CHALLENGE_IO_ERROR = -6
} ChallengeResult;

int viewer_challenges_init(ViewerChallenges *queue);
int viewer_challenges_save(const ViewerChallenges *queue);
ChallengeGame viewer_challenges_game(const char *text);
const char *viewer_challenges_game_name(ChallengeGame game);
const char *viewer_challenges_task(ChallengeGame game, unsigned int index);
long long viewer_challenges_price(ChallengeGame game);
ChallengeResult viewer_challenges_enqueue(ViewerChallenges *queue,
        ViewerProfile *viewer, ChallengeGame game, unsigned int task,
        ViewerChallenge *chosen);
ChallengeResult viewer_challenges_accept(ViewerChallenges *queue,
        ViewerChallenge *selected);
ChallengeResult viewer_challenges_finish(ViewerChallenges *queue,
        ViewerChallenge *finished);
ChallengeResult viewer_challenges_decline(ViewerChallenges *queue,
        ViewerChallenge *declined, int *refunded);
/* One-line queue overview for Twitch responses. */
void viewer_challenges_overview(const ViewerChallenges *queue,
                               char *out, size_t capacity);

#endif
