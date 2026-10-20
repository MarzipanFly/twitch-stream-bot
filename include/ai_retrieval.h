#ifndef AI_RETRIEVAL_H
#define AI_RETRIEVAL_H
#include <stddef.h>
/* Search local, maintainer-reviewed UTF-8 TSV facts. Returns matching
 * reference passages, not chat-user-supplied instructions. */
int ai_retrieval_search(const char *question, const char *previous_question,
                        char *out, size_t capacity);
#endif
