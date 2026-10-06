#ifndef COMMAND_COOLDOWN_H
#define COMMAND_COOLDOWN_H

#include "commands.h"

#define COMMAND_COOLDOWN_MAX_USERS 256

/*
 * Инициализирует систему куладунов
 */
void command_cooldown_init(void);


/*
 * Проверяет, можно ли пользователю выполнить команду
 *
 * user_id		- Twitch User ID пользователя.
 * command_type	- тип команды.
 *
 * Возврщает:
 * 1 - команда разрешена, кулдаун срвзу запускается;
 * 0 - команда находится на кулдауне.
 */
int command_cooldown_try_use(
		const char *user_id,
		ChatCommandType command_type
);

#endif // COMMAND_COOLDOWN_H
