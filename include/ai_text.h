#ifndef AI_TEXT_H
#define AI_TEXT_H

#include <stddef.h>

/* Normalize LLM output for Twitch: remove Markdown emphasis, flatten
 * whitespace and keep complete UTF-8 codepoints. */
void ai_text_clean_reply(const char *source, char *out, size_t capacity);

/* Copy the next bounded chunk of a cleaned UTF-8 reply.
 * Updates *cursor; returns nonzero if a chunk was produced.
 * Prefers a word break within the final 60 bytes of each chunk. */
int ai_text_next_part(const char *text, size_t *cursor,
                      char *part, size_t capacity, size_t max_bytes);

#endif
