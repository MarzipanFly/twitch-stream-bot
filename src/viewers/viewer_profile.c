#include "viewer_profile.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
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
        "TWITCHBOT_VIEWERS_V3\n"
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
                "%s|%s|%s|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld\n",
                profile.user_id,
                profile.login,
                profile.display_name,
                profile.balance,
                profile.last_daily,
                profile.created_at,
                profile.daily_count,
                profile.coin_wins,
                profile.coin_losses,
                profile.slot_jackpots,
                profile.slot_pairs,
                profile.slot_losses,
                profile.duel_wins,
                profile.duel_losses
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
    long long last_daily;
    long long created_at;
    long long daily_count;
    long long coin_wins;
    long long coin_losses;
    long long slot_jackpots;
    long long slot_pairs;
    long long slot_losses;
    long long duel_wins;
    long long duel_losses;
    int file_version;


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
            "TWITCHBOT_VIEWERS_V3",
            20
        ) == 0)
    {
        file_version = 3;
    }
    else if (
        strncmp(
            line,
            "TWITCHBOT_VIEWERS_V2",
            20
        ) == 0)
    {
        file_version = 2;
    }
    else if (
        strncmp(
            line,
            "TWITCHBOT_VIEWERS_V1",
            20
        ) == 0)
    {
        file_version = 1;
    }
    else
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
        last_daily = 0;
        created_at = 0;
        daily_count = 0;
        coin_wins = 0;
        coin_losses = 0;
        slot_jackpots = 0;
        slot_pairs = 0;
        slot_losses = 0;
        duel_wins = 0;
        duel_losses = 0;


        if (file_version == 3)
        {
            if (
                sscanf(
                    line,
                    "%63[^|]|%63[^|]|%127[^|]|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld|%lld",
                    user_id,
                    login,
                    display_name,
                    &balance,
                    &last_daily,
                    &created_at,
                    &daily_count,
                    &coin_wins,
                    &coin_losses,
                    &slot_jackpots,
                    &slot_pairs,
                    &slot_losses,
                    &duel_wins,
                    &duel_losses
                ) != 14)
            {
                continue;
            }
        }
        else if (file_version == 2)
        {
            if (
                sscanf(
                    line,
                    "%63[^|]|%63[^|]|%127[^|]|%lld|%lld",
                    user_id,
                    login,
                    display_name,
                    &balance,
                    &last_daily
                ) != 5)
            {
                continue;
            }
        }
        else
        {
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

            last_daily = 0;
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

        g_profiles[g_profile_count].last_daily = last_daily;
        g_profiles[g_profile_count].created_at = created_at;
        g_profiles[g_profile_count].daily_count = daily_count;
        g_profiles[g_profile_count].coin_wins = coin_wins;
        g_profiles[g_profile_count].coin_losses = coin_losses;
        g_profiles[g_profile_count].slot_jackpots = slot_jackpots;
        g_profiles[g_profile_count].slot_pairs = slot_pairs;
        g_profiles[g_profile_count].slot_losses = slot_losses;
        g_profiles[g_profile_count].duel_wins = duel_wins;
        g_profiles[g_profile_count].duel_losses = duel_losses;

        /*
         * Старые V1/V2 не содержали дату создания профиля.
         * При миграции фиксируем момент первого запуска V3.
         */
        if (g_profiles[g_profile_count].created_at == 0)
        {
            g_profiles[g_profile_count].created_at =
                (long long)time(NULL);
        }


        ++g_profile_count;
    }


    fclose(file);


    /*
     * V1/V2 автоматически переводим в V3 сразу после загрузки,
     * чтобы дата создания профиля и новые счётчики пережили
     * следующий перезапуск даже без игровой команды.
     */
    if (
        file_version < 3 &&
        !viewer_profile_save())
    {
        return 0;
    }


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

    profile->created_at =
        (long long)time(NULL);


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


int viewer_profile_build_top(
    size_t limit,
    char *buffer,
    size_t buffer_size
)
{
    size_t indices[VIEWER_PROFILE_MAX_USERS];
    size_t count;
    size_t i;
    size_t j;
    size_t used;

    if (
        buffer == NULL ||
        buffer_size == 0 ||
        limit == 0)
    {
        return 0;
    }

    buffer[0] = '\0';

    count = g_profile_count;

    if (count == 0)
    {
        snprintf(
            buffer,
            buffer_size,
            "Рейтинг пока пуст."
        );

        return 1;
    }

    for (i = 0; i < count; ++i)
    {
        indices[i] = i;
    }

    /*
     * Профилей максимум 1024, а рейтинг запрашивается редко,
     * поэтому для текущего масштаба достаточно простой сортировки
     * индексов без изменения порядка самих профилей.
     */
    for (i = 0; i < count; ++i)
    {
        size_t best = i;

        for (j = i + 1; j < count; ++j)
        {
            ViewerProfile *candidate =
                &g_profiles[indices[j]];

            ViewerProfile *current =
                &g_profiles[indices[best]];

            if (
                candidate->balance > current->balance ||
                (
                    candidate->balance == current->balance &&
                    strcmp(
                        candidate->display_name,
                        current->display_name
                    ) < 0
                ))
            {
                best = j;
            }
        }

        if (best != i)
        {
            size_t temporary = indices[i];

            indices[i] = indices[best];
            indices[best] = temporary;
        }
    }

    if (limit > count)
    {
        limit = count;
    }

    used = (size_t)snprintf(
        buffer,
        buffer_size,
        "Топ апельсинов: "
    );

    if (used >= buffer_size)
    {
        buffer[buffer_size - 1] = '\0';
        return 1;
    }

    for (i = 0; i < limit; ++i)
    {
        ViewerProfile *profile =
            &g_profiles[indices[i]];

        int written =
            snprintf(
                buffer + used,
                buffer_size - used,
                "%s%u. %s — %lld",
                i == 0 ? "" : " | ",
                (unsigned int)(i + 1),
                profile->display_name[0] != '\0'
                    ? profile->display_name
                    : profile->login,
                profile->balance
            );

        if (written < 0)
        {
            return 0;
        }

        if ((size_t)written >= buffer_size - used)
        {
            buffer[buffer_size - 1] = '\0';
            break;
        }

        used += (size_t)written;
    }

    return 1;
}
