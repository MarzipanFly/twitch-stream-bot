# Архитектура Twitch Stream Bot

## Общий поток

```text
main()
  |
  v
app_run()
  |
  +--> platform_create_directory()
  +--> logger_init()
  +--> config_load() / config_validate()
  +--> Internet test via http_get()
  |
  +--> ensure_twitch_auth()
  |      |
  |      +--> token_store_load()
  |      +--> twitch_auth_validate_token()
  |      +--> twitch_refresh_access_token()  [если access token недействителен]
  |      +--> run_device_authorization()     [если сохранённой сессии нет]
  |      +--> token_store_save()
  |
  +--> twitch_get_user()
  +--> twitch_parse_user_response()
  |
  +--> monitor_stream()
         |
         +--> twitch_get_stream()
         +--> twitch_parse_stream_response()
         +--> sleep 30 s
         +--> повтор
```

## Слои

### Application layer

Файлы: `src/main.c`, `src/app.c`, `include/app.h`.

`main()` передаёт управление `app_run()`. В `app.c` собран жизненный цикл программы: инициализация, OAuth, получение broadcaster ID, запуск и завершение мониторинга.

### Configuration and diagnostics

Файлы: `config.c`, `logger.c`, `bot_result.c`.

Этот слой отвечает за INI-конфигурацию, единые коды результата и журналирование. Секретные значения не должны выводиться в лог.

### Platform layer

Файл: `platform_win.c`.

Windows-зависимые операции, которые не относятся к Twitch API. Сейчас основная задача — создание служебных директорий.

### HTTP transport

Файл: `http_client.c`.

Обёртка над WinHTTP. Предоставляет единые `http_get()`, `http_post()` и `http_response_free()`. Twitch-модули работают через этот слой вместо прямого управления WinHTTP handles.

### Twitch OAuth

Файлы: `twitch_auth.c`, `twitch_refresh.c`, `token_store.c`.

`twitch_auth` реализует Device Code Flow и validation. `twitch_refresh` получает новую пару токенов. `token_store` шифрует токены Windows DPAPI и хранит их локально.

### Twitch Helix API

Файлы: `twitch_api.c`, `twitch_stream.c`.

`twitch_api` получает данные пользователя. `twitch_stream` проверяет состояние трансляции и преобразует JSON в `TwitchStream`.

## Модель состояния трансляции

Монитор хранит только предыдущее логическое состояние:

```text
previous_live_state
current_stream.is_live
```

Переходы:

| Было | Стало | Событие |
| --- | --- | --- |
| OFFLINE | OFFLINE | без события |
| OFFLINE | ONLINE | `STREAM STARTED` |
| ONLINE | ONLINE | без события |
| ONLINE | OFFLINE | `STREAM ENDED` |

Первое состояние после запуска используется только как baseline. Поэтому запуск бота посреди эфира не имитирует новое начало стрима.

## Владение памятью

`HttpResponse.body` выделяется HTTP-клиентом динамически. После обработки вызывающий код обязан вызвать `http_response_free()`.

Большинство остальных объектов — структуры фиксированного размера на стеке.

## Обработка ошибок

Модули возвращают `BotResult`. Сетевые ошибки в основном цикле мониторинга не должны мгновенно завершать приложение: они логируются, после чего следующая итерация может повторить запрос.

Ошибка авторизации обрабатывается отдельно: приложение пытается восстановить OAuth-сессию и повторить Twitch-запрос.

## Завершение

Windows console control handler не завершает процесс внутри callback. Он выставляет атомарный флаг `g_stop_requested`. Основной цикл замечает его, выходит из `monitor_stream()`, закрывает logger и завершает приложение штатно.
