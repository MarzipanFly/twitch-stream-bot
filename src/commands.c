#include "commands.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/*
 * ============================================================
 * ПАРАМЕТРЫ ЗАПУСКА ПРОГРАММЫ
 * ============================================================
 */
int command_parse(
    int argc,
    char *argv[],
    CommandOptions *options
)
{
    int i;


    if (options == NULL)
    {
        return 0;
    }


    memset(
        options,
        0,
        sizeof(*options)
    );


    for (i = 1; i < argc; ++i)
    {
        if (
            strcmp(
                argv[i],
                "--test-stream"
            ) == 0)
        {
            options->test_stream = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--dry-run"
            ) == 0)
        {
            options->dry_run = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--test-chat"
            ) == 0)
        {
            options->test_chat = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--test-twitch-chat"
            ) == 0)
        {
            options->test_twitch_chat = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--test-eventsub-chat"
            ) == 0)
        {
            options->test_eventsub_chat = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--test-twitch-command"
            ) == 0)
        {
            options->test_twitch_command = 1;
        }
        else if (
            strcmp(
                argv[i],
                "--help"
            ) == 0 ||
            strcmp(
                argv[i],
                "-h"
            ) == 0)
        {
            options->show_help = 1;
        }
        else
        {
            fprintf(
                stderr,
                "Unknown argument: %s\n",
                argv[i]
            );


            return 0;
        }
    }


    /*
     * --dry-run относится только
     * к тесту старта стрима.
     */
    if (
        options->dry_run &&
        !options->test_stream)
    {
        fprintf(
            stderr,
            "--dry-run requires --test-stream\n"
        );


        return 0;
    }


    /*
     * Одновременно запускаем
     * только один тестовый режим.
     */
    if (
        options->test_stream +
        options->test_chat +
        options->test_twitch_chat +
        options->test_eventsub_chat +
        options->test_twitch_command > 1)
    {
        fprintf(
            stderr,
            "Only one test mode can be used at a time\n"
        );


        return 0;
    }


    return 1;
}


void command_print_help(
    const char *program_name
)
{
    if (program_name == NULL)
    {
        program_name =
            "twitchbot";
    }


    printf(
        "Usage:\n"
        "  %s [options]\n"
        "\n"
        "Options:\n"
        "  --test-stream          Simulate stream start\n"
        "  --dry-run              Do not send test stream notification\n"
        "  --test-chat            Open local chat command test console\n"
        "  --test-twitch-chat     Send a real test message to Twitch chat\n"
        "  --test-eventsub-chat   Test EventSub chat processing offline\n"
        "  --test-twitch-command  Wait for one real Twitch command and reply\n"
        "  --help, -h             Show this help\n",
        program_name
    );
}


/*
 * ============================================================
 * ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ TWITCH-КОМАНД
 * ============================================================
 */


/*
 * Пропускает пробелы и табуляцию
 * в начале строки.
 */
static const char *chat_skip_spaces(
    const char *text
)
{
    if (text == NULL)
    {
        return NULL;
    }


    while (
        *text == ' ' ||
        *text == '\t')
    {
        ++text;
    }


    return text;
}


/*
 * Сравнивает найденное имя команды
 * с ожидаемым именем без создания
 * временной строки.
 */
static int chat_command_name_equals(
    const char *command_name,
    size_t command_length,
    const char *expected
)
{
    size_t expected_length;


    if (
        command_name == NULL ||
        expected == NULL)
    {
        return 0;
    }


    expected_length =
        strlen(
            expected
        );


    if (
        command_length !=
        expected_length)
    {
        return 0;
    }


    return
        strncmp(
            command_name,
            expected,
            command_length
        ) == 0;
}


/*
 * Проверяет, поместился ли результат snprintf()
 * полностью в переданный буфер.
 */
static int chat_response_is_valid(
    int written,
    size_t buffer_size
)
{
    if (written < 0)
    {
        return 0;
    }


    if (
        (size_t)written >=
        buffer_size)
    {
        return 0;
    }


    return 1;
}


/*
 * Один раз инициализирует генератор
 * псевдослучайных чисел.
 */
static void chat_random_init(void)
{
    static int initialized =
        0;


    if (initialized)
    {
        return;
    }


    srand(
        (unsigned int)time(NULL)
    );


    initialized =
        1;
}


/*
 * ============================================================
 * РАЗБОР СООБЩЕНИЯ TWITCH-ЧАТА
 * ============================================================
 */
int chat_command_parse(
    const char *message,
    char prefix,
    ChatCommand *command
)
{
    const char *name_start;
    const char *name_end;
    const char *arguments;

    size_t name_length;


    if (
        message == NULL ||
        command == NULL)
    {
        return 0;
    }


    command->type =
        CHAT_COMMAND_NONE;

    command->arguments =
        NULL;


    if (*message != prefix)
    {
        return 0;
    }


    name_start =
        message + 1;


    name_end =
        name_start;


    while (
        *name_end != '\0' &&
        *name_end != ' ' &&
        *name_end != '\t')
    {
        ++name_end;
    }


    name_length =
        (size_t)(
            name_end -
            name_start
        );


    if (name_length == 0)
    {
        command->type =
            CHAT_COMMAND_UNKNOWN;


        return 1;
    }


    arguments =
        chat_skip_spaces(
            name_end
        );


    if (
        arguments != NULL &&
        *arguments != '\0')
    {
        command->arguments =
            arguments;
    }


    /*
     * ========================================================
     * ИНФОРМАЦИОННЫЕ КОМАНДЫ
     * ========================================================
     */

    if (
        chat_command_name_equals(
            name_start,
            name_length,
            "тг"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "tg"
        ))
    {
        command->type =
            CHAT_COMMAND_TELEGRAM;
    }
	else if (
		chat_command_name_equals(
			name_start,
			name_length,
			"дискорд"
		) ||
		chat_command_name_equals(
			name_start,
			name_length,
			"дс"
		) ||
		chat_command_name_equals(
			name_start,
			name_length,
			"ds"
		))
	{
		command->type =
			CHAT_COMMAND_DISCORD;
	}
    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "команды"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "help"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "commands"
        ))
    {
        command->type =
            CHAT_COMMAND_HELP;
    }
    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "баланс"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "апельсины"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "balance"
        ))
    {
        command->type =
            CHAT_COMMAND_BALANCE;
    }


    /*
     * ========================================================
     * ИГРОВЫЕ КОМАНДЫ
     * ========================================================
     */

    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "монетка"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "монета"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "coin"
        ))
    {
        command->type =
            CHAT_COMMAND_COIN;
    }
    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "кости"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "кубик"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "dice"
        ))
    {
        command->type =
            CHAT_COMMAND_DICE;
    }
    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "шар"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "ball"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "8ball"
        ))
    {
        command->type =
            CHAT_COMMAND_EIGHT_BALL;
    }
    else if (
        chat_command_name_equals(
            name_start,
            name_length,
            "слот"
        ) ||
        chat_command_name_equals(
            name_start,
            name_length,
            "slot"
        ))
    {
        command->type =
            CHAT_COMMAND_SLOT;
    }
    else
    {
        command->type =
            CHAT_COMMAND_UNKNOWN;
    }


    return 1;
}


/*
 * ============================================================
 * ФОРМИРОВАНИЕ ОТВЕТА НА TWITCH-КОМАНДУ
 * ============================================================
 */
int chat_command_build_response(
    const ChatCommand *command,
    const char *telegram_url,
	const char *discord_url,
    char *buffer,
    size_t buffer_size
)
{
    int written;


    if (
        command == NULL ||
        buffer == NULL ||
        buffer_size == 0)
    {
        return 0;
    }


    chat_random_init();


    switch (command->type)
    {
        /*
         * ====================================================
         * !тг
         * ====================================================
         */
        case CHAT_COMMAND_TELEGRAM:

            if (
                telegram_url == NULL ||
                telegram_url[0] == '\0')
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "Telegram пока не настроен."
                    );
            }
            else
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "Наш Telegram: %s",
                        telegram_url
                    );
            }


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );

			/*
			 * ====================================================
			 * !дс
			 * ====================================================
			 */
			case CHAT_COMMAND_DISCORD:

				if (
					discord_url == NULL ||
					discord_url[0] == '\0')
				{
					written =
						snprintf(
							buffer,
							buffer_size,
							"Discord пока не настроен."
						);
				}
				else
				{
					written =
						snprintf(
							buffer,
							buffer_size,
							"Наш Discord: %s",
							discord_url
						);
				}


				return
					chat_response_is_valid(
						written,
						buffer_size
					);

        /*
         * ====================================================
         * !команды
         * ====================================================
         */
        case CHAT_COMMAND_HELP:

            written =
                snprintf(
                    buffer,
                    buffer_size,
					"Команды: !тг, !дс, !монетка, !кости, !шар <вопрос>, !слот"
                );


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );


        /*
         * ====================================================
         * !монетка
         * ====================================================
         */
        case CHAT_COMMAND_COIN:

            if (
                rand() % 2 == 0)
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "Монетка: орёл!"
                    );
            }
            else
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "Монетка: решка!"
                    );
            }


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );


        /*
         * ====================================================
         * !кости
         * ====================================================
         */
        case CHAT_COMMAND_DICE:
        {
            int value =
                1 + rand() % 6;


            written =
                snprintf(
                    buffer,
                    buffer_size,
                    "Кости: выпало %d",
                    value
                );


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );
        }


        /*
         * ====================================================
         * !шар
         * ====================================================
         */
        case CHAT_COMMAND_EIGHT_BALL:
        {
            static const char *answers[] =
            {
                "Определённо да.",
                "Скорее всего.",
                "Шансы хорошие.",
                "Спроси ещё раз позже.",
                "Пока непонятно.",
                "Скорее нет.",
                "Очень сомнительно.",
                "Нет."
            };


            int answer_index;


            if (
                command->arguments == NULL ||
                command->arguments[0] == '\0')
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "Использование: !шар <вопрос>"
                    );


                return
                    chat_response_is_valid(
                        written,
                        buffer_size
                    );
            }


            answer_index =
                rand() %
                (
                    sizeof(answers) /
                    sizeof(answers[0])
                );


            written =
                snprintf(
                    buffer,
                    buffer_size,
                    "Шар говорит: %s",
                    answers[answer_index]
                );


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );
        }


        /*
         * ====================================================
         * !слот
         * ====================================================
         */
        case CHAT_COMMAND_SLOT:
        {
            static const char *symbols[] =
            {
                "7",
                "BAR",
                "АПЕЛЬСИН",
                "ВИШНЯ",
                "ЗВЕЗДА"
            };


            int first;
            int second;
            int third;


            first =
                rand() % 5;

            second =
                rand() % 5;

            third =
                rand() % 5;


            if (
                first == second &&
                second == third)
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "[%s] [%s] [%s] ДЖЕКПОТ!",
                        symbols[first],
                        symbols[second],
                        symbols[third]
                    );
            }
            else
            {
                written =
                    snprintf(
                        buffer,
                        buffer_size,
                        "[%s] [%s] [%s]",
                        symbols[first],
                        symbols[second],
                        symbols[third]
                    );
            }


            return
                chat_response_is_valid(
                    written,
                    buffer_size
                );
        }


        case CHAT_COMMAND_UNKNOWN:
        case CHAT_COMMAND_NONE:
        default:

            return 0;
    }
}
