#include "chat_event.h"

#include <stdlib.h>
#include <time.h>

#define CHAT_EVENT_MIN_DELAY_SECONDS (10*60)
#define CHAT_EVENT_MAX_DELAY_SECONDS (20*60)

#define CHAT_EVENT_LIFITIME_SECONDS 60

#define CHAT_EVENT_MIN_REWARD 25
#define CHAT_EVENT_MAX_REWARD 100

/*
 * Возвращает случайное целое число
 * в диапозоне [min, max]
 */
static int random_rnage(
	int min,
	int max
)
{
	return min+rand()%(max-min);
}

/*
 * Планирует время следующего события
 */
static void schedule_next_event(
		ChatEvent *event,
		time_t now
)
{
	int delay;

	delay = random_rnage(CHAT_EVENT_MIN_DELAY_SECONDS, CHAT_EVENT_MAX_DELAY_SECONDS);

	event->next_event_time=now+delay;
}

void chat_event_init(
	ChatEvent *event
)
{
	time_t now;

	if (event == NULL)
		return;

	now = time(NULL);

	event->active=0;
	event->expires_at=0;
	event->reward=0;

	schedule_next_event(event, now);
}

int chat_event_update(
		ChatEvent *event
)
{
	time_t now;

	if (event == NULL)
		return 0;

	now = time(NULL);

	/*
	 * Уже появившуюся награду никто
	 * не забрал за овтедённое время
	 */
	if (event->active)
	{
		if (now >=event->expires_at)
		{
			event->active=0;
			event->reward=0;
			event->expires_at=0;

			schedule_next_event(
					event,
					now
			);

			return -1;
		}

		return 0;
	}

	/*
	 * пришло время создать новый дроп
	 */
	if (now>= event->next_event_time)
	{
		event->active = 1;
		event->reward=
				random_rnage(CHAT_EVENT_MIN_REWARD,
							 CHAT_EVENT_MAX_REWARD
				);
		event->expires_at=now+CHAT_EVENT_LIFITIME_SECONDS;

		return 1;
	}

	return 0;
}

int chat_event_claim(
		ChatEvent *event,
		long long *reward
)
{
	time_t now;

	if (event == NULL ||
		reward == NULL)
	{
		return 0;
	}

	now = time(NULL);

	/*
	 * Дополнительная проверка времени нужна,
	 * чтобы нельзя было забрать уже истекший дроп
	 * между двумя вызовами chat_event_update
	 */
	if (!event->active ||
		now >=event->expires_at)
	{
		return 0;
	}

	*reward =event->reward;

	event->active=0;
	event->reward=0;
	event->expires_at=0;

	schedule_next_event(
		event,
		now
	);

	return 1;
}
