#ifndef TWITCH_AUTH_H
#define TWITCH_AUTH_H

#include "bot_result.h"
#include "config.h"


#define TWITCH_DEVICE_CODE_SIZE 512
#define TWITCH_USER_CODE_SIZE 64
#define TWITCH_URI_SIZE 512

#define TWITCH_ACCESS_TOKEN_SIZE 1024
#define TWITCH_REFRESH_TOKEN_SIZE 1024

#define TWITCH_AUTH_LOGIN_SIZE 128
#define TWITCH_AUTH_USER_ID_SIZE 64
#define TWITCH_AUTH_CLIENT_ID_SIZE 256


typedef struct
{
    char device_code[TWITCH_DEVICE_CODE_SIZE];
    char user_code[TWITCH_USER_CODE_SIZE];
    char verification_uri[TWITCH_URI_SIZE];

    int expires_in;
    int interval;

} TwitchDeviceCode;


typedef struct
{
    char access_token[TWITCH_ACCESS_TOKEN_SIZE];
    char refresh_token[TWITCH_REFRESH_TOKEN_SIZE];

    int expires_in;

} TwitchAuthToken;


typedef struct
{
    char client_id[TWITCH_AUTH_CLIENT_ID_SIZE];
    char login[TWITCH_AUTH_LOGIN_SIZE];
    char user_id[TWITCH_AUTH_USER_ID_SIZE];

    int expires_in;

} TwitchTokenValidation;


BotResult twitch_auth_request_device_code(
    const TwitchConfig *config,
    TwitchDeviceCode *device
);


BotResult twitch_auth_poll_token(
    const TwitchConfig *config,
    const TwitchDeviceCode *device,
    TwitchAuthToken *token
);


BotResult twitch_auth_validate_token(
    const char *access_token,
    TwitchTokenValidation *validation
);


#endif
