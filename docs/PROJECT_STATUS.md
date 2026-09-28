# Текущее состояние проекта

Актуально для ветки `main` после коммита **Add continuous Twitch stream monitoring**.

## Подтверждено запуском

По фактическим логам приложения были успешно проверены:

- Internet test через WinHTTP;
- Device Code OAuth;
- получение access/refresh token;
- DPAPI-сохранение OAuth-токенов;
- повторная загрузка сохранённой OAuth-сессии;
- `/oauth2/validate`;
- получение пользователя через `/helix/users`;
- получение статуса через `/helix/streams`;
- определение состояния OFFLINE;
- запуск постоянного polling-цикла.

Подтверждённый старт монитора:

```text
Initial stream status: OFFLINE
Stream monitor started
Polling interval: 30 seconds
Press Ctrl+C to stop the bot
```

## Ещё не подтверждено полным тестом

В переписке пока нет полного лога одного сеанса:

```text
OFFLINE -> ONLINE -> OFFLINE
```

Код для обнаружения обоих переходов уже реализован, но перед подключением Telegram полезно сделать живой тест.

## Не реализовано

- отправка Telegram-сообщений;
- Twitch chat;
- команды чата;
- EventSub WebSocket;
- SQLite/долговременное состояние;
- dedup событий между перезапусками.

## Ближайшая задача

Подключить Telegram Bot API и вызывать отправку сообщения только при:

```text
previous_live_state == OFFLINE
current_stream.is_live == ONLINE
```

## Технические заметки

### Секреты

`config.ini`, `data/` и `logs/` исключены из Git. OAuth-токены не должны попадать в репозиторий.

### Размер буферов токенов

`TwitchAuthToken` использует буферы 1024 байта, а поля токенов в текущем `TwitchConfig` используют общий `CONFIG_STRING_SIZE = 256`. Текущий токен работает, однако это технический долг: безопаснее либо увеличить token buffers в config, либо вообще перестать хранить runtime OAuth tokens внутри `TwitchConfig`.

### Polling

Сейчас статус проверяется каждые 30 секунд. Это намеренный промежуточный этап для простой отладки state-transition логики. Позже polling целесообразно заменить на EventSub WebSocket.
