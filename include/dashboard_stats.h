#ifndef DASHBOARD_STATS_H
#define DASHBOARD_STATS_H
#include "twitch_stream.h"
#include <stddef.h>
#include <time.h>
/* 120 samples, at ~30s polls = one-hour trend. */
#define DASH_CHART_SAMPLES 120
typedef struct
{
    int viewers[DASH_CHART_SAMPLES];
    unsigned int count;
    char stream_id[TWITCH_STREAM_ID_SIZE];
} DashboardChart;
void dashboard_chart_observe(DashboardChart *chart, const TwitchStream *stream);
int dashboard_mark_moment(const TwitchStream *stream, const char *note,
                          time_t now, char *message, size_t size);
/* Compact recent reports; never includes paths or secrets. */
size_t dashboard_report_history(char *output, size_t capacity);
#endif
