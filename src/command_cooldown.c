#include "command_cooldown.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

#define COMMAND_COOLDOWN_USER_ID_SIZE 64

typedef struct
{
	char user_id[COMMAND_COOLDOWN_USER_ID_SIZE];
	DWORD command_times[CHAT_COMMAND_UNKNOWN];
} CommandCooldownUser;

/*
 * Runtime-кэш кулдаунов
 *
 * Он существует только во время работы программы
 * После перезапуска бота кулдауны обновляются
 */
static CommandCooldownUser g_users[COMMAND_COOLDOWN_MAX_USERS];

static size_t g_user_count = 0;

/*
 * Вовзращают длительность кулдауна
 * для конкретной команды в миллисекундах
 */
static DWORD get_cooldown_ms(
		ChatCommandType command_type
)
{
	switch(command_type)
	{
		case CHAT_COMMAND_TELEGRAM:
		case CHAT_COMMAND_DISCORD:
		case CHAT_COMMAND_HELP:
        case CHAT_COMMAND_BALANCE:
		case CHAT_COMMAND_DAILY:
        case CHAT_COMMAND_TOP:
        case CHAT_COMMAND_PROFILE:
			return 10UL * 1000UL;

		case CHAT_COMMAND_COIN:
		case CHAT_COMMAND_DICE:
		case CHAT_COMMAND_EIGHT_BALL:
		case CHAT_COMMAND_SLOT:
		case CHAT_COMMAND_DUEL:
		case CHAT_COMMAND_ACCEPT:
		case CHAT_COMMAND_DECLINE:
            return 5UL * 1000UL;

		case CHAT_COMMAND_CLAIM:
			return 0;

		default:
			return 0;
	}
}
/*
 * Ищет пользователя в runtime-кэше
 */
static CommandCooldownUser *find_user(
		const char *user_id
)
{
	size_t i;

	for (i=0; i<g_user_count; ++i)
	{
		if (strcmp(g_users[i].user_id, user_id) == 0)
		{
			return &g_users[i];
		}
	}

	return NULL;
}

/*
 * Создаёт runtime-запись пользователя.
 */
static CommandCooldownUser *create_user(
		const char *user_id
)
{
	CommandCooldownUser *user;

	if(g_user_count>=COMMAND_COOLDOWN_MAX_USERS)
	{
		return NULL;
	}

	user=&g_users[g_user_count];

	memset(user, 0, sizeof(*user));

	snprintf(user->user_id, sizeof(user->user_id), "%s", user_id);

	++g_user_count;

	return user;
}

void command_cooldown_init(void)
{
	memset(g_users, 0, sizeof(g_users));

	g_user_count = 0;
}

int command_cooldown_try_use(
		const char *user_id,
		ChatCommandType command_type
)
{
	CommandCooldownUser *user;

	DWORD now;
	DWORD previous;
	DWORD cooldown;

	if(user_id == NULL || user_id[0] == '\0')
	{
		/*
		 * Если twitch ID почему-то отсутсвует,
		 * не блокируем работу команды
		 */
		return 1;
	}

	if (
		command_type <= CHAT_COMMAND_NONE ||
		command_type >= CHAT_COMMAND_UNKNOWN)
	{
		return 1;
	}

	cooldown = get_cooldown_ms(command_type);

	if (cooldown == 0)
	{
		return 1;
	}
	user = find_user(user_id);

	if (user == NULL)
	{
		user = create_user(user_id);

		if (user == NULL)
		{
			return 1;
		}
	}

	now = GetTickCount();

	previous = user->command_times[command_type];

	if (previous != 0 && (DWORD)(now - previous) < cooldown)
	{
		return 0;
	}

	user->command_times[command_type] = now;

	return 1;
}



















