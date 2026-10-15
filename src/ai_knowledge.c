#include "ai_knowledge.h"

#include <stdio.h>
#include <string.h>

/*
 * Curated facts, not a live search engine or a comprehensive game wiki.
 * Sources checked 2026-10-15:
 * https://github.com/Creators-of-Create/Create/wiki/Andesite-Alloy
 * https://github.com/Creators-of-Create/Create/wiki/Generate-and-Convey
 * https://github.com/Creators-of-Create/Create/wiki/Mechanical-Press
 * https://www.curseforge.com/minecraft/mc-mods/create-mixed-stone
 *
 * Recipes differ between Create releases and modpacks; give ingredients
 * and general use, not exact item counts unless a version is known.
 */

/* ASCII and uppercase Cyrillic (Russian, including Ё) -> lowercase UTF-8.
 * Everything else is copied byte for byte. No locale or external library. */
static void make_searchable(const char *text, char *out, size_t capacity)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t used = 0;
    if (!out || !capacity) return;
    out[0] = '\0';
    if (!text) return;

    while (*p && used + 1 < capacity)
    {
        if (*p < 128)
        {
            unsigned char ch = *p++;
            if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + 32);
            out[used++] = (char)ch;
        }
        else if (p[0] == 0xD0 && p[1] != '\0' &&
                 used + 2 < capacity)
        {
            unsigned char second = p[1];
            if (second >= 0x90 && second <= 0x9F)
            {
                out[used++] = (char)0xD0;
                out[used++] = (char)(second + 0x20);
            }
            else if (second >= 0xA0 && second <= 0xAF)
            {
                out[used++] = (char)0xD1;
                out[used++] = (char)(second - 0x20);
            }
            else if (second == 0x81)
            {
                out[used++] = (char)0xD1;
                out[used++] = (char)0x91;
            }
            else
            {
                out[used++] = (char)p[0];
                out[used++] = (char)p[1];
            }
            p += 2;
        }
        else
        {
            out[used++] = (char)*p++;
        }
    }
    out[used] = '\0';
}

int ai_knowledge_is_create_topic(const char *text)
{
    char lowered[1024];
    make_searchable(text, lowered, sizeof(lowered));
    return strstr(lowered, "create") != NULL ||
           strstr(lowered, "мод create") != NULL ||
           strstr(lowered, "в create") != NULL ||
           strstr(lowered, "create mod") != NULL ||
           strstr(lowered, "андезит") != NULL ||
           strstr(lowered, "andesite alloy") != NULL ||
           strstr(lowered, "смешанн") != NULL ||
           strstr(lowered, "mixed stone") != NULL ||
           strstr(lowered, "механическ") != NULL ||
           strstr(lowered, "водяное колесо") != NULL;
}

const char *ai_knowledge_create_facts(void)
{
    return
        "ПРОВЕРЕННАЯ ЛОКАЛЬНАЯ СПРАВКА ПО БАЗОВОМУ МОДУ MINECRAFT CREATE "
        "(не заменяет JEI/EMI и зависит от версии): "
        "Andesite Alloy / андезитовый сплав — РЕАЛЬНЫЙ базовый материал Create. "
        "Его получают из андезита с железными или цинковыми самородками. "
        "Он нужен для валов (Shafts), шестерён (Cogwheels) и андезитовых корпусов. "
        "Водяное колесо (Water Wheel) создаёт вращение с помощью потока воды. "
        "Валы и шестерни передают вращение от источника к механизмам. "
        "Механический пресс (Mechanical Press) используется для прессования, "
        "в частности превращения металлических слитков в пластины. "
        "Хорошее начало: собери андезит, железо, сделай андезитовый сплав, "
        "валы и шестерни, поставь источник вращения и подключи первый механизм. "
        "НАЗВАНИЕ Mixed Stone: это НЕ стандартный материал базового Create. "
        "Существует ОТДЕЛЬНЫЙ аддон Create: Mixed Stone, "
        "который добавляет рецепты смешивания булыжника, камня и обсидиана; "
        "не утверждай, что существует отдельный создаваемый из блоков предмет Mixed Stone. "
        "Рецепты и доступные механизмы различаются по версии/модпаку. "
        "Если нужны точные количества или рецепты, советуй проверить JEI/EMI. "
        "Проверенные факты выше важнее ошибочных предположений в прошлых ответах.";
}

int ai_knowledge_direct_answer(const char *question, char *answer, size_t capacity)
{
    char lowered[1024];
    const char *response = NULL;
    int n;
    if (!question || !answer || capacity < 2) return 0;
    answer[0] = '\0';
    make_searchable(question, lowered, sizeof(lowered));

    if ((strstr(lowered, "андезит") && strstr(lowered, "сплав")) ||
        strstr(lowered, "andesite alloy"))
    {
        response = "Поправлюсь: андезитовый сплав (Andesite Alloy) реально есть в Create! "
                   "Его делают из андезита и железных или цинковых самородков. "
                   "Он нужен для валов и шестерён. Рецепт своей версии проверь в JEI/EMI.";
    }
    else if ((strstr(lowered, "смешан") &&
              (strstr(lowered, "камен") || strstr(lowered, "камн"))) ||
             strstr(lowered, "mixed stone"))
    {
        response = "Моя ошибка: «смешанный камень» не базовый предмет Create. "
                   "Есть отдельный аддон Create: Mixed Stone с рецептами камня, "
                   "булыжника и обсидиана, но это не такой предмет. "
                   "Проверь список модов и JEI/EMI.";
    }

    if (!response) return 0;
    n = snprintf(answer, capacity, "%s", response);
    if (n < 0 || (size_t)n >= capacity)
    {
        answer[0] = '\0';
        return 0;
    }
    return 1;
}
