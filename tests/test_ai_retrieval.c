#include "ai_retrieval.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char text[2800];
    assert(ai_retrieval_search("Что такое андезитовый сплав в Create?", "",
                               text, sizeof(text)));
    assert(strstr(text, "Andesite Alloy") || strstr(text,"андезит"));
    assert(ai_retrieval_search("Как работает водяное колесо?", "",
                               text, sizeof(text)));
    assert(strstr(text,"вращение"));
    assert(!ai_retrieval_search("Посоветуй хороший кофе", "",
                                text, sizeof(text)));
    puts("ai retrieval tests passed");
    return 0;
}
