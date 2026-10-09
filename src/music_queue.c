#include "music_queue.h"

#include <string.h>

static int copy_field(char *destination, size_t capacity, const char *source)
{
    size_t length;
    if (destination == NULL || source == NULL || source[0] == '\0')
        return 0;
    length = strlen(source);
    if (length >= capacity)
        return 0;
    memcpy(destination, source, length + 1);
    return 1;
}

void music_queue_init(MusicQueue *queue)
{
    if (queue != NULL)
        memset(queue, 0, sizeof(*queue));
}

int music_queue_push(MusicQueue *queue, const char *track_id, const char *requester)
{
    MusicTrack track;
    if (queue == NULL || queue->count >= MUSIC_QUEUE_CAPACITY)
        return 0;
    memset(&track, 0, sizeof(track));
    if (!copy_field(track.track_id, sizeof(track.track_id), track_id) ||
        !copy_field(track.requester, sizeof(track.requester), requester))
        return 0;
    queue->tracks[queue->count++] = track;
    return 1;
}

int music_queue_pop(MusicQueue *queue, MusicTrack *out_track)
{
    if (queue == NULL || out_track == NULL || queue->count == 0)
        return 0;
    *out_track = queue->tracks[0];
    --queue->count;
    if (queue->count != 0)
        memmove(&queue->tracks[0], &queue->tracks[1],
                queue->count * sizeof(queue->tracks[0]));
    memset(&queue->tracks[queue->count], 0, sizeof(queue->tracks[0]));
    return 1;
}

int music_queue_peek(const MusicQueue *queue, MusicTrack *out_track)
{
    if (queue == NULL || out_track == NULL || queue->count == 0)
        return 0;
    *out_track = queue->tracks[0];
    return 1;
}

size_t music_queue_size(const MusicQueue *queue)
{
    return queue != NULL ? queue->count : 0;
}

void music_queue_clear(MusicQueue *queue)
{
    music_queue_init(queue);
}
