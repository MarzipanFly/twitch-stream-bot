#ifndef STREAM_REPORT_H
#define STREAM_REPORT_H

#include "twitch_stream.h"
#include <stddef.h>
#include <time.h>

/* Automatic per-stream event log and end-of-stream summary.
 * No OBS changes, chat messages, AI or extra network requests.
 * Caller creates the output directory and calls observe only after
 * successfully fetching Twitch stream state. */
typedef struct
{
    int enabled;
    int active;
    char directory[260];
    char stream_id[TWITCH_STREAM_ID_SIZE];
    char started_at[TWITCH_STREAM_TIME_SIZE];
    char last_game[TWITCH_STREAM_GAME_SIZE];
    char last_title[TWITCH_STREAM_TITLE_SIZE];
} StreamReport;

void stream_report_init(StreamReport *report, int enabled, const char *directory);

/* Returns 1 on success (also when disabled), 0 on a filesystem error.
 * Initial live observation starts/resumes a report; an observed OFFLINE
 * transition generates a summary. Stopping the bot is NOT a stream end. */
int stream_report_observe(StreamReport *report,
                          const TwitchStream *stream,
                          time_t observed_at);

#endif
