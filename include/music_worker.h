#ifndef MUSIC_WORKER_H
#define MUSIC_WORKER_H
#include <windows.h>
#include <stddef.h>

typedef struct
{
    HANDLE thread;
    volatile LONG state; /* 0 idle, 1 downloading, 2 ready, -1 failed */
    char video_id[12];
    char path[MAX_PATH];
} MusicWorker;

int music_worker_start(MusicWorker *worker, const char *video_id);
int music_worker_status(MusicWorker *worker, char *path, size_t capacity);
void music_worker_close(MusicWorker *worker);
#endif
