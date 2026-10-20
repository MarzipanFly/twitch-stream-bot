#ifndef AI_SEMANTIC_H
#define AI_SEMANTIC_H
#include <stddef.h>

/* Optional semantic RAG via locally hosted Ollama /api/embed. All state is
 * owned by the single AI worker thread; no other thread calls search. */
void ai_semantic_init(int enabled, const char *model);
int ai_semantic_search(const char *question, const char *previous_question,
                       char *output, size_t capacity);
#endif
