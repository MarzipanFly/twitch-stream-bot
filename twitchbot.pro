TEMPLATE = app
TARGET = twitchbot
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/third_party/cjson

SOURCES += \
    src/app.c \
    src/bot_result.c \
    src/chat_event.c \
    src/command_cooldown.c \
    src/commands.c \
    src/config.c \
    src/http_client.c \
    src/logger.c \
    src/main.c \
    src/music_queue.c \
    src/music_audio.c \
    src/music_library.c \
    src/youtube_metadata.c \
    src/obs_websocket.c \
    src/platform_win.c \
    src/telegram_api.c \
    src/token_store.c \
    src/twitch_api.c \
    src/twitch_auth.c \
    src/twitch_chat.c \
    src/twitch_eventsub.c \
    src/twitch_eventsub_ws.c \
    src/twitch_refresh.c \
    src/twitch_stream.c \
    src/viewer_profile.c \
    src/viewer_duel.c \
    src/viewer_rank.c \
    third_party/cjson/cJSON.c

HEADERS += \
    include/app.h \
    include/bot_result.h \
    include/chat_event.h \
    include/command_cooldown.h \
    include/commands.h \
    include/config.h \
    include/http_client.h \
    include/logger.h \
    include/music_queue.h \
    include/music_audio.h \
    include/music_library.h \
    include/youtube_metadata.h \
    include/obs_websocket.h \
    include/platform.h \
    include/telegram_api.h \
    include/token_store.h \
    include/twitch_api.h \
    include/twitch_auth.h \
    include/twitch_chat.h \
    include/twitch_eventsub.h \
    include/twitch_eventsub_ws.h \
    include/twitch_refresh.h \
    include/twitch_stream.h \
    include/twitch_user.h \
    include/viewer_profile.h \
    include/viewer_duel.h \
    include/viewer_rank.h \
    third_party/cjson/cJSON.h

win32-g++ {
    LIBS += -lwinhttp
    LIBS += -lcrypt32
    LIBS += -ladvapi32
}

win32-msvc* {
    LIBS += Winhttp.lib
    LIBS += Crypt32.lib
    LIBS += Advapi32.lib
}

DISTFILES += \
    config.ini

VERSION = 0.8.0.0
