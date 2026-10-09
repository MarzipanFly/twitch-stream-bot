#include "music_worker.h"
#include "music_audio.h"
#include <string.h>
#include <stdio.h>

static DWORD WINAPI music_worker_thread(LPVOID parameter)
{
    MusicWorker *worker = (MusicWorker *)parameter;
    DWORD attributes;
    if (!CreateDirectoryA("music_cache", NULL))
    {
        attributes = GetFileAttributesA("music_cache");
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            InterlockedExchange(&worker->state, -1);
            return 0;
        }
    }
    if (music_audio_prepare(worker->video_id, "music_cache",
                            worker->path, sizeof(worker->path)))
        InterlockedExchange(&worker->state, 2);
    else
        InterlockedExchange(&worker->state, -1);
    return 0;
}

int music_worker_start(MusicWorker *worker, const char *video_id)
{
    size_t i;
    if (!worker || !video_id || strlen(video_id) != 11)
        return 0;
    for (i = 0; i < 11; ++i)
    {
        char c = video_id[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    memset(worker, 0, sizeof(*worker));
    memcpy(worker->video_id, video_id, 12);
    InterlockedExchange(&worker->state, 1);
    worker->thread = CreateThread(NULL, 0, music_worker_thread, worker, 0, NULL);
    if (!worker->thread)
    {
        InterlockedExchange(&worker->state, -1);
        return 0;
    }
    return 1;
}

int music_worker_status(MusicWorker *worker, char *path, size_t capacity)
{
    LONG state;
    if (!worker)
        return -1;
    state = InterlockedCompareExchange(&worker->state, 0, 0);
    if (state == 2 && path && capacity)
    {
        size_t length = strlen(worker->path);
        if (length + 1 > capacity)
            return -1;
        memcpy(path, worker->path, length + 1);
    }
    return (int)state;
}

void music_worker_close(MusicWorker *worker)
{
    if (!worker)
        return;
    if (worker->thread)
    {
        WaitForSingleObject(worker->thread, INFINITE);
        CloseHandle(worker->thread);
        worker->thread = NULL;
    }
}
