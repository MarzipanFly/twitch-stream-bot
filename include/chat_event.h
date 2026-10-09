#ifndef CHAT_EVENT_H
#define CHAT_EVENT_H

#include <time.h>

/*
 * Состояние случайного события
 */
typedef struct
{
	int active;
	long long reward;
	time_t next_event_time;
	time_t expires_at;
} ChatEvent;

/*
 * Инициализирует систему случайных событий
 */
void chat_event_init(
	ChatEvent *event
);

/*
 * Обновляет состояние события
 *
 * Возвращает:
 * 1  - событие завершено
 * 0  - событие идёт
 * -1 - активное событие истекло
 */
int chat_event_update(
	ChatEvent *event
);

/*
 * Пытает сязабрать активную награду
 *
 * Возвращает:
 * 1 - награда успешно забрана
 * 0 - активной награды нет
 */
int chat_event_claim(
		ChatEvent *event,
		long long *reward
);

#endif // CHAT_EVENT_H
