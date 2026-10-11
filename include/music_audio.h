#ifndef MUSIC_AUDIO_H
#define MUSIC_AUDIO_H

#include <stddef.h>

/* Downloads audio only and converts it to an MP3 file using yt-dlp + FFmpeg.
 * The destination directory must exist. The video ID is validated.
 * This function blocks until conversion finishes; call outside chat loop.
 */
int music_audio_prepare(const char *video_id, const char *directory,
                        char *output_path, size_t output_capacity);

/* Rename to a readable Unicode title with a unique video-ID suffix.
 * Returns an absolute UTF-8 path suitable for OBS WebSocket JSON. */
int music_audio_name_file(const char *downloaded_path, const char *title_utf8,
                          const char *video_id, char *output_utf8, size_t capacity);

#endif
