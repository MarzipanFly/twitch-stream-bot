#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h>

typedef struct
{
	int test_stream;
	int dry_run;
	int show_help;
} CommandOptions;

/*
 * Разбирает аргументы командной строки
 *
 * Возвращает
 * 1 - аргументы корректны;
 * 0 - найдены неизвестная или неправильный команда.
 */
int command_parse(
		int argc,
		char *argv[],
		CommandOptions *options
);

/*
 * выводит справку по параметрами запуска
 */
void command_print_help(
		const char *program_name
);


#endif // COMMANDS_H
