#ifndef MUSIC_LIBRARY_H
#define MUSIC_LIBRARY_H

#include <stddef.h>

#define MUSIC_LIBRARY_CAPACITY 256
#define MUSIC_LIBRARY_NAME_SIZE 128
#define MUSIC_LIBRARY_PATH_SIZE 512

typedef struct
{
    char id[MUSIC_LIBRARY_NAME_SIZE];
    char path[MUSIC_LIBRARY_PATH_SIZE];
} MusicLibraryEntry;

typedef struct
{
    MusicLibraryEntry entries[MUSIC_LIBRARY_CAPACITY];
    size_t count;
} MusicLibrary;

/* Scans one local directory for .mp3 files (no recursion). */
int music_library_load(MusicLibrary *library, const char *directory);
const MusicLibraryEntry *music_library_find(const MusicLibrary *library,
                                             const char *track_id);
#endif
