#include "youtube_metadata.h"
#include "cJSON.h"

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
    char output[4096];
    size_t used = 0;
    FILE *pipe;
    cJSON *root;
    cJSON *title;
    cJSON *duration;
    int status;
    int ch;
    int truncated = 0;

    if (!metadata || !valid_id(video_id))
        return 0;
    memset(metadata, 0, sizeof(*metadata));

    /* yt-dlp emits only title and duration as compact JSON. */
    snprintf(command, sizeof(command),
             "yt-dlp.exe --no-playlist --skip-download "
             "--no-warnings --socket-timeout 8 --retries 1 "
             "--extractor-retries 1 --print \"%%(title,duration)j\" "
             "\"https://www.youtube.com/watch?v=%s\" 2>NUL",
             video_id);
    pipe = _popen(command, "r");
    if (!pipe)
        return 0;
    while ((ch = fgetc(pipe)) != EOF)
    {
        if (used + 1 >= sizeof(output))
        {
            truncated = 1;
            break;
        }
        output[used++] = (char)ch;
    }
    output[used] = '\0';
    status = _pclose(pipe);
    if (truncated || status != 0 || used == 0)
        return 0;

    root = cJSON_Parse(output);
    if (!root)
        return 0;
    title = cJSON_GetObjectItemCaseSensitive(root, "title");
    duration = cJSON_GetObjectItemCaseSensitive(root, "duration");
    if (!cJSON_IsString(title) || !title->valuestring ||
        !title->valuestring[0] || !cJSON_IsNumber(duration) ||
        duration->valuedouble <= 0 || duration->valuedouble > 86400)
    {
        cJSON_Delete(root);
        return 0;
    }
    snprintf(metadata->title, sizeof(metadata->title), "%s",
             title->valuestring);
    metadata->duration_seconds = (unsigned int)duration->valuedouble;
    cJSON_Delete(root);
    return 1;
}
