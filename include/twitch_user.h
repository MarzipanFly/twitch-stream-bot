#ifndef TWITCH_USER_H
#define TWITCH_USER_H

#define TWITCH_ID_USER 64
#define TWITCH_LOGIN_SIZE 64
#define TWITCH_DISPLAY_NAME 128
#define TWITCH_BROADCASTER_TYPE_SIZE 32
#define TWITCH_DESCRIPTION_SIZE 512
#define TWITCH_URL_SIZE 512

typedef struct {
	char id[TWITCH_ID_USER];
	char login[TWITCH_LOGIN_SIZE];
	char display_name[TWITCH_DISPLAY_NAME];
	char broadcaster_type[TWITCH_BROADCASTER_TYPE_SIZE];
	char description[TWITCH_DESCRIPTION_SIZE];
	char profile_image_url[TWITCH_URL_SIZE];
} TwitchUser;

#endif // TWITCH_USER_H
