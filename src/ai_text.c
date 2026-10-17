#include "ai_text.h"

#include <string.h>

/* Clean plain-text Twitch responses; Twitch does not render Markdown. */
void ai_text_clean_reply(const char *source, char *out, size_t capacity)
{
    const unsigned char *p = (const unsigned char *)source;
    size_t used = 0;
    int space = 0;
    if (!out || capacity == 0) return;
    out[0] = '\0';
    if (!p) return;

    while (*p && used + 1 < capacity)
    {
        size_t n = 1;
        size_t i;
        if (*p <= 0x20 || *p == 0x7f)
        {
            if (used) space = 1;
            ++p;
            continue;
        }

        /* Remove Markdown bold/italic/code markers.
         * Keep arithmetic 2*3 intact; other asterisks are formatting. */
        if (*p == '`' ||
            (*p == '*' &&
             !(p > (const unsigned char *)source &&
               p[-1] >= '0' && p[-1] <= '9' &&
               p[1] >= '0' && p[1] <= '9')))
        {
            ++p;
            continue;
        }

        if ((*p & 0xe0) == 0xc0) n = 2;
        else if ((*p & 0xf0) == 0xe0) n = 3;
        else if ((*p & 0xf8) == 0xf0) n = 4;
        else if (*p >= 0x80) { ++p; continue; }
        for (i = 1; i < n; ++i)
            if (!p[i] || (p[i] & 0xc0) != 0x80) break;
        if (i != n) { ++p; continue; }
        if (used + (space ? 1 : 0) + n >= capacity) break;
        if (space) out[used++] = ' ';
        space = 0;
        memcpy(out + used, p, n);
        used += n;
        p += n;
    }
    out[used] = '\0';
}

/* One Twitch message must remain under 500 bytes including @login and label. */
int ai_text_next_part(const char *text, size_t *cursor,
                      char *part, size_t capacity, size_t max_bytes)
{
    size_t begin, end, length, chosen, search;
    if (!text || !cursor || !part || capacity < 2 || max_bytes == 0)
        return 0;
    part[0] = '\0';
    length = strlen(text);
    begin = *cursor;
    if (begin > length) return 0;
    while (begin < length && text[begin] == ' ') ++begin;
    if (begin == length) { *cursor = begin; return 0; }

    if (max_bytes >= capacity) max_bytes = capacity - 1;
    end = begin + max_bytes;
    if (end < begin || end > length) end = length;
    if (end < length)
    {
        /* An end offset must point to the start of a UTF-8 codepoint. */
        while (end > begin &&
               (((unsigned char)text[end] & 0xc0) == 0x80))
            --end;
        if (end == begin) return 0;

        chosen = end;
        search = end;
        /* Prefer whitespace close to the byte limit, without wasting a
         * whole line of output or splitting a UTF-8 sequence. */
        while (search > begin && end - search < 60)
        {
            if (text[search] == ' ')
            {
                chosen = search;
                break;
            }
            --search;
        }
        end = chosen;
    }
    if (end <= begin) return 0;
    memcpy(part, text + begin, end - begin);
    part[end - begin] = '\0';
    *cursor = end;
    while (*cursor < length && text[*cursor] == ' ') ++*cursor;
    return 1;
}
