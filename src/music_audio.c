#include "music_audio.h"

#include <stdio.h>
#include <string.h>
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
