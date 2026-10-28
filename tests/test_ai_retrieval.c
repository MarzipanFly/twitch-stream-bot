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
    assert(ai_retrieval_search("Что лучше в FIFA 26 для обороны?", "",
                               text, sizeof(text)));
    assert(strstr(text, "EA FC 26") != NULL);
    assert(strstr(text, "ETS2") == NULL);
    assert(ai_retrieval_search("Расскажи про EMF в Phasmophobia", "",
                               text, sizeof(text)));
    assert(strstr(text, "Phasmophobia") != NULL);
    assert(strstr(text, "EA FC 26") == NULL);
    assert(ai_retrieval_search("Круиз-контроль в ETS 2", "",
                               text, sizeof(text)));
    assert(strstr(text, "ETS2") != NULL);
    assert(strstr(text, "Phasmophobia") == NULL);
    assert(ai_retrieval_search("Как работает экономика в VALORANT?", "",
                               text, sizeof(text)));
    assert(strstr(text, "VALORANT") != NULL);
    assert(strstr(text, "World of Tanks") == NULL);
    assert(ai_retrieval_search("Смок и дым в CS2", "",
                               text, sizeof(text)));
    assert(strstr(text, "Counter-Strike 2") != NULL);
    assert(strstr(text, "EA FC 26") == NULL);
    assert(ai_retrieval_search("Что делает нексус в League of Legends?", "",
                               text, sizeof(text)));
    assert(strstr(text, "League of Legends") != NULL);
    assert(ai_retrieval_search("Как экономить буст в Rocket League?", "",
                               text, sizeof(text)));
    assert(strstr(text, "Rocket League") != NULL);
    assert(ai_retrieval_search("Что делать с конвейером в Satisfactory?", "",
                               text, sizeof(text)));
    assert(strstr(text, "Satisfactory") != NULL);
    assert(!ai_retrieval_search("А кто сейчас выигрывает?", "",
                                text, sizeof(text)));
    assert(!ai_retrieval_search("Привет! Сегодня классно играем", "",
                                text, sizeof(text)));
    puts("ai retrieval tests passed");
    return 0;
}
