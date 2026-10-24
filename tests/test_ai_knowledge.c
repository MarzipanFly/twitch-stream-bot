/*
 * Offline regression test for factual Create knowledge.
 * Linux/macOS: cc -std=c11 -Wall -Wextra -Iinclude \
 *     tests/test_ai_knowledge.c src/ai_knowledge.c -o ai_knowledge_test
 * Windows MinGW: gcc -std=c11 -Wall -Wextra -Iinclude \
 *     tests/test_ai_knowledge.c src/ai_knowledge.c -o ai_knowledge_test.exe
 */
#include "ai_knowledge.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char answer[761];

    assert(ai_knowledge_is_create_topic("Что такое Minecraft Create?"));
    assert(ai_knowledge_is_create_topic("Расскажи про АНДЕЗИТОВЫЙ сплав"));
    assert(ai_knowledge_is_create_topic("Что за Mixed Stone?"));
    assert(!ai_knowledge_is_create_topic("Как испечь пирог?"));

    assert(ai_knowledge_direct_answer("Что такое андезитовый сплав?", answer,
                                      sizeof(answer)));
    assert(strstr(answer, "Andesite Alloy"));
    assert(strstr(answer, "реально есть"));

    assert(ai_knowledge_direct_answer("А смешанный камень существует?", answer,
                                      sizeof(answer)));
    assert(strstr(answer, "отдельный аддон"));
    assert(strstr(answer, "не базовый предмет"));

    assert(ai_knowledge_direct_answer(
        "Какая схема в FIFA 26 поможет против высокого прессинга?",
        answer, sizeof(answer)));
    assert(strstr(answer, "4-2-3-1"));
    assert(strstr(answer, "два опорника"));
    assert(strstr(answer, "фланг"));
    assert(strlen(answer) <= 760);
    assert(!ai_knowledge_direct_answer(
        "Какой лучший грузовик в ETS2?", answer, sizeof(answer)));
    assert(!ai_knowledge_direct_answer("А с чего начать?", answer,
                                       sizeof(answer)));
    assert(ai_knowledge_create_facts() != NULL);
    assert(strstr(ai_knowledge_create_facts(), "Andesite Alloy"));

    puts("AI knowledge tests passed");
    return 0;
}
