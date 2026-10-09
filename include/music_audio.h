#ifndef MUSIC_AUDIO_H
#define MUSIC_AUDIO_H

#include <stddef.h>

/* Downloads audio only and converts it to an MP3 file using yt-dlp + FFmpeg.
 * The destination directory must exist. The video ID is validated.
 * This function blocks until conversion finishes; call outside chat loop.
 */
int music_audio_prepare(const char *video_id, const char *directory,
                        char *output_path, size_t output_capacity);

#endif
