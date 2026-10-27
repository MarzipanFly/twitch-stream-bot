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
    src/category_sync.c \
    src/game_detector.c \
    src/obs_director.c \
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
    src/music_worker.c \
    src/ai_chat.c \
    src/ai_text.c \
    src/ai_retrieval.c \
    src/ai_game_scope.c \
    src/ai_semantic.c \
    src/chat_promo.c \
    src/stream_report.c \
    src/dashboard_server.c \
    src/dashboard_obs.c \
    src/dashboard_settings.c \
    src/dashboard_stats.c \
    src/ai_knowledge.c \
    src/ai_tactics.c \
    src/achievements.c \
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
    src/viewer_challenges.c \
    src/viewer_challenge_chat.c \
    src/viewer_rank.c \
    third_party/cjson/cJSON.c

HEADERS += \
    include/app.h \
    include/category_sync.h \
    include/game_detector.h \
    include/obs_director.h \
    include/bot_result.h \
    include/chat_event.h \
    include/command_cooldown.h \
    include/commands.h \
    include/config.h \
    include/http_client.h \
    include/logger.h \
    include/music_queue.h \
    include/music_audio.h \
    include/music_worker.h \
    include/ai_chat.h \
    include/ai_text.h \
    include/ai_retrieval.h \
    include/ai_game_scope.h \
    include/ai_semantic.h \
    include/chat_promo.h \
    include/stream_report.h \
    include/dashboard.h \
    include/dashboard_obs.h \
    include/dashboard_settings.h \
    include/dashboard_stats.h \
    include/ai_knowledge.h \
    include/ai_tactics.h \
    include/achievements.h \
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
    include/viewer_challenges.h \
    include/viewer_challenge_chat.h \
    include/viewer_rank.h \
    third_party/cjson/cJSON.h

win32-g++ {
    LIBS += -lws2_32
    LIBS += -lcrypt32
    LIBS += -lwinhttp
    LIBS += -lm
    LIBS += -lcrypt32
    LIBS += -ladvapi32
}

win32-msvc* {
    LIBS += Ws2_32.lib
    LIBS += Crypt32.lib
    LIBS += Winhttp.lib
    LIBS += Crypt32.lib
    LIBS += Advapi32.lib
}

win32 {
    # Keep the reviewed local AI knowledge available beside the built executable.
    QMAKE_POST_LINK += $escape_expand(\\n\\t)if not exist \"$OUT_PWD\\knowledge\" mkdir \"$OUT_PWD\\knowledge\"
    QMAKE_POST_LINK += $escape_expand(\\n\\t)copy /Y \"$PWD\\knowledge\\facts.tsv\" \"$OUT_PWD\\knowledge\\facts.tsv\"
}

DISTFILES += \
    knowledge/facts.tsv \
    config.ini \
    dashboard/index.html \
    dashboard/app.js \
    dashboard/settings.html \
    dashboard/settings.js

VERSION = 0.8.0.0
