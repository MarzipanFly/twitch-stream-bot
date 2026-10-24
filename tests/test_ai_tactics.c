/* Build from project root:
 * cc -std=c11 -Wall -Wextra -Werror -Iinclude \
 *   tests/test_ai_tactics.c src/ai_tactics.c src/ai_game_scope.c -o test_ai_tactics
 */
#include "ai_tactics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void assert_has(const char *haystack, const char *needle)
{
    if (!strstr(haystack, needle))
    {
        fprintf(stderr, "Expected %s in reply: %s\n", needle, haystack);
        assert(0);
    }
}
int main(void)
{
    char answer[761];
    const char *match[] = {
        "Какая схема в FIFA 26 поможет против прессинга?",
        "Соперник играет 4-4-2, постоянно атакует через фланги. Что делать?"
    };
    const char *other_game[] = { "Играл в Phasmophobia" };
    const char *neutral[] = { "Расскажи про мод Create" };
    assert(ai_tactics_reply(match[1], NULL, 0, answer, sizeof(answer)));
    assert_has(answer, "4-2-3-1");
    assert_has(answer, "фланг");
    assert(strlen(answer) <= 760);

    assert(ai_tactics_reply("А если я проигрываю 0:2 на 70-й минуте?",
                            match, 2, answer, sizeof(answer)));
    assert_has(answer, "4-3-3");
    assert_has(answer, "контратак");
    assert(strstr(answer, "усилением вратаря") == NULL);
    assert(strlen(answer) <= 760);

    assert(ai_tactics_reply("Я проигрываю 0:2 на 70-й минуте",
                            match, 2, answer, sizeof(answer)));
    assert_has(answer, "4-3-3");

    assert(ai_tactics_reply("Веду 1:0 на 82-й минуте. Что делать?",
                            match, 2, answer, sizeof(answer)));
    assert_has(answer, "4-2-3-1");
    assert_has(answer, "контратак");

    assert(ai_tactics_reply("Проигрываю 0:2 на 25-й минуте. Что делать?",
                            match, 2, answer, sizeof(answer)));
    assert_has(answer, "4-3-3");
    assert_has(answer, "достаточно времени");
    assert(strstr(answer, "в концовке") == NULL);

    assert(!ai_tactics_reply("А как искать улики в Phasmophobia?",
                             match, 2, answer, sizeof(answer)));
    assert(!ai_tactics_reply("Сколько нужно железа в Create?",
                             neutral, 1, answer, sizeof(answer)));
    assert(!ai_tactics_reply("Кто такой Мимик?", other_game, 1,
                             answer, sizeof(answer)));
    assert(!ai_tactics_reply("Как дела?", NULL, 0,
                             answer, sizeof(answer)));
    puts("AI tactical scenario tests passed");
    return 0;
}
