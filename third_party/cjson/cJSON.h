/*
 * Minimal cJSON-compatible API subset used by TwitchBot.
 *
 * This recovery copy implements only the parser/query functions that the
 * project currently calls. It is NOT the complete upstream cJSON library.
 * The public names are intentionally compatible so the application modules
 * do not need to change.
 */
#ifndef CJSON_H
#define CJSON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

#define cJSON_Invalid (0)
#define cJSON_False   (1 << 0)
#define cJSON_True    (1 << 1)
#define cJSON_NULL    (1 << 2)
#define cJSON_Number  (1 << 3)
#define cJSON_String  (1 << 4)
#define cJSON_Array   (1 << 5)
#define cJSON_Object  (1 << 6)

typedef struct cJSON
{
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;

    int type;

    char *valuestring;
    int valueint;
    double valuedouble;

    char *string;

} cJSON;

cJSON *cJSON_Parse(const char *value);
void cJSON_Delete(cJSON *item);

cJSON *cJSON_GetObjectItemCaseSensitive(
    const cJSON *object,
    const char *string
);

int cJSON_GetArraySize(
    const cJSON *array
);

cJSON *cJSON_GetArrayItem(
    const cJSON *array,
    int index
);

int cJSON_IsString(
    const cJSON *item
);

int cJSON_IsNumber(
    const cJSON *item
);

int cJSON_IsArray(
    const cJSON *item
);

int cJSON_IsObject(
    const cJSON *item
);

#ifdef __cplusplus
}
#endif

#endif
