#ifndef AI_KNOWLEDGE_H
#define AI_KNOWLEDGE_H

#include <stddef.h>

/* A small, reviewed offline reference for base Create mechanics. */
int ai_knowledge_is_create_topic(const char *text);

/* Return the vetted facts to pass as an extra system message for Create. */
const char *ai_knowledge_create_facts(void);

/* Handle a few known misinformation traps deterministically.
 * Returns 1 on a complete, vetted answer and 0 for ordinary model generation. */
int ai_knowledge_direct_answer(const char *question, char *answer, size_t capacity);

#endif
