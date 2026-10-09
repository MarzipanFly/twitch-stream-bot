#ifndef YOUTUBE_METADATA_H
#define YOUTUBE_METADATA_H

#include <stddef.h>

#define YOUTUBE_TITLE_SIZE 256

typedef struct
{
    char title[YOUTUBE_TITLE_SIZE];
    unsigned int duration_seconds;
} YouTubeMetadata;

/* video_id must contain exactly 11 valid YouTube ID characters.
 * yt-dlp.exe must be available in PATH. No media is downloaded.
 * Returns 1 on success; 0 on failure.
 */
int youtube_metadata_fetch(const char *video_id, YouTubeMetadata *metadata);

#endif
