#include "stream_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_PATH_CAP 460
#define REPORT_ROW_CAP 2048
#define REPORT_CATEGORIES 16

/* Restrict filenames to Twitch numeric stream IDs, not arbitrary API text. */
static int valid_stream_id(const char *id)
{
    size_t i, len;
    if (!id) return 0;
    len = strlen(id);
    if (!len || len >= TWITCH_STREAM_ID_SIZE) return 0;
    for (i = 0; i < len; ++i)
        if (id[i] < '0' || id[i] > '9') return 0;
    return 1;
}
static int make_path(const StreamReport *r, char *out,
                     size_t cap, const char *suffix)
{
    int n = snprintf(out, cap, "%s/stream_%s%s",
                     r->directory, r->stream_id, suffix);
    return n > 0 && (size_t)n < cap;
}
/* Preserve UTF-8 bytes but neutralize tabs/newlines and control characters. */
static void clean_field(const char *in, char *out, size_t cap)
{
    size_t i = 0;
    if (!out || !cap) return;
    if (!in) in = "";
    while (*in && i + 1 < cap)
    {
        unsigned char c = (unsigned char)*in++;
        out[i++] = (c < 32 || c == 127) ? ' ' : (char)c;
    }
    out[i] = '\0';
}
static void utc_timestamp(time_t at, char *out, size_t cap)
{
    struct tm *utc = gmtime(&at);
    if (!utc || !strftime(out, cap, "%Y-%m-%dT%H:%M:%SZ", utc))
        snprintf(out, cap, "%s", "unknown");
}
static int append_event(StreamReport *r, const char *event,
                        int viewer_count, const char *game,
                        const char *title, time_t at)
{
    char path[REPORT_PATH_CAP], stamp[32];
    char safe_game[TWITCH_STREAM_GAME_SIZE], safe_title[TWITCH_STREAM_TITLE_SIZE];
    FILE *file;
    long size;
    int ok = 1;
    if (!make_path(r, path, sizeof(path), ".tsv")) return 0;
    file = fopen(path, "ab+");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) ok = 0;
    size = ftell(file);
    if (size < 0) ok = 0;
    if (ok && size == 0)
        if (fprintf(file, "# ApelsinBot: Twitch stream observations (~30s polls)\n"
                          "# Twitch started (UTC): %s\n"
                          "observed_utc\tepoch_seconds\tevent\tviewers\tcategory\ttitle\n",
                    r->started_at[0] ? r->started_at : "unknown") < 0)
            ok = 0;
    utc_timestamp(at, stamp, sizeof(stamp));
    clean_field(game, safe_game, sizeof(safe_game));
    clean_field(title, safe_title, sizeof(safe_title));
    if (ok && fprintf(file, "%s\t%lld\t%s\t%d\t%s\t%s\n",
                      stamp, (long long)at, event,
                      viewer_count < 0 ? 0 : viewer_count,
                      safe_game, safe_title) < 0)
        ok = 0;
    if (fclose(file) != 0) ok = 0;
    return ok;
}
static int path_exists(const StreamReport *r)
{
    char path[REPORT_PATH_CAP];
    FILE *file;
    if (!make_path(r, path, sizeof(path), ".tsv")) return 0;
    file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}
static int seen_category(char games[REPORT_CATEGORIES][TWITCH_STREAM_GAME_SIZE],
                         int count, const char *value)
{
    int i;
    for (i = 0; i < count; ++i)
        if (strcmp(games[i], value) == 0) return 1;
    return 0;
}

/* Read append-only log so peak and categories survive bot restarts. */
static int write_summary(StreamReport *r, time_t offline_at)
{
    char logpath[REPORT_PATH_CAP], sumfile[REPORT_PATH_CAP];
    char buf[REPORT_ROW_CAP], first_observed[32] = "";
    char end_utc[32], first_title[TWITCH_STREAM_TITLE_SIZE] = "";
    char last_title[TWITCH_STREAM_TITLE_SIZE] = "";
    char categories[REPORT_CATEGORIES][TWITCH_STREAM_GAME_SIZE];
    FILE *in = NULL, *out = NULL;
    int peak = 0, polls = 0, count = 0, ok = 1;
    long long first_epoch = 0;
    if (!make_path(r, logpath, sizeof(logpath), ".tsv") ||
        !make_path(r, sumfile, sizeof(sumfile), "_summary.txt"))
        return 0;
    in = fopen(logpath, "rb");
    if (!in) return 0;
    memset(categories, 0, sizeof(categories));
    while (fgets(buf, sizeof(buf), in))
    {
        char *parts[6], *cursor = buf;
        int i, viewers;
        long long epoch;
        if (buf[0] == '#' || !strncmp(buf, "observed_utc\t", 13))
            continue;
        for (i = 0; i < 5; ++i)
        {
            char *tab;
            parts[i] = cursor;
            tab = strchr(cursor, '\t');
            if (!tab) break;
            *tab = '\0';
            cursor = tab + 1;
        }
        if (i != 5) continue;
        parts[5] = cursor;
        parts[5][strcspn(parts[5], "\r\n")] = '\0';
        if (strcmp(parts[2], "SAMPLE") != 0) continue;
        epoch = strtoll(parts[1], NULL, 10);
        viewers = atoi(parts[3]);
        if (viewers > peak) peak = viewers;
        if (!first_epoch && epoch > 0)
        {
            first_epoch = epoch;
            snprintf(first_observed, sizeof(first_observed), "%s", parts[0]);
            snprintf(first_title, sizeof(first_title), "%s", parts[5]);
        }
        snprintf(last_title, sizeof(last_title), "%s", parts[5]);
        if (parts[4][0] && count < REPORT_CATEGORIES &&
            !seen_category(categories, count, parts[4]))
        {
            snprintf(categories[count], sizeof(categories[count]), "%s", parts[4]);
            ++count;
        }
        ++polls;
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (!ok) return 0;
    out = fopen(sumfile, "wb");
    if (!out) return 0;
    utc_timestamp(offline_at, end_utc, sizeof(end_utc));
    if (fprintf(out,
                "ApelsinBot: automatic Twitch stream report\n"
                "Stream ID: %s\n"
                "Twitch stream started (UTC): %s\n"
                "First observed (UTC): %s\n"
                "Offline detected (UTC): %s\n"
                "Approximate bot-observed duration: %lld minutes\n"
                "Peak concurrent viewers OBSERVED: %d\n"
                "Twitch samples: %d\n"
                "First observed title: %s\n"
                "Last observed title: %s\n"
                "Categories observed:\n",
                r->stream_id,
                r->started_at[0] ? r->started_at : "unknown",
                first_observed[0] ? first_observed : "unknown",
                end_utc,
                first_epoch > 0 && (long long)offline_at >= first_epoch ?
                    ((long long)offline_at - first_epoch) / 60 : 0,
                peak, polls,
                first_title, last_title) < 0)
        ok = 0;
    {
        int i;
        if (count == 0 && fprintf(out, "  (none recorded)\n") < 0) ok = 0;
        for (i = 0; i < count; ++i)
            if (fprintf(out, "  - %s\n", categories[i]) < 0) ok = 0;
    }
    if (fprintf(out,
                "\nNotes: Twitch is sampled while ApelsinBot runs. "
                "Peak viewers is NOT the official Twitch peak.\n"
                "Bot restart gaps and up to one polling interval are not measured.\n"
                "Full observation log: %s\n", logpath) < 0)
        ok = 0;
    if (fclose(out) != 0) ok = 0;
    return ok;
}
void stream_report_init(StreamReport *report, int enabled, const char *directory)
{
    if (!report) return;
    memset(report, 0, sizeof(*report));
    if (!enabled || !directory || !directory[0]) return;
    if (snprintf(report->directory, sizeof(report->directory), "%s",
                 directory) >= (int)sizeof(report->directory))
        return;
    report->enabled = 1;
}
static int end_active(StreamReport *r, time_t at)
{
    int ok = 1;
    if (!r->active) return 1;
    if (!append_event(r, "END", 0, r->last_game, r->last_title, at))
        ok = 0;
    if (!write_summary(r, at))
        ok = 0;
    r->active = 0;
    return ok;
}
int stream_report_observe(StreamReport *r, const TwitchStream *stream, time_t at)
{
    int ok = 1;
    if (!r || !stream) return 0;
    if (!r->enabled) return 1;
    if (!stream->is_live) return end_active(r, at);
    if (!valid_stream_id(stream->id)) return 0;
    if (!r->active || strcmp(stream->id, r->stream_id) != 0)
    {
        int already_exists;
        if (r->active && !end_active(r, at)) ok = 0;
        snprintf(r->stream_id, sizeof(r->stream_id), "%s", stream->id);
        snprintf(r->started_at, sizeof(r->started_at), "%s", stream->started_at);
        r->last_game[0] = '\0';
        r->last_title[0] = '\0';
        already_exists = path_exists(r);
        r->active = 1;
        if (!append_event(r, already_exists ? "RESUME" : "START",
                          stream->viewer_count, stream->game_name,
                          stream->title, at))
            ok = 0;
    }
    if (r->last_game[0] &&
        strcmp(r->last_game, stream->game_name) != 0 &&
        !append_event(r, "GAME_CHANGE", stream->viewer_count,
                      stream->game_name, stream->title, at))
        ok = 0;
    if (r->last_title[0] &&
        strcmp(r->last_title, stream->title) != 0 &&
        !append_event(r, "TITLE_CHANGE", stream->viewer_count,
                      stream->game_name, stream->title, at))
        ok = 0;
    if (!append_event(r, "SAMPLE", stream->viewer_count,
                      stream->game_name, stream->title, at))
        ok = 0;
    snprintf(r->last_game, sizeof(r->last_game), "%s", stream->game_name);
    snprintf(r->last_title, sizeof(r->last_title), "%s", stream->title);
    return ok;
}
