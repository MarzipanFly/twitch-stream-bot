#ifndef VIEWER_PROFILE_H
#define VIEWER_PROFILE_H

#include <stddef.h>


#define VIEWER_PROFILE_MAX_USERS 1024

#define VIEWER_PROFILE_ID_SIZE 64
#define VIEWER_PROFILE_LOGIN_SIZE 64
#define VIEWER_PROFILE_NAME_SIZE 128

#define VIEWER_PROFILE_START_BALANCE 100


typedef struct
{
    char user_id[VIEWER_PROFILE_ID_SIZE];
    char login[VIEWER_PROFILE_LOGIN_SIZE];
    char display_name[VIEWER_PROFILE_NAME_SIZE];

    long long balance;

    /* Unix-время последнего получения ежедневной награды. */
    long long last_daily;

    /* Долгосрочная статистика профиля. */
    long long created_at;
    long long daily_count;
    long long coin_wins;
    long long coin_losses;
    long long slot_jackpots;
    long long slot_pairs;
    long long slot_losses;
    long long duel_wins;
    long long duel_losses;

} ViewerProfile;


/*
 * Загружает базу профилей из data/viewers.dat.
 *
 * Если файла ещё нет, создаётся пустая база.
 *
 * Возвращает:
 * 1 - успешно;
 * 0 - ошибка.
 */
int viewer_profile_init(void);


/*
 * Сохраняет все профили на диск.
 */
int viewer_profile_save(void);


/*
 * Ищет профиль по Twitch User ID.
 *
 * Возвращает NULL, если профиль не найден.
 */
ViewerProfile *viewer_profile_find(
    const char *user_id
);


/*
 * Возвращает существующий профиль либо
 * создаёт новый со стартовым балансом.
 */
ViewerProfile *viewer_profile_get_or_create(
    const char *user_id,
    const char *login,
    const char *display_name
);


/*
 * Количество загруженных профилей.
 */
size_t viewer_profile_count(void);


/*
 * Формирует текстовый рейтинг зрителей по балансу.
 *
 * limit задаёт максимальное количество мест в рейтинге.
 *
 * Возвращает:
 * 1 - рейтинг сформирован;
 * 0 - ошибка.
 */
int viewer_profile_build_top(
    size_t limit,
    char *buffer,
    size_t buffer_size
);


#endif
