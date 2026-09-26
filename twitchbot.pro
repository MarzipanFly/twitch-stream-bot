TEMPLATE = app
TARGET = twitchbot

CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += \
	$$PWD/include \
	$$PWD/third_party/cjson

SOURCES += \
    src/bot_result.c \
    src/config.c \
	src/http_client.c \
    src/logger.c \
    src/main.c \
    src/app.c \
    src/platform_win.c \
	src/twitch_api.c \
    src/twitch_auth.c \
	third_party/cjson/cJSON.c

HEADERS += \
    include/app.h \
    include/bot_result.h \
    include/config.h \
    include/http_client.h \
    include/logger.h \
    include/platform.h \
    include/twitch_api.h \
    include/twitch_auth.h \
	include/twitch_user.h \
	third_party/cjson/cJSON.h

win32-g++ {
	LIBS += -lwinhttp
}

win32-msvc* {
    LIBS += Winhttp.lib
}

DISTFILES += \
    config.ini
