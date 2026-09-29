#include <commands.h>

#include <stdio.h>
#include<string.h>

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
				"--help"
			) == 0 ||
			 strcmp(
				 argv[i],
				 "--h"
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
	 * --dry-run используется только вместе
	 * с тестовым событием стрима.
	 */
	if (options->dry_run && !options->test_stream)
	{
		fprintf(stderr, "--dry-run requires --test-stream");

		return 0;
	}

	return 1;
}

void command_print_help(const char *program_name)
{
	if (program_name == NULL)
	{
		program_name = "twitchbot";
	}

	printf(
		"Usage:\n"
		"  %s [options]\n"
		"\n"
		"Options:\n"
		"  --test-stream\t" "Simulate stream start\n"
		"  --dry-run\t"		"Do not perform network\n"
		"  --help, -h\t"	"Show this help\n",
		program_name
	);
}
//	if (message == NULL || reply == NULL || reply_size == 0)
//	{
//		return 0;
//	}

//	reply[0] = '\0';

//	if (strcmp(message, "!tg") == 0 ||
//		strcmp(message, "!тг") == 0 ||
//		strcmp(message, "!телега") == 0)
//	{
//		snprintf(
//			reply,
//			reply_size,
//			"Тележка: ссылка"
//		);

//		return 1;
//	}

//	if (strcmp(message, "!ds") == 0 ||
//		strcmp(message, "!дс") == 0 ||
//		strcmp(message, "!дисик") == 0)
//	{
//		snprintf(
//			reply,
//			reply_size,
//			"Дисик: ссылка"
//		);

//		return 1;
//	}

//	if (strcmp(message, "!commands") == 0 ||
//		strcmp(message, "!command") == 0 ||
//		strcmp(message, "!команды") == 0)
//	{
//		snprintf(
//			reply,
//			reply_size,
//			"Дисик: ссылка"
//		);

//		return 1;
//	}
//}
