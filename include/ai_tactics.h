#ifndef AI_TACTICS_H
#define AI_TACTICS_H

#include <stddef.h>

/*
 * Practical soccer/EA FC decision rules, with explicit scenario detection.
 * Previous questions are from the SAME viewer's memory, oldest first.
 * No Twitch/Windows dependency: callable in offline regression tests.
 *
 * Returns 1 only for a recognized tactical situation, else 0 (use Qwen).
 */
int ai_tactics_reply(const char *question, const char *const *previous_questions,
                     size_t previous_count, char *answer, size_t answer_capacity);
#endif
