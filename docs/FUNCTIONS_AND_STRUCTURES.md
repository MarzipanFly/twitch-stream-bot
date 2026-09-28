# Функции и структуры

Этот документ — карта текущего исходного кода: что хранит каждая основная структура и какую роль выполняют функции.

## Базовые типы

### `BotResult`

Единый код результата операций проекта.

Основные значения:

- `BOT_OK` — успех;
- `BOT_AUTH_PENDING` — Device Code авторизация ещё не подтверждена;
- `BOT_ERR_CONFIG` — ошибка конфигурации;
- `BOT_ERR_FILE` — ошибка файла;
- `BOT_ERR_NETWORK` — ошибка сети/WinHTTP;
- `BOT_ERR_AUTH` — проблема OAuth;
- `BOT_ERR_JSON` — некорректный JSON;
- `BOT_ERR_TWITCH` — ошибка Twitch API;
- `BOT_ERR_TELEGRAM` — зарезервировано под Telegram;
- `BOT_ERR_STORAGE` — ошибка локального хранилища.

`bot_result_to_string()` превращает код в текст для логов.

## Конфигурация

### `TwitchConfig`

Хранит Twitch-настройки приложения:

- `client_id`, `client_secret`;
- текущие `access_token`, `refresh_token` в памяти процесса;
- `broadcaster_login`, `broadcaster_id`;
- будущие `bot_login`, `bot_user_id`.

### `TelegramConfig`

Будущая конфигурация Telegram:

- `bot_token`;
- `chat_id`.

### `BotConfig`

Общие параметры бота. Сейчас содержит `command_prefix`.

### `AppConfig`

Корневая структура конфигурации. Объединяет `TwitchConfig`, `TelegramConfig` и `BotConfig`.

### `config_load(filename, config)`

Читает INI-файл и заполняет `AppConfig`.

### `config_validate(config)`

Проверяет обязательные параметры перед дальнейшим запуском.

## Логирование

### `LogLevel`

Уровни `DEBUG`, `INFO`, `WARNING`, `ERROR`, `FATAL`.

### `logger_init(filename)`

Открывает файл журнала и готовит logger.

### `logger_shutdown()`

Закрывает файл журнала.

### `logger_set_level(level)`

Меняет минимальный выводимый уровень.

### `log_debug/info/warning/error/fatal(...)`

Variadic-функции журналирования с форматированием в стиле `printf`.

## HTTP

### `HttpResponse`

```c
typedef struct {
    unsigned long status_code;
    char *body;
    size_t body_size;
} HttpResponse;
```

`status_code` — HTTP-код, `body` — динамически выделенное тело ответа, `body_size` — его размер.

### `http_get(host, path, headers, response)`

Выполняет HTTPS GET через WinHTTP.

### `http_post(host, path, headers, body, response)`

Выполняет HTTPS POST.

### `http_response_free(response)`

Освобождает `response->body` и сбрасывает поля структуры. После любого завершённого HTTP-вызова это основная функция cleanup.

## Twitch User

### `TwitchUser`

Модель пользователя Twitch: ID, login, display name, broadcaster type, description и URL изображения профиля.

### `twitch_get_user(config, login, response)`

Формирует запрос Helix `/users?login=...`, добавляет `Authorization: Bearer` и `Client-Id`, затем вызывает HTTP-клиент.

### `twitch_parse_user_response(json, user)`

Разбирает JSON Helix и заполняет `TwitchUser`.

## OAuth

### `TwitchDeviceCode`

Данные незавершённой Device Code авторизации:

- `device_code` — служебный код для polling;
- `user_code` — код, который вводит пользователь;
- `verification_uri` — адрес Twitch;
- `expires_in` — срок жизни;
- `interval` — интервал polling.

### `TwitchAuthToken`

Пара OAuth-токенов и `expires_in`.

### `TwitchTokenValidation`

Результат `/oauth2/validate`: Client ID, login, user ID и остаток срока действия.

### `twitch_auth_request_device_code(config, device)`

Запрашивает Device Code у Twitch.

### `twitch_auth_poll_token(config, device, token)`

Проверяет, подтвердил ли пользователь авторизацию. Пока подтверждения нет, возвращает `BOT_AUTH_PENDING`.

### `twitch_auth_validate_token(access_token, validation)`

Проверяет access token через Twitch validation endpoint.

### `twitch_refresh_access_token(config, refresh_token, new_token)`

Обменивает refresh token на новую пару OAuth-токенов.

## Защищённое хранение

### `token_store_save(token)`

Сериализует OAuth-данные, шифрует через `CryptProtectData` и записывает в `data/twitch_tokens.dat`.

### `token_store_load(token)`

Читает файл, расшифровывает через `CryptUnprotectData`, проверяет magic/version и возвращает сохранённые токены.

### `token_store_delete()`

Удаляет локальный token store.

## Twitch Stream

### `TwitchStream`

Модель текущей трансляции:

- `is_live`;
- stream/user ID;
- login/name;
- game ID/name;
- title;
- viewer count;
- started_at;
- language.

### `twitch_get_stream(config, user_id, response)`

Выполняет Helix-запрос `/streams?user_id=...`.

### `twitch_parse_stream_response(json, stream)`

Разбирает ответ. Пустой массив `data` означает OFFLINE. Наличие объекта означает ONLINE, после чего поля копируются в `TwitchStream`.

## Application layer — `app.c`

### `console_ctrl_handler(control_type)`

Windows callback для Ctrl+C/закрытия консоли. Устанавливает флаг остановки.

### `stop_requested()`

Безопасно читает атомарный флаг завершения.

### `sleep_interruptible(seconds)`

Спит короткими шагами и позволяет быстро выйти после Ctrl+C вместо ожидания полного polling-интервала.

### `copy_token_to_config(config, token)`

Копирует полученные OAuth-токены в рабочую конфигурацию процесса.

### `validate_current_token(config)`

Вызывает Twitch validation и дополнительно проверяет, что токен принадлежит ожидаемому Client ID.

### `run_device_authorization(config, token)`

Оркестрирует Device Code Flow: получает код, показывает данные пользователю и делает polling до успеха/истечения срока.

### `ensure_twitch_auth(config)`

Главная функция восстановления OAuth-сессии:

1. загрузить сохранённые токены;
2. провалидировать access token;
3. при необходимости выполнить refresh;
4. если восстановление невозможно — запустить Device Code Flow;
5. сохранить новую пару токенов.

### `get_current_stream(config, stream)`

Вспомогательная обёртка: получить HTTP-ответ Twitch, распарсить `TwitchStream`, освободить `HttpResponse`.

### `log_stream_information(stream)`

Пишет в лог название, категорию, viewers, время старта и язык активной трансляции.

### `monitor_stream(config)`

Основной долгоживущий цикл бота. Получает baseline-состояние, каждые 30 секунд повторяет запрос и обнаруживает переходы ONLINE/OFFLINE. Здесь находится точка будущей отправки Telegram-уведомления.

### `app_run()`

Главный оркестратор приложения:

1. создаёт директории;
2. запускает logger;
3. загружает config;
4. проверяет Интернет;
5. восстанавливает OAuth;
6. получает broadcaster ID;
7. запускает `monitor_stream()`;
8. штатно завершает приложение.

## Platform

### `platform_create_directory(path)`

Создаёт директорию в Windows или подтверждает, что она уже существует.

## `main()`

Минимальная точка входа:

```c
int main(void)
{
    return app_run();
}
```

Вся бизнес-логика намеренно находится вне `main.c`.
