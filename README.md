# Twitch Stream Bot

Windows-консольный Twitch-бот на C, собираемый в Qt Creator через qmake.

Текущая версия: **0.6.0.0**.

## Что уже работает

- Twitch Device Code OAuth;
- безопасное хранение `access_token` и `refresh_token` через Windows DPAPI;
- автоматическая загрузка, проверка и обновление OAuth-сессии;
- получение Twitch-пользователя через Helix;
- мониторинг статуса стрима через `GET /helix/streams`;
- обнаружение `OFFLINE -> ONLINE` и `ONLINE -> OFFLINE`;
- Telegram-уведомление при старте стрима;
- отправка сообщений в Twitch-чат через Helix;
- получение сообщений Twitch-чата через EventSub WebSocket;
- команды `!тг`, `!команды`, `!монетка`, `!кости`, `!шар`, `!слот`;
- постоянная обработка Twitch-команд в обычном режиме запуска;
- автоматическое повторное подключение EventSub после потери соединения;
- UTF-8 в Windows-консоли;
- корректная остановка по `Ctrl+C`;
- логирование в `logs/bot.log`.

## Обычный режим

Запускайте `twitchbot.exe` без тестовых аргументов.

После инициализации бот одновременно держит подключение к Twitch-чату и продолжает проверять состояние стрима. EventSub присылает keepalive-сообщения, поэтому основной цикл регулярно выполняет и периодические задачи.

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

`--test-twitch-command` оставлен как диагностический режим: он ждёт одну настоящую команду Twitch, отвечает и завершается. Для постоянной работы он больше не нужен.

## Настройка

Скопируйте `config.example.ini` в `config.ini` и заполните настройки.

Client ID берётся в Twitch Developer Console: Applications -> Manage -> Client ID. Подсказка также находится прямо в `config.example.ini`.

`access_token` и `refresh_token` вручную заполнять не нужно: бот получает их через Device Code Flow и сохраняет в `data/twitch_tokens.dat` с помощью DPAPI.

## Сборка

В Qt Creator:

```text
Run qmake
Clean Project
Rebuild Project
Run
```

Проект рассчитан в том числе на Qt 5.12.12 + MinGW 7.3. Так как старый MinGW не содержит современных объявлений WinHTTP WebSocket API, необходимые WebSocket-функции загружаются из системной `winhttp.dll` во время выполнения.

## Секреты

`config.ini`, `data/`, `logs/` исключены через `.gitignore`.

Не публикуйте Twitch OAuth-токены, Telegram bot token, Client Secret и `data/twitch_tokens.dat`.

## Следующий этап

После версии 0.6.0.0 основной фундамент готов. Дальше проект можно развивать небольшими обновлениями: мини-игровая система, кулдауны, профили зрителей, внутренняя валюта, новые команды и улучшение EventSub reconnect.
