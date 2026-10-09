#include "youtube_metadata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only video IDs are accepted, never arbitrary shell input. */
static int valid_id(const char *id)
{
    size_t i;
    if (!id || strlen(id) != 11)
        return 0;
    for (i = 0; i < 11; ++i)
    {
        unsigned char c = (unsigned char)id[i];
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

int youtube_metadata_fetch(const char *video_id, YouTubeMetadata *metadata)
{
    char command[512];
    char title_line[2048];
    char duration_line[128];
    char *end;
    unsigned long seconds;
    size_t length;
    FILE *pipe;
    int status;

    if (!metadata || !valid_id(video_id))
        return 0;
    memset(metadata, 0, sizeof(*metadata));

    /* Two simple output lines avoid full JSON and JSON template ambiguity. */
    snprintf(command, sizeof(command),
             "yt-dlp.exe --no-playlist --skip-download "
             "--no-warnings --socket-timeout 8 --retries 1 "
             "--extractor-retries 1 "
             "--print \"%%(title)s\" --print \"%%(duration)s\" "
             "\"https://www.youtube.com/watch?v=%s\" 2>NUL",
             video_id);

    pipe = _popen(command, "r");
    if (!pipe)
        return 0;
    title_line[0] = '\0';
    duration_line[0] = '\0';
    if (fgets(title_line, sizeof(title_line), pipe) == NULL ||
        fgets(duration_line, sizeof(duration_line), pipe) == NULL)
    {
        _pclose(pipe);
        return 0;
    }
    status = _pclose(pipe);
    if (status != 0)
        return 0;

    length = strcspn(title_line, "\r\n");
    if (length == 0 || length >= sizeof(metadata->title))
        return 0;
    title_line[length] = '\0';

    seconds = strtoul(duration_line, &end, 10);
    if (end == duration_line || seconds == 0 || seconds > 86400)
        return 0;
    while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')
        ++end;
    if (*end != '\0')
        return 0;

    memcpy(metadata->title, title_line, length + 1);
    metadata->duration_seconds = (unsigned int)seconds;
    return 1;
}
