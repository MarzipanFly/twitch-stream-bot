#include "ai_game_scope.h"

#include <string.h>

/* Lowercase ASCII and two-byte Russian UTF-8 letters (including Ё). */
static void lowercase(const char *text, char *out, unsigned int cap)
{
    const unsigned char *p = (const unsigned char *)text;
    unsigned int n = 0;
    if (!out || !cap) return;
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

AiGameScope ai_game_scope_from_text(const char *text)
{
    char s[1200];
    lowercase(text, s, sizeof(s));
    if (strstr(s, "phasmophobia") || strstr(s, "фазм"))
        return AI_GAME_PHASMO;
    if (strstr(s, "world of tanks") || strstr(s, "wot") ||
        strstr(s, "мир танков") || strstr(s, "танки"))
        return AI_GAME_WOT;
    if (strstr(s, "euro truck") || strstr(s, "ets2") ||
        strstr(s, "ets 2") || strstr(s, "етс") || strstr(s, "дальнобой"))
        return AI_GAME_ETS2;
    if (strstr(s, "fifa") || strstr(s, "фифа") ||
        strstr(s, "ea sports fc") || strstr(s, "ea fc") ||
        strstr(s, "fc 26") || strstr(s, "fc26") || strstr(s, "фк26"))
        return AI_GAME_FC;
    if (strstr(s, "minecraft") || strstr(s, "майнкрафт") ||
        strstr(s, "create") || strstr(s, "креат") ||
        strstr(s, "mekanism") || strstr(s, "immersive engineering") ||
        strstr(s, "ae2"))
        return AI_GAME_CREATE;
    return AI_GAME_NONE;
}

AiGameScope ai_game_scope_from_title(const char *title)
{
    AiGameScope id = ai_game_scope_from_text(title);
    char s[256];
    if (id != AI_GAME_NONE) return id;
    lowercase(title, s, sizeof(s));
    if (strstr(s, "applied energistics")) return AI_GAME_CREATE;
    return AI_GAME_NONE;
}
