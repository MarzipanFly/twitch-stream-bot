/* Offline regression test for game scope detection. 
 * gcc -std=c11 -Wall -Wextra -Werror -Iinclude
 *     tests/test_ai_game_scope.c src/ai_game_scope.c -o test_ai_game_scope
 */
#include "ai_game_scope.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(ai_game_scope_from_text("Играю в FC 27") == AI_GAME_FC);
    assert(ai_game_scope_from_text("Как защититься в FIFA?") == AI_GAME_FC);
    assert(ai_game_scope_from_text("Кто призрак в Phasmophobia?") == AI_GAME_PHASMO);
    assert(ai_game_scope_from_text("Рейс в ETS2") == AI_GAME_ETS2);
    assert(ai_game_scope_from_text("Подскажи по WoT") == AI_GAME_WOT);
    assert(ai_game_scope_from_text("War Thunder: реалистичные бои") ==
           AI_GAME_WAR_THUNDER);
    assert(ai_game_scope_from_text("Что купить в VALORANT?") == AI_GAME_VALORANT);
    assert(ai_game_scope_from_text("Где смок в кс2?") == AI_GAME_CS2);
    assert(ai_game_scope_from_text("League of Legends: кто в лесу?") ==
           AI_GAME_LOL);
    assert(ai_game_scope_from_text("Rocket League: буст") ==
           AI_GAME_ROCKET_LEAGUE);
    assert(ai_game_scope_from_text("Factorio: перегрузка ленты") ==
           AI_GAME_FACTORIO);
    assert(ai_game_scope_from_text("Satisfactory: электроснабжение") ==
           AI_GAME_SATISFACTORY);
    assert(ai_game_scope_from_text("Stardew Valley: ферма") == AI_GAME_STARDEW);
    assert(ai_game_scope_from_text("Dead by Daylight: маньяк") == AI_GAME_DBD);
    assert(ai_game_scope_from_text("Minecraft Create механизмы") ==
           AI_GAME_CREATE);
    assert(ai_game_scope_from_title("War Thunder / боевой рейтинг") ==
           AI_GAME_WAR_THUNDER);
    assert(ai_game_scope_from_title("EA FC 26 / оборона") == AI_GAME_FC);
    /* Chat laughter must not accidentally become League of Legends. */
    assert(ai_game_scope_from_text("лол, забавно!") == AI_GAME_NONE);
    assert(ai_game_scope_from_text("А кто сейчас выигрывает?") == AI_GAME_NONE);
    assert(ai_game_scope_from_text("Привет всем!") == AI_GAME_NONE);
    puts("AI game scope tests passed");
    return 0;
}
