#include "music_library.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

static int is_mp3(const char *name)
{
    size_t length = strlen(name);
    const char *extension;
    if (length < 5)
        return 0;
    extension = name + length - 4;
    return extension[0] == '.' &&
           (extension[1] == 'm' || extension[1] == 'M') &&
           (extension[2] == 'p' || extension[2] == 'P') &&
           (extension[3] == '3');
}

static int safe_name(const char *name)
{
    const unsigned char *p = (const unsigned char *)name;
    if (!name || !name[0] || name[0] == '.')
        return 0;
    while (*p)
    {
        if (*p == '/' || *p == '\\' || *p == ':' ||
            *p == '*' || *p == '?' || *p == '"' ||
            *p == '<' || *p == '>' || *p == '|')
            return 0;
        ++p;
    }
    return 1;
}

int music_library_load(MusicLibrary *library, const char *directory)
{
    WIN32_FIND_DATAA data;
    HANDLE handle;
    char pattern[MUSIC_LIBRARY_PATH_SIZE];
    size_t length;
    int written;

    if (!library || !directory || !directory[0])
        return 0;
    memset(library, 0, sizeof(*library));
    length = strlen(directory);
    written = snprintf(pattern, sizeof(pattern), "%s%s*",
                       directory, directory[length - 1] == '/' ||
                       directory[length - 1] == '\\' ? "" : "\\");
    if (written < 0 || (size_t)written >= sizeof(pattern))
        return 0;
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        MusicLibraryEntry *entry;
        char filename[MUSIC_LIBRARY_NAME_SIZE];
        size_t name_length;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !safe_name(data.cFileName) || !is_mp3(data.cFileName))
            continue;
        name_length = strlen(data.cFileName) - 4;
        if (!name_length || name_length >= sizeof(filename))
            continue;
        memcpy(filename, data.cFileName, name_length);
        filename[name_length] = '\0';
        if (music_library_find(library, filename))
            continue;
        if (library->count >= MUSIC_LIBRARY_CAPACITY)
            break;
        entry = &library->entries[library->count];
        written = snprintf(entry->path, sizeof(entry->path), "%s%s%s",
                           directory, directory[length - 1] == '/' ||
                           directory[length - 1] == '\\' ? "" : "\\",
                           data.cFileName);
        if (written < 0 || (size_t)written >= sizeof(entry->path))
            continue;
        memcpy(entry->id, filename, name_length + 1);
        ++library->count;
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return 1;
}

const MusicLibraryEntry *music_library_find(const MusicLibrary *library,
                                             const char *track_id)
{
    size_t i;
    if (!library || !safe_name(track_id))
        return NULL;
    for (i = 0; i < library->count; ++i)
        if (_stricmp(library->entries[i].id, track_id) == 0)
            return &library->entries[i];
    return NULL;
}
