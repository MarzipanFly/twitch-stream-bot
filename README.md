# Twitch Stream Bot

Windows-консольный Twitch-бот на C. Проект собирается через **qmake / Qt Creator**, при этом сам код не использует Qt: сеть работает через **WinHTTP**, безопасное локальное хранение OAuth-токенов — через **Windows DPAPI (Crypt32)**.

## Что уже работает

- загрузка настроек из `config.ini`;
- Twitch Device Code OAuth;
- безопасное сохранение `access_token` и `refresh_token` в `data/twitch_tokens.dat`;
- повторная загрузка и валидация сохранённой OAuth-сессии;
- обновление access token через refresh token;
- получение Twitch-пользователя через Helix;
- получение текущего статуса трансляции через `/helix/streams`;
- постоянный мониторинг канала раз в 30 секунд;
- обнаружение переходов `OFFLINE -> ONLINE` и `ONLINE -> OFFLINE`;
- плановая проверка OAuth-сессии;
- корректное завершение по `Ctrl+C`;
- логирование в консоль и `logs/bot.log`.

Telegram пока не подключён. Точка для будущего уведомления уже находится в обработке события `STREAM STARTED`.

## Структура проекта

```text
twitch-stream-bot/
├── include/                # публичные заголовки модулей
├── src/                    # реализация
├── third_party/cjson/      # cJSON
├── docs/                   # документация проекта
├── twitchbot.pro           # qmake-проект
└── .gitignore
```

Основные модули:

| Модуль | Назначение |
| --- | --- |
| `app` | жизненный цикл приложения и основной цикл мониторинга |
| `config` | чтение и проверка `config.ini` |
| `logger` | журналирование |
| `http_client` | HTTPS GET/POST через WinHTTP |
| `twitch_auth` | Device Code OAuth и validation |
| `twitch_refresh` | обновление OAuth-токенов |
| `token_store` | шифрование и хранение токенов через DPAPI |
| `twitch_api` | получение информации о Twitch-пользователе |
| `twitch_stream` | получение и разбор состояния трансляции |
| `platform_win` | Windows-зависимые операции |
| `bot_result` | единые коды результата |

Подробности: [функции и структуры](docs/FUNCTIONS_AND_STRUCTURES.md), [архитектура](docs/ARCHITECTURE.md), [текущее состояние](docs/PROJECT_STATUS.md).

## Требования

- Windows 10/11;
- Qt Creator с qmake;
- MinGW или MSVC toolchain;
- доступ в Интернет;
- зарегистрированное Twitch Application и его Client ID.

В qmake подключаются системные библиотеки:

```text
WinHTTP
Crypt32
```

## Конфигурация

`config.ini` намеренно находится в `.gitignore` и **не должен попадать в GitHub**.

Минимальный пример:

```ini
[twitch]

client_id=YOUR_TWITCH_CLIENT_ID
client_secret=

access_token=
refresh_token=

broadcaster_login=YOUR_TWITCH_LOGIN
broadcaster_id=

bot_login=
bot_user_id=

[telegram]

bot_token=
chat_id=

[bot]

command_prefix=!
```

`access_token` и `refresh_token` вручную заполнять не нужно: после Device Code авторизации программа сохраняет их в зашифрованном DPAPI-файле.

## Сборка

Открыть `twitchbot.pro` в Qt Creator и выполнить:

```text
Run qmake
Clean Project
Rebuild Project
Run
```

При первом запуске программа выдаст код и URL Twitch для Device Code авторизации. При следующих запусках используется сохранённая OAuth-сессия, пока её можно валидировать или обновить.

## Как работает мониторинг

```text
start
  |
  +--> load config
  +--> validate/restore OAuth
  +--> get broadcaster ID
  +--> get initial stream status
  |
  +--> loop every 30 seconds
         |
         +--> OFFLINE -> ONLINE  => STREAM STARTED
         +--> ONLINE  -> OFFLINE => STREAM ENDED
```

Если бот запускается уже во время эфира, это состояние считается исходным и не создаёт ложное событие `STREAM STARTED`.

## Безопасность

В репозиторий не должны попадать:

- `config.ini`;
- `data/twitch_tokens.dat`;
- Twitch access/refresh tokens;
- Telegram bot token;
- локальные логи.

DPAPI-файл привязан к Windows-профилю пользователя и не предназначен для переноса между компьютерами.

## Текущий план

Ближайшие этапы:

1. Telegram Bot API и отправка сообщения на `STREAM STARTED`;
2. тест полного перехода `OFFLINE -> ONLINE -> OFFLINE`;
3. EventSub WebSocket вместо регулярного polling;
4. Twitch chat и команды;
5. долговременное состояние/дедупликация событий.

Проект находится в активной разработке.
