#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <stddef.h>
#include <windows.h>

#include "bot_result.h"

typedef struct
{
	unsigned long status_code;

	char *body;
	size_t body_size;
} HttpResponse;

/*
 * Выполняет HTTPS Get-запрос
 *
 * host:
 *		L"example.com"
 *
 * path:
 *		L"/"
 *
 * additional_headers:
 *	  Дополнительные HTTP-заголовки.
 *	  Можно передать NULL.
 */

BotResult http_get(
		const wchar_t *host,
		const wchar_t *path,
		const wchar_t *additional_headers,
		HttpResponse  *response
);

BotResult http_post(
		const wchar_t *host,
		const wchar_t *path,
		const wchar_t *additional_headers,
		const char *body,
		HttpResponse *response
);

/*
 * Освобождает память,
 * выделенную для HTTP-овтета
 */
void http_response_free(
		HttpResponse *response
);

#endif // HTTPCLIENT_H
