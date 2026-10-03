# Функции и структуры Twitch Stream Bot

Документ описывает **текущий код**, а не будущую архитектуру. Главная цель — быстро понять, где находится нужная логика, что функция принимает, что возвращает и кто её вызывает.

---

# 1. `include/bot_result.h` / `src/bot_result.c`

## `BotResult`

Единый enum кодов результата, которым пользуются почти все модули.

| Значение | Роль |
|---|---|
| `BOT_OK` | Успешное выполнение |
| `BOT_AUTH_PENDING` | Device Code авторизация ещё ждёт пользователя |
| `BOT_ERR_UNKNOWN` | Ошибка, не попавшая в более точную категорию |
| `BOT_ERR_CONFIG` | Неверная конфигурация |
| `BOT_ERR_FILE` | Ошибка файла/директории |
| `BOT_ERR_NETWORK` | WinHTTP или сетевая ошибка |
| `BOT_ERR_AUTH` | Ошибка OAuth / невалидный токен |
| `BOT_ERR_JSON` | Не удалось разобрать ожидаемый JSON |
| `BOT_ERR_TWITCH` | Ошибка Twitch API |
| `BOT_ERR_TELEGRAM` | Зарезервировано для Telegram |
| `BOT_ERR_STORAGE` | Ошибка локального хранилища |

## `bot_result_to_string()`

```c
const char *bot_result_to_string(BotResult result);
```

Преобразует enum в строку для логов.

---

# 2. `include/config.h` / `src/config.c`

## `TwitchConfig`

Хранит параметры Twitch.

| Поле | Назначение |
|---|---|
| `client_id` | Client ID Twitch Application |
| `client_secret` | Сейчас не используется Device Code Flow |
| `access_token` | Текущий access token в памяти процесса |
| `refresh_token` | Текущий refresh token в памяти процесса |
| `broadcaster_login` | Логин отслеживаемого канала |
| `broadcaster_id` | Числовой Twitch user ID, полученный через Helix |
| `bot_login` | Зарезервировано под отдельный bot account |
| `bot_user_id` | Зарезервировано под ID bot account |

Токены в `TwitchConfig` — **рабочая копия в памяти**. Долговременное хранение выполняет `token_store.c`.

## `TelegramConfig`

| Поле | Назначение |
|---|---|
| `bot_token` | Telegram Bot API token; пока не используется |
| `chat_id` | Канал/чат назначения; пока не используется |

## `BotConfig`

| Поле | Назначение |
|---|---|
| `command_prefix` | Префикс будущих Twitch-команд, сейчас `!` |

## `AppConfig`

Объединяет:

```c
TwitchConfig twitch;
TelegramConfig telegram;
BotConfig bot;
```

Это основная конфигурация, живущая в `app_run()`.

## `trim_left()` — static

Убирает пробельные символы слева и возвращает указатель на первый полезный символ. Память не выделяет.

## `trim_right()` — static

Убирает пробелы и переводы строки в конце C-строки, записывая `'\0'`.

## `copy_string()` — static

Безопасно копирует строку через `snprintf()` с учётом размера массива назначения.

## `parse_section()` — static

Определяет текущую INI-секцию:

```text
[twitch]
[telegram]
[bot]
```

## `parse_key_value()` — static

Разделяет строку по первому `=` и записывает значение в соответствующее поле `AppConfig`.

## `config_load()`

```c
BotResult config_load(
    const char *filename,
    AppConfig *config
);
```

Читает INI-файл построчно. Перед чтением полностью обнуляет `AppConfig` и задаёт `command_prefix='!'` по умолчанию.

## `config_validate()`

Проверяет базовые обязательные поля. Сейчас обязательно наличие:

- `broadcaster_login`;
- ненулевого `command_prefix`.

`client_id` дополнительно проверяется в `app_run()`.

---

# 3. `include/logger.h` / `src/logger.c`

## `LogLevel`

```text
DEBUG
INFO
WARNING
ERROR
FATAL
```

## `logger_init()`

Открывает файл лога в режиме append.

```c
logger_init("logs/bot.log");
```

## `logger_shutdown()`

Закрывает файловый дескриптор логгера.

## `logger_set_level()`

Задаёт минимальный уровень сообщений.

## `log_debug()`, `log_info()`, `log_warning()`, `log_error()`, `log_fatal()`

Variadic-функции в стиле `printf`.

Пример:

```c
log_info("Broadcaster ID: %s", user.id);
```

## `level_to_string()` — static

Преобразует `LogLevel` в подпись для строки журнала.

## `log_message()` — static

Центральная реализация логирования:

1. строит timestamp;
2. печатает в консоль;
3. печатает в `bot.log`;
4. вызывает `fflush`, чтобы лог не терялся при падении программы.

---

# 4. `include/platform.h` / `src/platform_win.c`

## `platform_create_directory()`

```c
BotResult platform_create_directory(const char *path);
```

Windows-обёртка над:

```text
GetFileAttributesA
CreateDirectoryA
GetLastError
```

Если каталог уже существует — это считается успехом.

Сейчас создаёт:

```text
logs/
data/
```

---

# 5. `include/http_client.h` / `src/http_client.c`

## `HttpResponse`

```c
typedef struct
{
    unsigned long status_code;
    char *body;
    size_t body_size;
} HttpResponse;
```

### `status_code`

HTTP status:

```text
200
400
401
403
...
```

### `body`

Динамически выделенный буфер ответа.

### `body_size`

Размер payload в байтах без завершающего `'\0'`.

## `http_get()`

```c
BotResult http_get(
    const wchar_t *host,
    const wchar_t *path,
    const wchar_t *additional_headers,
    HttpResponse *response
);
```

Делает HTTPS GET через WinHTTP.

Последовательность:

```text
WinHttpOpen
WinHttpConnect
WinHttpOpenRequest
WinHttpAddRequestHeaders
WinHttpSendRequest
WinHttpReceiveResponse
WinHttpQueryHeaders
WinHttpReadData
```

## `http_post()`

Аналогично `http_get()`, но отправляет POST body.

Используется OAuth-модулем.

## `http_response_free()`

**Обязательная функция после использования ответа.**

Освобождает `response->body`, затем обнуляет структуру ответа.

## `read_response_body()` — static

Читает тело ответа кусками. По мере поступления данных расширяет `body` через `realloc()`.

## `query_status_and_body()` — static

Получает HTTP status code и затем вызывает `read_response_body()`.

## `log_winhttp_error()` — static

Берёт `GetLastError()` и пишет Windows error code в лог.

---

# 6. `include/twitch_auth.h` / `src/twitch_auth.c`

## `TwitchDeviceCode`

Состояние Device Code Flow.

| Поле | Роль |
|---|---|
| `device_code` | Машинный код, используемый при polling |
| `user_code` | Код, который видит и вводит пользователь |
| `verification_uri` | Адрес Twitch для подтверждения |
| `expires_in` | Время жизни кода |
| `interval` | Минимальный интервал polling |

## `TwitchAuthToken`

```c
char access_token[1024];
char refresh_token[1024];
int expires_in;
```

Хранит пару OAuth-токенов, полученную от Twitch.

## `TwitchTokenValidation`

Результат `/oauth2/validate`.

| Поле | Роль |
|---|---|
| `client_id` | Какому Twitch Application принадлежит токен |
| `login` | Авторизованный Twitch login |
| `user_id` | Twitch user ID |
| `expires_in` | Сколько ещё живёт access token |

## `twitch_auth_request_device_code()`

POST:

```text
id.twitch.tv/oauth2/device
```

Получает `device_code`, `user_code`, URI, lifetime и polling interval.

## `twitch_auth_poll_token()`

POST:

```text
id.twitch.tv/oauth2/token
```

Пока пользователь не подтвердил код, возвращает:

```text
BOT_AUTH_PENDING
```

После подтверждения заполняет `TwitchAuthToken`.

## `twitch_auth_validate_token()`

GET:

```text
id.twitch.tv/oauth2/validate
```

Проверяет access token и заполняет `TwitchTokenValidation`.

---

# 7. `include/twitch_refresh.h` / `src/twitch_refresh.c`

## `url_encode_component()` — static

Percent-encoding для refresh token перед `application/x-www-form-urlencoded` запросом.

Например специальный байт превращается в `%XX`.

## `twitch_refresh_access_token()`

```c
BotResult twitch_refresh_access_token(
    const TwitchConfig *config,
    const char *refresh_token,
    TwitchAuthToken *new_token
);
```

Обменивает refresh token на новую пару токенов.

При успехе `new_token` должен сразу попасть в DPAPI-хранилище.

---

# 8. `include/token_store.h` / `src/token_store.c`

## `TokenStorePayload` — private

Внутренний binary payload:

```c
DWORD magic;
DWORD version;
TwitchAuthToken token;
```

`magic` и `version` помогают отличить ожидаемый формат файла от случайных/устаревших данных.

## `token_store_save()`

1. собирает `TokenStorePayload`;
2. вызывает `CryptProtectData()`;
3. записывает зашифрованный blob в:

```text
data/twitch_tokens.dat
```

Сырые токены в лог не выводятся.

## `token_store_load()`

1. читает файл;
2. вызывает `CryptUnprotectData()`;
3. проверяет magic/version;
4. возвращает `TwitchAuthToken`.

## `token_store_delete()`

Удаляет сохранённый файл токенов, когда сессия повреждена или её невозможно восстановить.

---

# 9. `include/twitch_user.h`

## `TwitchUser`

Модель объекта пользователя из Helix `/users`.

| Поле | Twitch JSON |
|---|---|
| `id` | `id` |
| `login` | `login` |
| `display_name` | `display_name` |
| `broadcaster_type` | `broadcaster_type` |
| `description` | `description` |
| `profile_image_url` | `profile_image_url` |

Twitch ID хранится строкой, а не целым числом.

---

# 10. `include/twitch_api.h` / `src/twitch_api.c`

## `utf8_to_wide()` — static

Преобразует UTF-8 `char *` в Windows `wchar_t *`, потому что WinHTTP API использует wide strings.

## `copy_json_string()` — static

Берёт строковое поле из cJSON object и копирует в фиксированный C-массив структуры.

## `twitch_get_user()`

Формирует:

```text
GET https://api.twitch.tv/helix/users?login=<login>
```

Добавляет:

```text
Authorization: Bearer ...
Client-Id: ...
```

Функция получает **сырой HTTP-ответ**, но не разбирает JSON.

## `twitch_parse_user_response()`

Разбирает:

```json
{
  "data": [
    {
      "id": "...",
      "login": "..."
    }
  ]
}
```

и заполняет `TwitchUser`.

---

# 11. `include/twitch_stream.h` / `src/twitch_stream.c`

## `TwitchStream`

| Поле | Значение |
|---|---|
| `is_live` | 0 offline / 1 online |
| `id` | ID конкретной трансляции |
| `user_id` | ID стримера |
| `user_login` | login стримера |
| `user_name` | display name |
| `game_id` | Twitch category/game ID |
| `game_name` | название категории |
| `title` | название стрима |
| `viewer_count` | текущие зрители |
| `started_at` | время начала |
| `language` | язык стрима |

## `twitch_get_stream()`

Формирует:

```text
GET /helix/streams?user_id=<id>
```

и возвращает сырой `HttpResponse`.

## `twitch_parse_stream_response()`

Если `data` — пустой массив:

```c
stream->is_live = 0;
```

Если в массиве есть stream object:

```c
stream->is_live = 1;
```

и остальные поля копируются в `TwitchStream`.

## `utf8_to_wide()` / `copy_json_string()` — static

Локальные helper-функции, аналогичные функциям в `twitch_api.c`.

---

# 12. `src/app.c`

Это основной orchestrator проекта.

## `g_stop_requested`

```c
static volatile LONG g_stop_requested;
```

Флаг штатного завершения.

## `console_ctrl_handler()`

Windows callback для:

```text
Ctrl+C
Ctrl+Break
закрытие консоли
```

Не делает тяжёлой работы. Только выставляет `g_stop_requested=1`.

## `stop_requested()`

Атомарно читает флаг остановки через `InterlockedCompareExchange()`.

## `sleep_interruptible()`

Вместо одного `Sleep(30000)` спит по 1 секунде. Поэтому `Ctrl+C` обрабатывается быстро.

## `copy_token_to_config()`

Копирует `TwitchAuthToken` в `TwitchConfig` для дальнейших Helix-запросов.

## `validate_current_token()`

Вызывает `twitch_auth_validate_token()` и дополнительно сверяет:

```text
validation.client_id == config.client_id
```

Это не даёт случайно использовать токен от другого Twitch Application.

## `run_device_authorization()`

Полный интерактивный Device Code цикл:

```text
request device code
      |
      v
показать user_code/URL
      |
      v
Sleep(interval)
      |
      v
poll token
      |
      +--> AUTH_PENDING -> повтор
      |
      +--> OK -> token
```

## `ensure_twitch_auth()`

Главная функция восстановления OAuth-сессии.

Порядок:

```text
token_store_load()
      |
      v
validate access token
      |
      +--> valid -> использовать
      |
      +--> invalid
              |
              v
        refresh token
              |
              +--> success -> сохранить новую пару
              |
              +--> fail -> Device Code Flow
```

## `get_current_stream()`

Удобная обёртка:

```text
twitch_get_stream()
        +
twitch_parse_stream_response()
```

Она также гарантирует освобождение `HttpResponse`.

## `log_stream_information()`

Пишет:

```text
title
category
viewers
started_at
language
```

## `monitor_stream()`

Основной долгоживущий цикл.

На старте:

```text
get current state
previous_live_state = current state
```

Затем каждые 30 секунд:

1. при необходимости проверяет OAuth;
2. вызывает `get_current_stream()`;
3. при `BOT_ERR_AUTH` пытается восстановить сессию;
4. при временной ошибке сети не убивает процесс;
5. сравнивает прошлое и новое состояние.

### OFFLINE -> ONLINE

```c
previous_live_state == 0 &&
current_stream.is_live != 0
```

Лог:

```text
STREAM STARTED
```

Именно сюда будет подключён Telegram.

### ONLINE -> OFFLINE

```c
previous_live_state != 0 &&
current_stream.is_live == 0
```

Лог:

```text
STREAM ENDED
```

### Состояние не поменялось

Логируется только `DEBUG`.

## `app_run()`

Главный жизненный цикл:

```text
создать logs/
инициализировать logger
установить Ctrl+C handler
создать data/
прочитать config.ini
проверить сеть
восстановить Twitch OAuth
получить TwitchUser
получить broadcaster_id
запустить monitor_stream()
shutdown
```

---

# 13. `src/main.c`

## `main()`

Намеренно минимальный:

```c
int main(void)
{
    return app_run();
}
```

`main.c` знает только о запуске приложения. Вся предметная логика находится в модулях.

---

# 14. `third_party/cjson`

В восстановленном архиве присутствует небольшой cJSON-совместимый parser.

Используются только:

```text
cJSON_Parse
cJSON_Delete
cJSON_GetObjectItemCaseSensitive
cJSON_GetArraySize
cJSON_GetArrayItem
cJSON_IsString
cJSON_IsNumber
cJSON_IsArray
cJSON_IsObject
```

## `cJSON`

Дерево JSON.

Главные поля, с которыми косвенно работает проект:

```text
type
valuestring
valueint
child
next
string
```

Приложение не должно вручную менять это дерево — только читать поля через функции API и в конце вызывать `cJSON_Delete()`.

---

# 15. Где что менять

Если нужно изменить интервал опроса Twitch:

```c
#define STREAM_POLL_INTERVAL_SECONDS 30
```

в `src/app.c`.

Если нужно добавить новый Helix endpoint — создавать отдельный модуль по модели:

```text
twitch_stream.c
twitch_api.c
```

Если нужно подключить Telegram, точка события уже есть внутри:

```c
monitor_stream()
```

в ветке:

```text
OFFLINE -> ONLINE
```

Если нужно добавить новый тип ошибки — менять:

```text
include/bot_result.h
src/bot_result.c
```

Если нужно добавить параметр INI — менять:

```text
include/config.h
src/config.c
config.example.ini
```

---

# 16. Важные правила проекта

1. Никогда не логировать access/refresh token.
2. После каждого `http_get/http_post` освобождать `HttpResponse`.
3. После `cJSON_Parse` обязательно делать `cJSON_Delete`.
4. `config.ini`, `data/` и `logs/` не коммитить.
5. Новый refresh token сохранять сразу после refresh.
6. Не считать `Initial stream status: ONLINE` событием начала эфира.
7. Ошибка одного polling-запроса не должна автоматически завершать постоянно работающего бота.
