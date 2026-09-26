#ifndef PLATFORM_H
#define PLATFORM_H

#include "bot_result.h"

/*
 * Создаёт директорию, если она ещё не существуетю
 *
 * BOT_OK:
 *	- директория создана
 *	- директоря уже существовала.
 *
 * BOT_ERR_FILE:
 * - произошла ошибка Windows
 */
BotResult platform_create_directory(const char *path);

#endif // PLATFORM_H
