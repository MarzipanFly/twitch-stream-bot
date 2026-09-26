#ifndef LOGGER_H
#define LOGGER_H
typedef enum
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

/*
 * Инициализация логера
 */
int logger_init(const char *filename);

void logger_shutdown(void);

/*
 * Устанавливает минимальный уровень сообщений
 */
void logger_set_level(LogLevel level);

/*
 * Функция вывода сообщений
 */
void log_debug(const char *format, ...);
void log_info(const char *format, ...);
void log_warning(const char *format, ...);
void log_error(const char *format, ...);
void log_fatal(const char *format, ...);

#endif // LOGGER_H
