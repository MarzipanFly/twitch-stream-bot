#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>
#include <wchar.h>

#include "bot_result.h"

typedef struct
{
    unsigned long status_code;

    char *body;
    size_t body_size;

} HttpResponse;

BotResult http_get(
    const wchar_t *host,
    const wchar_t *path,
    const wchar_t *additional_headers,
    HttpResponse *response
);

BotResult http_post(
    const wchar_t *host,
    const wchar_t *path,
    const wchar_t *additional_headers,
    const char *body,
    HttpResponse *response
);

void http_response_free(
    HttpResponse *response
);

#endif
