# Архитектура Twitch Stream Bot

## 1. Общая схема

```text
main()
  |
  v
app_run()
  |
  +--> config / logger / directories
  +--> ensure_twitch_auth()
  +--> twitch_get_user()
  |
  +--> run_bot_loop()
         |
         +--> EventSub WebSocket
         |      +--> session_welcome
         |      +--> channel.chat.message subscription
         |      +--> Twitch chat command
         |      +--> chat_command_parse()
         |      +--> twitch_chat_send_message()
         |
         +--> каждые 30 секунд: stream status
         |      +--> OFFLINE -> ONLINE
         |      +--> Telegram notification
         |      +--> ONLINE -> OFFLINE
         |
         +--> периодическая OAuth validation
         +--> EventSub reconnect при разрыве
```

## 2. Слои проекта

### Application layer

Файлы:

```text
src/main.c
src/app.c
include/app.h
```

Отвечает за порядок запуска, жизненный цикл приложения, OAuth-сессию и основной цикл бота.

### Configuration and diagnostics

```text
src/config.c
src/logger.c
src/bot_result.c
```

Конфигурация, журналы и единый тип кодов возврата.

### Platform layer

```text
src/platform_win.c
```

Windows-зависимые операции файловой системы.

### HTTP transport

```text
src/http_client.c
```

Единая обёртка над WinHTTP. Twitch-модули не должны напрямую создавать WinHTTP handles.

### Twitch OAuth

```text
src/twitch_auth.c
src/twitch_refresh.c
src/token_store.c
```

Получение, проверка, обновление и безопасное локальное хранение OAuth-токенов.

### Twitch Helix API

```text
src/twitch_api.c
src/twitch_stream.c
```

Работа с `/helix/users` и `/helix/streams`.

### JSON

```text
third_party/cjson/
```

Разбор JSON-ответов Twitch.

## 3. Основные данные

### `AppConfig`

Корневая конфигурация приложения:

```text
AppConfig
├── TwitchConfig
├── TelegramConfig
└── BotConfig
```

В `app_run()` создаётся один объект `AppConfig`, который передаётся дальше по указателю.

### `HttpResponse`

Каждый HTTP-запрос возвращает:

```text
HttpResponse
├── status_code
├── body
└── body_size
```

`body` выделяется динамически внутри HTTP-модуля. После использования необходимо вызвать:

```c
http_response_free(&response);
```

### OAuth

```text
TwitchDeviceCode
     |
     v
TwitchAuthToken
     |
     +--> access_token
     +--> refresh_token
     |
     v
data/twitch_tokens.dat
```

`data/twitch_tokens.dat` хранит зашифрованную DPAPI-версию структуры с токенами.

### Поток

`TwitchStream.is_live` является главным состоянием мониторинга:

```text
0 = OFFLINE
1 = ONLINE
```

Монитор хранит предыдущее состояние и сравнивает его с текущим:

```text
previous=0 current=1 -> STREAM STARTED
previous=1 current=0 -> STREAM ENDED
```

## 4. Почему стартовое ONLINE не считается началом стрима

При запуске `monitor_stream()` сначала получает текущее состояние и записывает его в `previous_live_state`.

Если бот стартует во время уже идущего эфира:

```text
Initial stream status: ONLINE
```

событие `STREAM STARTED` не создаётся. Это защищает будущую Telegram-рассылку от повторного уведомления после перезапуска бота.

## 5. Обработка ошибок

Большинство функций возвращают `BotResult`.

Смысл:

```text
BOT_OK             операция успешна
BOT_AUTH_PENDING   Device Code ещё не подтверждён
BOT_ERR_CONFIG     ошибка конфигурации
BOT_ERR_FILE       файловая ошибка
BOT_ERR_NETWORK    WinHTTP/сеть
BOT_ERR_AUTH       OAuth/401/403
BOT_ERR_JSON       неожиданный или повреждённый JSON
BOT_ERR_TWITCH     Twitch API вернул ошибку
BOT_ERR_TELEGRAM   зарезервировано под Telegram
BOT_ERR_STORAGE    локальное хранилище токенов
```

Критические ошибки этапа инициализации завершают процесс. Ошибка отдельной проверки стрима внутри долгого цикла логируется, после чего цикл продолжается.

## 6. Владение памятью

Особенно важно для `HttpResponse`.

`http_get()` / `http_post()`:

- выделяют `response.body` через `malloc/realloc`;
- вызывающий код получает владение буфером;
- вызывающий код обязан вызвать `http_response_free()`.

JSON:

- `cJSON_Parse()` создаёт дерево;
- `cJSON_Delete()` освобождает всё дерево;
- указатели, возвращённые `cJSON_GetObjectItemCaseSensitive()` и `cJSON_GetArrayItem()`, принадлежат дереву и отдельно не освобождаются.

DPAPI:

- `CryptProtectData()` и `CryptUnprotectData()` выделяют память через Windows;
- она освобождается `LocalFree()`.

## 7. Периодические процессы

На текущем этапе работают два интервала:

```text
Проверка стрима:      каждые 30 секунд
Проверка OAuth token: примерно раз в час
```

Цикл можно остановить `Ctrl+C`. Обработчик Windows выставляет `g_stop_requested`, после чего основной цикл завершает работу штатно.

## 8. Следующие модули

Логичное продолжение проекта:

```text
telegram.c/.h
    -> sendMessage при STREAM STARTED

EventSub WebSocket
    -> заменить polling событиями stream.online/stream.offline

chat.c / commands.c
    -> команды !help, !uptime, !game, !title ...

storage.c
    -> SQLite для состояния/дедупликации
```
