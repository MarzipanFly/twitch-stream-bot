#ifndef GAME_DETECTOR_H
#define GAME_DETECTOR_H

#include <windows.h>
#include "config.h"

typedef struct
{
    HWND window;
    char title[CONFIG_STRING_SIZE];
    char process_name[CONFIG_STRING_SIZE];
    unsigned long process_id;
    unsigned long long area;
    int is_foreground;
} GameDetectorWindow;

/*
 * Finds a visible, non-minimized top-level window that occupies most of a
 * monitor and does not belong to a known desktop/browser/launcher process.
 * Returns 1 when found, 0 when no candidate exists, and -1 on enumeration error.
 */
int game_detector_find_fullscreen(GameDetectorWindow *result);

#endif
