#ifndef AI_GAME_SCOPE_H
#define AI_GAME_SCOPE_H

typedef enum {
    AI_GAME_NONE = 0,
    AI_GAME_CREATE,
    AI_GAME_ETS2,
    AI_GAME_FC,
    AI_GAME_PHASMO,
    AI_GAME_WOT,
    AI_GAME_WAR_THUNDER,
    AI_GAME_VALORANT,
    AI_GAME_CS2,
    AI_GAME_LOL,
    AI_GAME_ROCKET_LEAGUE,
    AI_GAME_FACTORIO,
    AI_GAME_SATISFACTORY,
    AI_GAME_STARDEW,
    AI_GAME_DBD
} AiGameScope;

/* Detect explicit game names in viewer questions, not implicit assumptions.
 * Case-insensitive for Russian/English game names. */
AiGameScope ai_game_scope_from_text(const char *text);
/* Assign the local fact's title to a game namespace, when possible. */
AiGameScope ai_game_scope_from_title(const char *title);

#endif
