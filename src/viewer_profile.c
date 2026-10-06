#include "viewer_profile.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


#define VIEWER_PROFILE_DATA_DIR "data"
#define VIEWER_PROFILE_FILE "data/viewers.dat"
#define VIEWER_PROFILE_TEMP_FILE "data/viewers.tmp"

#define VIEWER_PROFILE_LINE_SIZE 512


static ViewerProfile g_profiles[
    VIEWER_PROFILE_MAX_USERS
];

static size_t g_profile_count = 0;


/*
 * Безопасное копирование строки.
 */
static void copy_string(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (
        destination == NULL ||
        destination_size == 0)
    {
        return;
    }


    if (source == NULL)
    {
        destination[0] = '\0';
        return;
    }


    snprintf(
        destination,
        destination_size,
        "%s",
        source
    );
}


/*
 * Убирает символы, которые используются
 * как разделители нашего файлового формата.
 */
static void sanitize_string(
    char *text
)
{
    if (text == NULL)
    {
        return;
    }


    while (*text != '\0')
    {
        if (
            *text == '|' ||
            *text == '\r' ||
            *text == '\n')
        {
            *text = '_';
        }


        ++text;
    }
}


/*
 * Создаёт папку data, если её ещё нет.
 */
static int ensure_data_directory(void)
{
    DWORD attributes;


    attributes =
        GetFileAttributesA(
            VIEWER_PROFILE_DATA_DIR
        );


    if (
        attributes !=
        INVALID_FILE_ATTRIBUTES)
    {
        return
            (attributes &
             FILE_ATTRIBUTE_DIRECTORY) != 0;
    }


    if (
        CreateDirectoryA(
            VIEWER_PROFILE_DATA_DIR,
            NULL))
    {
        return 1;
    }


    return
        GetLastError() ==
        ERROR_ALREADY_EXISTS;
}


ViewerProfile *viewer_profile_find(
    const char *user_id
)
{
    size_t i;


    if (
        user_id == NULL ||
        user_id[0] == '\0')
    {
        return NULL;
    }


    for (
        i = 0;
        i < g_profile_count;
        ++i)
    {
        if (
            strcmp(
                g_profiles[i].user_id,
                user_id
            ) == 0)
        {
            return
                &g_profiles[i];
        }
    }


    return NULL;
}


int viewer_profile_save(void)
{
    FILE *file;
    size_t i;


    if (!ensure_data_directory())
    {
        return 0;
    }


    file =
        fopen(
            VIEWER_PROFILE_TEMP_FILE,
            "wb"
        );


    if (file == NULL)
    {
        return 0;
    }


    /*
     * Первая строка — версия формата.
     * При будущих изменениях структуры это позволит
     * сделать миграцию данных.
     */
    fprintf(
        file,
        "TWITCHBOT_VIEWERS_V1\n"
    );


    for (
        i = 0;
        i < g_profile_count;
        ++i)
    {
        ViewerProfile profile =
            g_profiles[i];


        sanitize_string(
            profile.user_id
        );

        sanitize_string(
            profile.login
        );

        sanitize_string(
            profile.display_name
        );


        if (
            fprintf(
                file,
                "%s|%s|%s|%lld\n",
                profile.user_id,
                profile.login,
                profile.display_name,
                profile.balance
            ) < 0)
        {
            fclose(file);
            remove(
                VIEWER_PROFILE_TEMP_FILE
            );

            return 0;
        }
    }


    if (fclose(file) != 0)
    {
        remove(
            VIEWER_PROFILE_TEMP_FILE
        );

        return 0;
    }


    /*
     * Сначала пишем временный файл.
     * Только после успешной записи заменяем основной.
     *
     * Это безопаснее прямой перезаписи viewers.dat:
     * при аварии во время сохранения старый файл
     * с профилями не будет наполовину перезаписан.
     */
    if (
        !MoveFileExA(
            VIEWER_PROFILE_TEMP_FILE,
            VIEWER_PROFILE_FILE,
            MOVEFILE_REPLACE_EXISTING |
            MOVEFILE_WRITE_THROUGH))
    {
        remove(
            VIEWER_PROFILE_TEMP_FILE
        );

        return 0;
    }


    return 1;
}


int viewer_profile_init(void)
{
    FILE *file;

    char line[
        VIEWER_PROFILE_LINE_SIZE
    ];

    char user_id[
        VIEWER_PROFILE_ID_SIZE
    ];

    char login[
        VIEWER_PROFILE_LOGIN_SIZE
    ];

    char display_name[
        VIEWER_PROFILE_NAME_SIZE
    ];

    long long balance;


    memset(
        g_profiles,
        0,
        sizeof(g_profiles)
    );


    g_profile_count = 0;


    if (!ensure_data_directory())
    {
        return 0;
    }


    file =
        fopen(
            VIEWER_PROFILE_FILE,
            "rb"
        );


    /*
     * Первый запуск.
     * Базы зрителей ещё просто нет.
     */
    if (file == NULL)
    {
        return 1;
    }


    if (
        fgets(
            line,
            sizeof(line),
            file
        ) == NULL)
    {
        fclose(file);
        return 0;
    }


    if (
        strncmp(
            line,
            "TWITCHBOT_VIEWERS_V1",
            20
        ) != 0)
    {
        fclose(file);
        return 0;
    }


    while (
        fgets(
            line,
            sizeof(line),
            file
        ) != NULL)
    {
        if (
            g_profile_count >=
            VIEWER_PROFILE_MAX_USERS)
        {
            break;
        }


        user_id[0] = '\0';
        login[0] = '\0';
        display_name[0] = '\0';
        balance = 0;


        if (
            sscanf(
                line,
                "%63[^|]|%63[^|]|%127[^|]|%lld",
                user_id,
                login,
                display_name,
                &balance
            ) != 4)
        {
            continue;
        }


        copy_string(
            g_profiles[
                g_profile_count
            ].user_id,
            sizeof(
                g_profiles[
                    g_profile_count
                ].user_id
            ),
            user_id
        );


        copy_string(
            g_profiles[
                g_profile_count
            ].login,
            sizeof(
                g_profiles[
                    g_profile_count
                ].login
            ),
            login
        );


        copy_string(
            g_profiles[
                g_profile_count
            ].display_name,
            sizeof(
                g_profiles[
                    g_profile_count
                ].display_name
            ),
            display_name
        );


        g_profiles[
            g_profile_count
        ].balance =
            balance;


        ++g_profile_count;
    }


    fclose(file);


    return 1;
}


ViewerProfile *viewer_profile_get_or_create(
    const char *user_id,
    const char *login,
    const char *display_name
)
{
    ViewerProfile *profile;


    if (
        user_id == NULL ||
        user_id[0] == '\0')
    {
        return NULL;
    }


    profile =
        viewer_profile_find(
            user_id
        );


    /*
     * Пользователь уже существует.
     *
     * Twitch login/display name могли измениться,
     * поэтому обновляем их при каждом обращении.
     */
    if (profile != NULL)
    {
        if (
            login != NULL &&
            login[0] != '\0')
        {
            copy_string(
                profile->login,
                sizeof(profile->login),
                login
            );
        }


        if (
            display_name != NULL &&
            display_name[0] != '\0')
        {
            copy_string(
                profile->display_name,
                sizeof(profile->display_name),
                display_name
            );
        }


        return profile;
    }


    if (
        g_profile_count >=
        VIEWER_PROFILE_MAX_USERS)
    {
        return NULL;
    }


    profile =
        &g_profiles[
            g_profile_count
        ];


    memset(
        profile,
        0,
        sizeof(*profile)
    );


    copy_string(
        profile->user_id,
        sizeof(profile->user_id),
        user_id
    );


    copy_string(
        profile->login,
        sizeof(profile->login),
        login
    );


    copy_string(
        profile->display_name,
        sizeof(profile->display_name),
        display_name
    );


    profile->balance =
        VIEWER_PROFILE_START_BALANCE;


    ++g_profile_count;


    /*
     * Новый профиль сразу сохраняем.
     */
    if (!viewer_profile_save())
    {
        /*
         * Если запись не удалась,
         * откатываем создание профиля.
         */
        --g_profile_count;


        memset(
            profile,
            0,
            sizeof(*profile)
        );


        return NULL;
    }


    return profile;
}


size_t viewer_profile_count(void)
{
    return g_profile_count;
}
