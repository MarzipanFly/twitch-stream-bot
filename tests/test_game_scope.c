#include "ai_game_scope.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(ai_game_scope_from_text("Расскажи про ФИФА и тактики") == AI_GAME_FC);
    assert(ai_game_scope_from_text("Лучший тягач в ETS 2?") == AI_GAME_ETS2);
    assert(ai_game_scope_from_text("Как играть в ФАЗМОФОБИЮ") == AI_GAME_PHASMO);
    assert(ai_game_scope_from_text("Create андезитовый сплав") == AI_GAME_CREATE);
    assert(ai_game_scope_from_text("Как пробивать танки?") == AI_GAME_WOT);
    assert(ai_game_scope_from_text("Что лучше утром на завтрак?") == AI_GAME_NONE);
    assert(ai_game_scope_from_title("Applied Energistics 2") == AI_GAME_CREATE);
    assert(ai_game_scope_from_title("EA FC 26 / прессинг") == AI_GAME_FC);
    puts("game scope tests passed");
    return 0;
}
