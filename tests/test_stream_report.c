/* Standalone test (from repo root):
 * cc -std=c11 -Wall -Wextra -Iinclude \
 *    tests/test_stream_report.c src/stream_report.c -o test_stream_report
 * Windows: add src/stream_report.c to the MinGW test program.
 */
#include "stream_report.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#define remove_dir(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir(p,0700)
#define remove_dir(p) rmdir(p)
#endif
static int file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char buffer[4096];
    size_t size;
    if (!f) return 0;
    size = fread(buffer, 1, sizeof(buffer)-1, f);
    fclose(f);
    buffer[size] = '\0';
    return strstr(buffer, needle) != NULL;
}
int main(void)
{
    StreamReport report;
    TwitchStream stream = {0};
    const char *dir = "tests/.tmp_stream_report";
    const char *log = "tests/.tmp_stream_report/stream_987654321.tsv";
    const char *summary = "tests/.tmp_stream_report/stream_987654321_summary.txt";
    const time_t begin = (time_t)1800000000;
    remove(log);
    remove(summary);
    make_dir(dir);

    stream_report_init(&report, 1, dir);
    assert(!report.active);
    assert(stream_report_observe(&report, &stream, begin));
    assert(!report.active);
    stream.is_live = 1;
    snprintf(stream.id,sizeof(stream.id),"%s","987654321");
    snprintf(stream.started_at,sizeof(stream.started_at),"%s","2026-11-03T09:00:00Z");
    snprintf(stream.game_name,sizeof(stream.game_name),"%s","Minecraft");
    snprintf(stream.title,sizeof(stream.title),"%s","Building a factory");
    stream.viewer_count = 4;
    assert(stream_report_observe(&report, &stream, begin));
    assert(report.active);
    assert(file_contains(log, "START"));
    stream.viewer_count = 12;
    assert(stream_report_observe(&report, &stream, begin+30));
    snprintf(stream.game_name,sizeof(stream.game_name),"%s","Phasmophobia");
    snprintf(stream.title,sizeof(stream.title),"%s","Ghost hunting");
    stream.viewer_count = 7;
    assert(stream_report_observe(&report, &stream, begin+60));
    assert(file_contains(log,"GAME_CHANGE"));
    assert(file_contains(log,"TITLE_CHANGE"));

    /* Restart bot mid-stream. The same stream ID should append RESUME. */
    stream_report_init(&report, 1, dir);
    assert(stream_report_observe(&report, &stream, begin+90));
    assert(file_contains(log,"RESUME"));

    stream.is_live=0;
    assert(stream_report_observe(&report, &stream, begin+180));
    assert(!report.active);
    assert(file_contains(summary,"Peak concurrent viewers OBSERVED: 12"));
    assert(file_contains(summary,"Approximate bot-observed duration: 3 minutes"));
    assert(file_contains(summary,"Minecraft"));
    assert(file_contains(summary,"Phasmophobia"));
    assert(file_contains(summary,"Ghost hunting"));

    /* Repeated offline polls must not append more END events. */
    assert(stream_report_observe(&report, &stream, begin+210));
    assert(file_contains(log,"END"));

    stream.is_live=1;
    snprintf(stream.id,sizeof(stream.id),"%s","../../unsafe");
    assert(!stream_report_observe(&report, &stream, begin+240));
    stream_report_init(&report, 0, dir);
    assert(stream_report_observe(&report, &stream, begin+250));

    remove(log);
    remove(summary);
    remove_dir(dir);
    puts("stream report tests passed");
    return 0;
}
