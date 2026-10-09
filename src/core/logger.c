#include "logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static FILE *g_log_file = NULL;
static LogLevel g_log_level = LOG_LEVEL_DEBUG;

static const char *level_to_string(LogLevel level)
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

static void log_message(
    LogLevel level,
    const char *format,
    va_list args
)
{
    time_t now;
    struct tm time_info;
    char timestamp[32];

    va_list console_args;
    va_list file_args;

    if (format == NULL || level < g_log_level)
    {
        return;
    }

    now = time(NULL);

#ifdef _WIN32
    localtime_s(&time_info, &now);
#else
    localtime_r(&now, &time_info);
#endif

    strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%d %H:%M:%S",
        &time_info
    );

    va_copy(console_args, args);
    va_copy(file_args, args);

    fprintf(
        stdout,
        "%s [%-7s] ",
        timestamp,
        level_to_string(level)
    );

    vfprintf(
        stdout,
        format,
        console_args
    );

    fputc('\n', stdout);
    fflush(stdout);

    if (g_log_file != NULL)
    {
        fprintf(
            g_log_file,
            "%s [%-7s] ",
            timestamp,
            level_to_string(level)
        );

        vfprintf(
            g_log_file,
            format,
            file_args
        );

        fputc('\n', g_log_file);
        fflush(g_log_file);
    }

    va_end(console_args);
    va_end(file_args);
}

int logger_init(const char *filename)
{
    if (filename == NULL)
    {
        return -1;
    }

    g_log_file = fopen(
        filename,
        "a"
    );

    if (g_log_file == NULL)
    {
        return -1;
    }

    return 0;
}

void logger_shutdown(void)
{
    if (g_log_file != NULL)
    {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

void logger_set_level(LogLevel level)
{
    g_log_level = level;
}

void log_debug(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_message(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

void log_info(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_message(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

void log_warning(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_message(LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

void log_error(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_message(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

void log_fatal(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    log_message(LOG_LEVEL_FATAL, format, args);
    va_end(args);
}
