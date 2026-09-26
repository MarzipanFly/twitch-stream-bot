#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static LogLevel current_log_level = LOG_LEVEL_DEBUG;

static FILE *log_file = NULL;

static const char *log_level_to_string(LogLevel level)
{
    switch (level)
    {
        case LOG_LEVEL_DEBUG:
            return "DEBUG";

        case LOG_LEVEL_INFO:
            return "INFO";

        case LOG_LEVEL_WARNING:
            return "WARNING";

        case LOG_LEVEL_ERROR:
            return "ERROR";

        case LOG_LEVEL_FATAL:
            return "FATAL";

        default:
            return "UNKNOWN";
    }
}

static void write_log_line(
		FILE *stream,
		const char *timestamp,
		LogLevel level,
		const char *format,
		va_list args
)
{
	fprintf(stream,
			"%s [%-7s] ",
			timestamp,
			log_level_to_string(level)
	);

	vfprintf(
			stream,
			format,
			args
	);

	fprintf(
			stream,
			"\n"
	);

	fflush(stream);
}

static void log_write(
		LogLevel level,
		const char *format,
		va_list args
)
{
    time_t now;
    struct tm *local_time;

	char time_buffer[32];

	va_list file_args;

    if (level < current_log_level)
    {
        return;
    }

    now = time(NULL);
    local_time = localtime(&now);

    if (local_time != NULL)
    {
        strftime(
            time_buffer,
            sizeof(time_buffer),
            "%Y-%m-%d %H:%M:%S",
            local_time
        );
    }
    else
    {
        snprintf(
            time_buffer,
            sizeof(time_buffer),
            "0000-00-00 00:00:00"
        );
    }

	/*
	 * Копируем список аргументов
	 * потому что va_list екльзя безопасно
	 * использовать два раза подряд
	 */
	va_copy(
		file_args,
		args
	);

	/*
	 * Вывод в консоль
	 */
	write_log_line(
		stdout,
		time_buffer,
		level,
		format,
		args
	);

	/*
	 * Вывод в файл
	 */

	if (log_file != NULL)
	{
		write_log_line(
					log_file,
					time_buffer,
					level,
					format,
					file_args
		);
	}

	va_end(file_args);
}

int logger_init(const char *filename)
{
    current_log_level = LOG_LEVEL_DEBUG;

	if (filename == NULL)
	{
		return -1;
	}

	log_file = fopen(
				filename,
				"a"
	);

	if (log_file == NULL)
	{
		return -1;
	}
	return 0;
}

void logger_shutdown(void)
{
	if (log_file != NULL)
	{
		fclose(log_file);

		log_file = NULL;
	}
}

void logger_set_level(LogLevel level)
{
    current_log_level = level;
}

void log_debug(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_write(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

void log_info(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_write(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void log_warning(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_write(LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_write(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void log_fatal(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_write(LOG_LEVEL_FATAL, format, args);
    va_end(args);
}
