#include "music_audio.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>

static int valid_video_id(const char *id)
{
    size_t i;
    if (!id || strlen(id) != 11)
        return 0;
    for (i = 0; i < 11; ++i)
    {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

int music_audio_prepare(const char *video_id, const char *directory,
                        char *output_path, size_t output_capacity)
{
    char command[1024];
    char base_path[MAX_PATH];
    char final_path[MAX_PATH];
    DWORD attributes;
    int result;

    if (!output_path || output_capacity == 0)
        return 0;
    output_path[0] = '\0';
    if (!valid_video_id(video_id) || !directory || !directory[0] ||
        strchr(directory, '"') || strchr(directory, '%') ||
        strchr(directory, '&') || strchr(directory, '|') ||
        strchr(directory, '<') || strchr(directory, '>') ||
        strchr(directory, '^') || strchr(directory, '!'))
        return 0;
    attributes = GetFileAttributesA(directory);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    if (snprintf(base_path, sizeof(base_path), "%s\\%s",
                 directory, video_id) >= (int)sizeof(base_path))
        return 0;
    if (snprintf(final_path, sizeof(final_path), "%s.mp3",
                 base_path) >= (int)sizeof(final_path))
        return 0;
    if (strlen(final_path) + 1 > output_capacity)
        return 0;
    attributes = GetFileAttributesA(final_path);
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        strcpy(output_path, final_path);
        return 1;
    }
    if (snprintf(command, sizeof(command),
                 "yt-dlp.exe --no-playlist --no-warnings "
                 "--socket-timeout 8 --retries 1 --extractor-retries 1 "
                 "-f bestaudio -x --audio-format mp3 "
                 "--audio-quality 5 -o \"%s.%%(ext)s\" "
                 "\"https://www.youtube.com/watch?v=%s\"",
                 base_path, video_id) >= (int)sizeof(command))
        return 0;
    result = system(command);
    if (result != 0)
        return 0;
    attributes = GetFileAttributesA(final_path);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    strcpy(output_path, final_path);
    return 1;
}

/* Find previously renamed files by their unique YouTube ID suffix. */
int music_audio_find_cached(const char *video_id, char *output_utf8,
                            size_t capacity)
{
    wchar_t directory[MAX_PATH], pattern[MAX_PATH], suffix[32];
    WIN32_FIND_DATAW entry;
    HANDLE found;
    DWORD length;
    size_t name_length, suffix_length;
    int converted;

    if (!output_utf8 || !capacity || !valid_video_id(video_id))
        return 0;
    output_utf8[0] = '\0';
    length = GetFullPathNameW(L"music_cache", MAX_PATH, directory, NULL);
    if (!length || length >= MAX_PATH)
        return 0;
    if (swprintf(suffix, 32, L" [%hs].mp3", video_id) < 0 ||
        swprintf(pattern, MAX_PATH, L"%ls\\*%ls", directory, suffix) < 0)
        return 0;
    found = FindFirstFileW(pattern, &entry);
    if (found == INVALID_HANDLE_VALUE)
        return 0;
    suffix_length = wcslen(suffix);
    do
    {
        wchar_t absolute_path[MAX_PATH];
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        name_length = wcslen(entry.cFileName);
        if (name_length <= suffix_length ||
            wcscmp(entry.cFileName + name_length - suffix_length, suffix) != 0)
            continue;
        if (swprintf(absolute_path, MAX_PATH, L"%ls\\%ls",
                     directory, entry.cFileName) < 0)
            continue;
        converted = WideCharToMultiByte(CP_UTF8, 0, absolute_path, -1,
                                        output_utf8, (int)capacity, NULL, NULL);
        if (converted)
        {
            FindClose(found);
            return 1;
        }
    } while (FindNextFileW(found, &entry));
    FindClose(found);
    output_utf8[0] = '\0';
    return 0;
}

/* Windows Unicode rename: preserve Cyrillic, remove forbidden filename chars. */
int music_audio_name_file(const char *downloaded_path, const char *title_utf8,
                          const char *video_id, char *output_utf8, size_t capacity)
{
    wchar_t old_path[MAX_PATH], new_path[MAX_PATH];
    wchar_t title[180], safe[180], directory[MAX_PATH];
    wchar_t suffix[32];
    DWORD n;
    int count, i, j = 0;
    size_t directory_length;
    if (!downloaded_path || !title_utf8 || !valid_video_id(video_id) ||
        !output_utf8 || capacity == 0) return 0;
    output_utf8[0] = '\0';
    n = GetFullPathNameW(L"music_cache", MAX_PATH, directory, NULL);
    if (!n || n >= MAX_PATH) return 0;
    directory_length = wcslen(directory);
    if (directory_length + 1 >= MAX_PATH) return 0;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, title_utf8,
                                -1, title, 180);
    if (!count) return 0;
    for (i = 0; title[i] && j < 110; ++i)
    {
        wchar_t ch = title[i];
        if (ch < 32 || wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
        safe[j++] = ch;
    }
    while (j && (safe[j - 1] == L' ' || safe[j - 1] == L'.')) --j;
    if (!j) return 0;
    safe[j] = 0;
    swprintf(suffix, 32, L" [%hs].mp3", video_id);
    if (swprintf(new_path, MAX_PATH, L"%ls\\%ls%ls",
                 directory, safe, suffix) < 0) return 0;
    if (!MultiByteToWideChar(CP_ACP, 0, downloaded_path, -1,
                             old_path, MAX_PATH)) return 0;
    if (!MoveFileW(old_path, new_path))
    {
        if (GetFileAttributesW(new_path) == INVALID_FILE_ATTRIBUTES)
            return 0;
        /* Same track was already cached with its readable name. */
        DeleteFileW(old_path);
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, new_path, -1, output_utf8,
                             (int)capacity, NULL, NULL)) return 0;
    return 1;
}
