#include "ai_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int valid_utf8(const unsigned char *p)
{
    while (*p)
    {
        unsigned int count = 1, i;
        if (*p < 0x80) count = 1;
        else if ((*p & 0xE0) == 0xC0) count = 2;
        else if ((*p & 0xF0) == 0xE0) count = 3;
        else if ((*p & 0xF8) == 0xF0) count = 4;
        else return 0;
        for (i = 1; i < count; ++i)
            if (!p[i] || (p[i] & 0xC0) != 0x80) return 0;
        p += count;
    }
    return 1;
}

int main(void)
{
    char clean[801], chunk[431], combined[801] = "";
    char long_text[801];
    size_t cursor = 0, total = 0, i;
    int pieces = 0;
    ai_text_clean_reply("**Грузовики**  *Volvo FM* `ETS 2`\nи **Scania**!",
                        clean, sizeof(clean));
    assert(strcmp(clean, "Грузовики Volvo FM ETS 2 и Scania!") == 0);
    ai_text_clean_reply("2*3 = 6", clean, sizeof(clean));
    assert(strcmp(clean, "2*3 = 6") == 0);

    for (i = 0; i + 3 < sizeof(long_text); i += 3)
    {
        long_text[i] = (char)0xD0;
        long_text[i + 1] = (char)0x90; /* Russian capital А */
        long_text[i + 2] = ' ';
    }
    long_text[i] = '\0';
    ai_text_clean_reply(long_text, clean, sizeof(clean));
    assert(strlen(clean) > 430);
    while (ai_text_next_part(clean, &cursor, chunk, sizeof(chunk), 420))
    {
        size_t len = strlen(chunk);
        assert(len <= 420);
        assert(valid_utf8((const unsigned char *)chunk));
        if (pieces) strcat(combined, " ");
        strcat(combined, chunk);
        total += len;
        ++pieces;
        assert(pieces <= 2);
    }
    assert(pieces == 2);
    assert(strcmp(combined, clean) == 0);

    ai_text_clean_reply("FC 26: Начни с 4-2-3-1. Держи два опорника. А ещё поставь централ",
                        clean, sizeof(clean));
    ai_text_finish_reply(clean, sizeof(clean));
    assert(strcmp(clean, "FC 26: Начни с 4-2-3-1. Держи два опорника.") == 0);
    ai_text_clean_reply("Пробуй фланговую схе", clean, sizeof(clean));
    ai_text_finish_reply(clean, sizeof(clean));
    assert(strcmp(clean, "Пробуй фланговую.") == 0);
    ai_text_clean_reply("Уже законченный ответ.", clean, sizeof(clean));
    ai_text_finish_reply(clean, sizeof(clean));
    assert(strcmp(clean, "Уже законченный ответ.") == 0);

    cursor = 0;
    assert(ai_text_next_part("Привет, мир!", &cursor, chunk, sizeof(chunk), 420));
    assert(!ai_text_next_part("Привет, мир!", &cursor, chunk, sizeof(chunk), 420));
    printf("ai_text tests passed (%d chunks; %lu bytes)\n",
           pieces, (unsigned long)total);
    return 0;
}
