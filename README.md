# Twitch Stream Bot

Консольный Twitch-бот на **C** для Windows. Проект собирается через **qmake / Qt Creator** и использует Twitch Helix API, EventSub WebSocket и Telegram Bot API.

**Текущая версия: 0.6.0.0**

На этом этапе основной фундамент бота готов: он авторизуется в Twitch, следит за состоянием стрима, отправляет уведомления в Telegram, постоянно слушает Twitch-чат через EventSub и отвечает на команды зрителей.

## Возможности

- Twitch Device Code OAuth;
- безопасное хранение `access_token` и `refresh_token` через Windows DPAPI;
- автоматическая загрузка, проверка и обновление OAuth-сессии;
- получение данных Twitch-пользователя через Helix;
- мониторинг стрима через `GET /helix/streams`;
- обнаружение переходов `OFFLINE -> ONLINE` и `ONLINE -> OFFLINE`;
- Telegram-уведомление при начале стрима;
- отправка сообщений в Twitch-чат через Helix;
- получение сообщений Twitch-чата через EventSub WebSocket;
- постоянная обработка команд в обычном режиме запуска;
- повторное подключение EventSub после потери соединения;
- UTF-8 в Windows-консоли;
- корректная остановка по `Ctrl+C`;
- логирование в `logs/bot.log`.

## Как работает бот

```text
Twitch OAuth
    |
    +--> Helix API ------------------> состояние стрима
    |                                     |
    |                                     +--> старт стрима --> Telegram
    |
    +--> EventSub WebSocket ----------> сообщения Twitch-чата
                                          |
                                          +--> парсер команд
                                                  |
                                                  +--> ответ через Helix Chat API
```

В обычном режиме бот работает постоянно: EventSub принимает сообщения чата, а основной цикл продолжает выполнять периодические задачи, включая проверку состояния трансляции.

## Команды Twitch-чата

| Команда | Алиасы | Что делает |
| --- | --- | --- |
| `!команды` | `!help`, `!commands` | Показывает доступные команды |
| `!тг` | `!tg` | Отправляет публичную ссылку на Telegram-канал |
| `!монетка` | `!монета`, `!coin` | Подбрасывает монетку |
| `!кости` | `!кубик`, `!dice` | Бросает кубик от 1 до 6 |
| `!шар <вопрос>` | `!ball`, `!8ball` | Отвечает в стиле Magic 8 Ball |
| `!слот` | `!slot` | Запускает простой слот |

Префикс команд задаётся в `config.ini`. По умолчанию используется `!`.

Для команды `!тг` необходимо заполнить `channel_url` в секции `[telegram]`. Это отдельная публичная ссылка и она не заменяет `chat_id`, который используется Telegram Bot API.

## Быстрый запуск

1. Скопируйте `config.example.ini` в `config.ini`.
2. Укажите Twitch `client_id` и `broadcaster_login`.
3. При необходимости настройте Telegram.
4. Соберите проект.
5. Запустите `twitchbot.exe` без тестовых аргументов.

OAuth-токены вручную указывать не нужно. При первом запуске бот использует Device Code Flow, после чего сохраняет токены локально в `data/twitch_tokens.dat`.

## Конфигурация

Основные параметры:

```ini
[twitch]
client_id=
client_secret=
access_token=
refresh_token=
broadcaster_login=
broadcaster_id=
bot_login=
bot_user_id=

[telegram]
bot_token=
chat_id=
channel_url=https://t.me/your_channel

[bot]
command_prefix=!
```

`access_token`, `refresh_token`, `broadcaster_id`, `bot_login` и `bot_user_id` обычно не требуется заполнять вручную.

## Сборка

Проект рассчитан на Windows и проверялся с **Qt 5.12.12 + MinGW 7.3**.

В Qt Creator:

```text
Run qmake
Clean Project
Rebuild Project
Run
```

Проект использует WinHTTP и Windows Cryptography API:

- `winhttp` — HTTP-запросы и EventSub WebSocket;
- `crypt32` — защита OAuth-токенов через DPAPI.

Старые заголовки MinGW 7.3 не содержат современных объявлений WinHTTP WebSocket API, поэтому необходимые WebSocket-функции загружаются из системной `winhttp.dll` во время выполнения.

## Тестовые режимы

```text
--test-stream
--dry-run
--test-chat
--test-twitch-chat
--test-eventsub-chat
--test-twitch-command
--help
```

`--test-twitch-command` — диагностический режим. Он подключается к настоящему Twitch-чату, ждёт одну команду, отвечает на неё и завершает работу.

Для обычной постоянной работы тестовые аргументы не нужны.

## Структура проекта

```text
include/        заголовочные файлы
src/            исходный код
third_party/    сторонние библиотеки
docs/           документация проекта
data/           локальные данные и OAuth-токены
logs/           журнал работы бота
```

Основные модули:

- `twitch_auth` / `twitch_refresh` — OAuth;
- `twitch_api` — работа с Twitch Helix;
- `twitch_stream` — состояние трансляции;
- `twitch_chat` — отправка сообщений в чат;
- `twitch_eventsub` — разбор событий EventSub;
- `twitch_eventsub_ws` — WebSocket-соединение EventSub;
- `commands` — параметры запуска и Twitch-команды;
- `telegram_api` — уведомления Telegram;
- `token_store` — защищённое локальное хранение токенов.

## Безопасность

`config.ini`, `data/`, `logs/` и файлы сборки исключены через `.gitignore`.

Не публикуйте:

- Twitch OAuth-токены;
- Twitch Client Secret;
- Telegram bot token;
- `data/twitch_tokens.dat`.

`config.example.ini` предназначен только для примера и не должен содержать реальные секреты.

## Дальнейшее развитие

После версии **0.6.0.0** проект переходит от построения основной инфраструктуры к небольшим функциональным обновлениям.

В планах:

- дополнительные команды;
- кулдауны и защита от спама;
- профили зрителей;
- внутренняя валюта «апельсины»;
- мини-игры и взаимодействия между зрителями;
- статистика и таблица лидеров;
- дальнейшее улучшение восстановления EventSub-соединения.
