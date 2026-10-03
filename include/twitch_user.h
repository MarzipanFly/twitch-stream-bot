#ifndef TWITCH_USER_H
#define TWITCH_USER_H

#define TWITCH_USER_ID_SIZE 64
#define TWITCH_LOGIN_SIZE 64
#define TWITCH_DISPLAY_NAME_SIZE 128
#define TWITCH_BROADCASTER_TYPE_SIZE 32
#define TWITCH_DESCRIPTION_SIZE 512
#define TWITCH_URL_SIZE 512

typedef struct
{
    char id[TWITCH_USER_ID_SIZE];

    char login[TWITCH_LOGIN_SIZE];
    char display_name[TWITCH_DISPLAY_NAME_SIZE];

    char broadcaster_type[
        TWITCH_BROADCASTER_TYPE_SIZE
    ];

    char description[
        TWITCH_DESCRIPTION_SIZE
    ];

    char profile_image_url[
        TWITCH_URL_SIZE
    ];

} TwitchUser;

#endif
