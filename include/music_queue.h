#ifndef MUSIC_QUEUE_H
#define MUSIC_QUEUE_H

#include <stddef.h>

#define MUSIC_QUEUE_CAPACITY 32
#define MUSIC_TRACK_ID_SIZE 128
#define MUSIC_REQUESTER_SIZE 64

typedef struct
{
    char track_id[MUSIC_TRACK_ID_SIZE];
    char requester[MUSIC_REQUESTER_SIZE];
} MusicTrack;

typedef struct
{
    MusicTrack tracks[MUSIC_QUEUE_CAPACITY];
    size_t count;
} MusicQueue;

void music_queue_init(MusicQueue *queue);
int music_queue_push(MusicQueue *queue, const char *track_id, const char *requester);
int music_queue_pop(MusicQueue *queue, MusicTrack *out_track);
int music_queue_peek(const MusicQueue *queue, MusicTrack *out_track);
size_t music_queue_size(const MusicQueue *queue);
void music_queue_clear(MusicQueue *queue);

#endif
