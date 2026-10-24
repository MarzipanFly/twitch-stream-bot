#include "ai_tactics.h"
#include "ai_game_scope.h"

#include <stdio.h>
#include <string.h>

#define TACTICS_INPUT_CAP 1024

/* Case-insensitive matching of English and Russian UTF-8 text; no locale. */
static void lowercase(const char *input, char *out, size_t cap)
{
    const unsigned char *p = (const unsigned char *)input;
    size_t n = 0;
    if (!out || cap < 2) return;
    out[0] = '\0';
    if (!p) return;
    while (*p && n + 2 < cap)
    {
        if (*p >= 'A' && *p <= 'Z')
            out[n++] = (char)(*p++ + 32);
        else if (p[0] == 0xD0 && p[1])
        {
            unsigned char b = p[1];
            if (b >= 0x90 && b <= 0x9F)
            {
                out[n++] = (char)0xD0;
                out[n++] = (char)(b + 32);
            }
            else if (b >= 0xA0 && b <= 0xAF)
            {
                out[n++] = (char)0xD1;
                out[n++] = (char)(b - 32);
            }
            else if (b == 0x81)
            {
                out[n++] = (char)0xD1;
                out[n++] = (char)0x91;
            }
            else
            {
                out[n++] = (char)p[0];
                out[n++] = (char)b;
            }
            p += 2;
        }
        else out[n++] = (char)*p++;
    }
    out[n] = '\0';
}
static int contains(const char *text, const char *needle)
{
    return text && needle && strstr(text, needle) != NULL;
}
static int football_clue(const char *text)
{
    return contains(text, "4-4-2") || contains(text, "4-2-3-1") ||
           contains(text, "4-3-3") || contains(text, "4-2-4") ||
           contains(text, "опорник") || contains(text, "прессинг") ||
           (contains(text, "соперник") && contains(text, "фланг")) ||
           (contains(text, "соперник играет") && contains(text, "схем"));
}
static int threat_on_wings(const char *text)
{
    return contains(text, "фланг") || contains(text, "крайние атак") ||
           contains(text, "через края") || contains(text, "по краям");
}
static int asks_tactic(const char *text)
{
    return contains(text, "что делать") || contains(text, "какую схем") ||
           contains(text, "как играть") || contains(text, "что поменять") ||
           contains(text, "как отыграть") || contains(text, "как защищ") ||
           contains(text, "как останов") || contains(text, "что посовет") ||
           contains(text, "а если") || contains(text, "как перестро");
}
static int score_related(const char *text)
{
    return contains(text, "проигрыв") || contains(text, "уступа") ||
           contains(text, "веду ") || contains(text, "выигрыв") ||
           contains(text, "побежда") || contains(text, "счёт") ||
           contains(text, "счет");
}
/* Read a clock minute from 70-й, 70 минуте etc, not any arbitrary integer.
 * Avoid mistaking a scoreboard 0:2 for the match minute. */
static int minute_from_text(const char *text)
{
    const char *p = text;
    while (p && *p)
    {
        int minute = 0, digits = 0;
        const char *q, *term, *r;
        int intervening_number = 0;
        if (*p < '0' || *p > '9') { ++p; continue; }
        /* Skip minutes embedded in scorelines (0:2), identifiers or years. */
        if (p > text && (p[-1] == ':' ||
                         (p[-1] >= '0' && p[-1] <= '9')))
        {
            ++p;
            continue;
        }
        q = p;
        while (*q >= '0' && *q <= '9' && digits < 3)
        {
            minute = minute * 10 + (*q - '0');
            ++digits;
            ++q;
        }
        if (digits <= 3 && minute >= 1 && minute <= 120 && *q != ':')
        {
            term = strstr(q, "минут");
            if (term && (size_t)(term - q) <= 18)
            {
                for (r = q; r < term; ++r)
                    if (*r >= '0' && *r <= '9')
                    {
                        intervening_number = 1;
                        break;
                    }
                if (!intervening_number) return minute;
            }
            if (minute >= 45 &&
                (!strncmp(q, "-й", strlen("-й")) ||
                 !strncmp(q, "-ой", strlen("-ой"))))
                return minute;
        }
        p = q > p ? q : p + 1;
    }
    return 0;
}
static int write_answer(const char *text, char *out, size_t cap)
{
    int n;
    if (!text || !out || cap < 2) return 0;
    n = snprintf(out, cap, "%s", text);
    if (n < 0 || (size_t)n >= cap)
    {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

int ai_tactics_reply(const char *question, const char *const *previous_questions,
                     size_t previous_count, char *answer, size_t capacity)
{
    char current[TACTICS_INPUT_CAP], previous[TACTICS_INPUT_CAP];
    size_t i;
    int known_football = 0, wing_threat = 0, minute, trailing, leading;
    if (!answer || capacity < 2) return 0;
    answer[0] = '\0';
    if (!question || !question[0]) return 0;
    /* Explicitly named other games must never be interpreted as soccer. */
    if (ai_game_scope_from_text(question) != AI_GAME_NONE &&
        ai_game_scope_from_text(question) != AI_GAME_FC)
        return 0;
    lowercase(question, current, sizeof(current));
    known_football = ai_game_scope_from_text(question) == AI_GAME_FC ||
                     football_clue(current);
    wing_threat = threat_on_wings(current);

    /* The same viewer may have an ongoing match discussion without the
     * name "FIFA" in every follow-up; previous answers are NOT trusted. */
    for (i = 0; previous_questions && i < previous_count; ++i)
    {
        const char *past = previous_questions[i];
        if (!past) continue;
        lowercase(past, previous, sizeof(previous));
        if (ai_game_scope_from_text(past) == AI_GAME_FC ||
            football_clue(previous))
            known_football = 1;
        if (threat_on_wings(previous) &&
            (football_clue(previous) || ai_game_scope_from_text(past) == AI_GAME_FC))
            wing_threat = 1;
    }
    if (!known_football) return 0;

    trailing = contains(current, "проигрыв") || contains(current, "уступа");
    leading = contains(current, "веду ") || contains(current, "выигрыв") ||
              contains(current, "побежда");
    minute = minute_from_text(current);

    /* Late-game urgency outranks earlier defensive positioning.
     * Do not infer which player is behind from a score alone. */
    if (trailing && minute >= 65)
    {
        if (wing_threat)
            return write_answer(
                "Если уступаешь в концовке и соперник опасен на флангах, "
                "попробуй перейти на атакующую 4-3-3. Оставь одного опорника страховать центр, "
                "крайних атакующих игроков используй широко и увеличь темп передач. "
                "Крайних защитников подключай по очереди: иначе 4-4-2 поймает тебя на контратаке. "
                "Это риск ради голов, а не гарантия отыгрыша.", answer, capacity);
        return write_answer(
            "Если уступаешь в концовке, попробуй более атакующую 4-3-3 "
            "с широкими крайними нападающими и быстрым продвижением мяча. "
            "Один опорник должен страховать контратаки, защитников подключай выборочно. "
            "На стандартах используй сильных игроков в штрафной. "
            "Риск возрастёт, но при 0:2 осторожная игра редко помогает отыграться.",
            answer, capacity);
    }
    if (leading && minute >= 70)
        return write_answer(
            "Если ведёшь после 70-й минуты, не раскрывайся без нужды. "
            "Можно сохранить 4-2-3-1: опорники перекрывают центр, крайние полузащитники "
            "помогают защитникам, а впереди остаётся вариант контратаки. "
            "Избегай ненужных потерь возле своей штрафной; совсем отказываться от атаки тоже рискованно.",
            answer, capacity);
    if (!asks_tactic(current) && !score_related(current))
        return 0;
    if (trailing)
    {
        if (minute > 0)
            return write_answer(
                "Если уступаешь уже в начале матча, не нужно бросать всю команду вперёд. "
                "Попробуй контролируемую 4-3-3: сохрани одного опорника для страховки, "
                "расширь атаку и чаще ищи свободный фланг. "
                "У тебя ещё достаточно времени, а слишком высокий прессинг без подстраховки "
                "подарит сопернику контратаки. Смотри, где именно теряешь мяч.",
                answer, capacity);
        return write_answer(
            "Если уступаешь, можно перестроиться на 4-3-3 с широкими "
            "атакующими игроками, сохранив опорника для страховки. "
            "Но степень риска зависит от минуты, счёта и угроз соперника: "
            "уточни их, чтобы выбрать более конкретный план.", answer, capacity);
    }
    if (wing_threat)
        return write_answer(
            "Против 4-4-2 с частыми атаками через фланги попробуй 4-2-3-1. "
            "Не оголяй оба края: крайние защитники осторожнее подключаются, "
            "а крайние полузащитники помогают им против двух игроков на фланге. "
            "Два опорника страхуют центр, после отбора переводи мяч в свободную зону. "
            "Переход на трёх центральных защитников без нужных крайних игроков может лишь усилить проблему.",
            answer, capacity);
    return 0;
}
