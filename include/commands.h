#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>


/*
 * Параметры запуска программы.
 */
typedef struct
{
    int test_stream;
    int dry_run;
    int test_chat;
    int test_twitch_chat;
    int test_eventsub_chat;
    int test_twitch_command;
    int show_help;

} CommandOptions;


/*
 * Разбирает аргументы командной строки.
 *
 * Возвращает:
 * 1 - аргументы корректны;
 * 0 - найден неизвестный или некорректный аргумент.
 */
int command_parse(
    int argc,
    char *argv[],
    CommandOptions *options
);


/*
 * Выводит справку по параметрам запуска.
 */
void command_print_help(
    const char *program_name
);


/*
 * ============================================================
 * КОМАНДЫ TWITCH-ЧАТА
 * ============================================================
 */

typedef enum
{
    CHAT_COMMAND_NONE = 0,

    CHAT_COMMAND_TELEGRAM,
	CHAT_COMMAND_DISCORD,
    CHAT_COMMAND_HELP,
    CHAT_COMMAND_BALANCE,

    CHAT_COMMAND_COIN,
    CHAT_COMMAND_DICE,
    CHAT_COMMAND_EIGHT_BALL,
    CHAT_COMMAND_SLOT,

    CHAT_COMMAND_UNKNOWN

} ChatCommandType;


typedef struct
{
    ChatCommandType type;

    /*
     * Указатель на текст после имени команды.
     *
     * Пример:
     * !шар Будет сегодня стрим?
     *
     * arguments -> "Будет сегодня стрим?"
     */
    const char *arguments;

} ChatCommand;


/*
 * Разбирает одно сообщение Twitch-чата.
 *
 * Возвращает:
 * 1 - сообщение начинается с командного префикса;
 * 0 - это обычное сообщение.
 */
int chat_command_parse(
    const char *message,
    char prefix,
    ChatCommand *command
);


/*
 * Формирует ответ бота на уже разобранную команду.
 *
 * telegram_url используется командой !тг.
 *
 * Возвращает:
 * 1 - ответ сформирован;
 * 0 - отвечать не нужно или произошла ошибка.
 */
int chat_command_build_response(
    const ChatCommand *command,
    const char *telegram_url,
	const char *discord_url,
    char *buffer,
    size_t buffer_size
);


#endif
