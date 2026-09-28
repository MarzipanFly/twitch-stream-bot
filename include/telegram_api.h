#ifndef TELEGRAM_API_H
#define TELEGRAM_API_H

#include "bot_result.h"
#include "config.h"


/*
 * Отправляет обычное текстовое сообщение
 * в Telegram-чат / группу / канал.
 *
 * config:
 *     Telegram-настройки:
 *     bot_token
 *     chat_id
 *
 * text:
 *     текст сообщения.
 */
BotResult telegram_send_message(
    const TelegramConfig *config,
    const char *text
);


#endif
