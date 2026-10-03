#ifndef TELEGRAM_API_H
#define TELEGRAM_API_H

#include "bot_result.h"
#include "config.h"

BotResult telegram_send_message(
    const TelegramConfig *config,
    const char *text
);

#endif
