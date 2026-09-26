#include "http_client.h"
#include "logger.h"

#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>
#include <string.h>


/*
 * Выводит код последней ошибки WinHTTP / WinAPI.
 */
static void log_winhttp_error(const char *operation)
{
    DWORD error_code;

    error_code = GetLastError();

    log_error(
        "%s failed. Windows error code: %lu",
        operation,
        (unsigned long)error_code
    );
}


/*
 * Читает тело HTTP-ответа.
 *
 * Например сервер вернул:
 *
 * {
 *     "data": [...]
 * }
 *
 * Эта функция постепенно считывает данные
 * и помещает их в response->body.
 */
static BotResult read_response_body(
    HINTERNET request,
    HttpResponse *response
)
{
    DWORD available;
    DWORD bytes_read;

    char *new_buffer;

    size_t new_size;


    while (1)
    {
        available = 0;


        /*
         * Узнаём, сколько байт сейчас
         * доступно для чтения.
         */
        if (!WinHttpQueryDataAvailable(
                request,
                &available))
        {
            log_winhttp_error(
                "WinHttpQueryDataAvailable"
            );

            return BOT_ERR_NETWORK;
        }


        /*
         * Если доступно 0 байт,
         * ответ закончился.
         */
        if (available == 0)
        {
            break;
        }


        /*
         * Увеличиваем наш буфер.
         *
         * +1 нужен для '\0',
         * чтобы body был обычной C-строкой.
         */
        new_size =
            response->body_size +
            (size_t)available +
            1;


        new_buffer = realloc(
            response->body,
            new_size
        );


        if (new_buffer == NULL)
        {
            log_error(
                "Failed to allocate memory for HTTP response"
            );

            return BOT_ERR_UNKNOWN;
        }


        response->body = new_buffer;


        bytes_read = 0;


        /*
         * Читаем очередную часть ответа.
         */
        if (!WinHttpReadData(
                request,
                response->body +
                    response->body_size,
                available,
                &bytes_read))
        {
            log_winhttp_error(
                "WinHttpReadData"
            );

            return BOT_ERR_NETWORK;
        }


        response->body_size +=
            (size_t)bytes_read;


        /*
         * Завершаем C-строку.
         */
        response->body[
            response->body_size
        ] = '\0';
    }


    /*
     * Сервер может вернуть HTTP-ответ
     * вообще без body.
     *
     * В таком случае создаём пустую строку "".
     */
    if (response->body == NULL)
    {
        response->body = malloc(1);


        if (response->body == NULL)
        {
            return BOT_ERR_UNKNOWN;
        }


        response->body[0] = '\0';
    }


    return BOT_OK;
}


/*
 * ================================================================
 * HTTP GET
 * ================================================================
 */
BotResult http_get(
    const wchar_t *host,
    const wchar_t *path,
    const wchar_t *additional_headers,
    HttpResponse *response
)
{
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;

    DWORD response_status = 0;
    DWORD response_status_size =
        sizeof(response_status);

    BotResult result = BOT_ERR_NETWORK;


    /*
     * Проверяем аргументы.
     */
    if (host == NULL ||
        path == NULL ||
        response == NULL)
    {
        return BOT_ERR_NETWORK;
    }


    /*
     * Обнуляем HttpResponse.
     */
    response->status_code = 0;
    response->body = NULL;
    response->body_size = 0;


    /*
     * ------------------------------------------------------------
     * 1. Создаём WinHTTP session.
     * ------------------------------------------------------------
     */
    session = WinHttpOpen(
        L"TwitchBot/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );


    if (session == NULL)
    {
        log_winhttp_error(
            "WinHttpOpen"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 2. Подключаемся к серверу.
     *
     * Например:
     *
     * host = L"example.com"
     * ------------------------------------------------------------
     */
    connection = WinHttpConnect(
        session,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );


    if (connection == NULL)
    {
        log_winhttp_error(
            "WinHttpConnect"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 3. Создаём GET request.
     *
     * Например:
     *
     * path = L"/"
     *
     * или:
     *
     * path = L"/helix/users?login=test"
     * ------------------------------------------------------------
     */
    request = WinHttpOpenRequest(
        connection,
        L"GET",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );


    if (request == NULL)
    {
        log_winhttp_error(
            "WinHttpOpenRequest"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 4. Добавляем HTTP-заголовки.
     *
     * ВАЖНО:
     *
     * заголовки добавляем ДО WinHttpSendRequest().
     * ------------------------------------------------------------
     */
    if (additional_headers != NULL)
    {
        if (!WinHttpAddRequestHeaders(
                request,
                additional_headers,
                (DWORD)-1L,
                WINHTTP_ADDREQ_FLAG_ADD))
        {
            log_winhttp_error(
                "WinHttpAddRequestHeaders"
            );

            goto cleanup;
        }
    }


    /*
     * ------------------------------------------------------------
     * 5. Отправляем запрос.
     *
     * Только ОДИН раз.
     * ------------------------------------------------------------
     */
    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0))
    {
        log_winhttp_error(
            "WinHttpSendRequest"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 6. Получаем ответ сервера.
     * ------------------------------------------------------------
     */
    if (!WinHttpReceiveResponse(
            request,
            NULL))
    {
        log_winhttp_error(
            "WinHttpReceiveResponse"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 7. Получаем HTTP status.
     *
     * Например:
     *
     * 200
     * 401
     * 404
     * 500
     * ------------------------------------------------------------
     */
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &response_status,
            &response_status_size,
            WINHTTP_NO_HEADER_INDEX))
    {
        log_winhttp_error(
            "WinHttpQueryHeaders"
        );

        goto cleanup;
    }


    response->status_code =
        (unsigned long)response_status;


    /*
     * ------------------------------------------------------------
     * 8. Читаем body.
     * ------------------------------------------------------------
     */
    result = read_response_body(
        request,
        response
    );


cleanup:

    /*
     * Закрываем handles в обратном порядке.
     */

    if (request != NULL)
    {
        WinHttpCloseHandle(
            request
        );
    }


    if (connection != NULL)
    {
        WinHttpCloseHandle(
            connection
        );
    }


    if (session != NULL)
    {
        WinHttpCloseHandle(
            session
        );
    }


    /*
     * Если произошла ошибка,
     * очищаем частично сформированный ответ.
     */
    if (result != BOT_OK)
    {
        http_response_free(
            response
        );
    }


    return result;
}


/*
 * ================================================================
 * HTTP RESPONSE FREE
 * ================================================================
 */
void http_response_free(
    HttpResponse *response
)
{
    if (response == NULL)
    {
        return;
    }


    if (response->body != NULL)
    {
        free(
            response->body
        );

        response->body = NULL;
    }


    response->body_size = 0;
    response->status_code = 0;
}


/*
 * ================================================================
 * HTTP POST
 * ================================================================
 */
BotResult http_post(
    const wchar_t *host,
    const wchar_t *path,
    const wchar_t *additional_headers,
    const char *body,
    HttpResponse *response
)
{
    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;

    DWORD response_status = 0;
    DWORD response_status_size =
        sizeof(response_status);

    DWORD body_size;

    BotResult result = BOT_ERR_NETWORK;


    /*
     * Проверяем аргументы.
     */
    if (host == NULL ||
        path == NULL ||
        body == NULL ||
        response == NULL)
    {
        return BOT_ERR_NETWORK;
    }


    /*
     * Обнуляем ответ.
     */
    response->status_code = 0;
    response->body = NULL;
    response->body_size = 0;


    /*
     * Размер POST-body в байтах.
     */
    body_size =
        (DWORD)strlen(body);


    /*
     * ------------------------------------------------------------
     * 1. Создаём WinHTTP session.
     * ------------------------------------------------------------
     */
    session = WinHttpOpen(
        L"TwitchBot/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );


    if (session == NULL)
    {
        log_winhttp_error(
            "WinHttpOpen"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 2. Подключаемся к серверу.
     * ------------------------------------------------------------
     */
    connection = WinHttpConnect(
        session,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );


    if (connection == NULL)
    {
        log_winhttp_error(
            "WinHttpConnect"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 3. Создаём POST request.
     * ------------------------------------------------------------
     */
    request = WinHttpOpenRequest(
        connection,
        L"POST",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );


    if (request == NULL)
    {
        log_winhttp_error(
            "WinHttpOpenRequest"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 4. Добавляем заголовки.
     *
     * Например:
     *
     * Content-Type:
     * application/x-www-form-urlencoded
     * ------------------------------------------------------------
     */
    if (additional_headers != NULL)
    {
        if (!WinHttpAddRequestHeaders(
                request,
                additional_headers,
                (DWORD)-1L,
                WINHTTP_ADDREQ_FLAG_ADD))
        {
            log_winhttp_error(
                "WinHttpAddRequestHeaders"
            );

            goto cleanup;
        }
    }


    /*
     * ------------------------------------------------------------
     * 5. Отправляем POST вместе с body.
     * ------------------------------------------------------------
     */
    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            (LPVOID)body,
            body_size,
            body_size,
            0))
    {
        log_winhttp_error(
            "WinHttpSendRequest"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 6. Получаем ответ.
     * ------------------------------------------------------------
     */
    if (!WinHttpReceiveResponse(
            request,
            NULL))
    {
        log_winhttp_error(
            "WinHttpReceiveResponse"
        );

        goto cleanup;
    }


    /*
     * ------------------------------------------------------------
     * 7. Получаем HTTP status.
     * ------------------------------------------------------------
     */
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE |
                WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &response_status,
            &response_status_size,
            WINHTTP_NO_HEADER_INDEX))
    {
        log_winhttp_error(
            "WinHttpQueryHeaders"
        );

        goto cleanup;
    }


    response->status_code =
        (unsigned long)response_status;


    /*
     * ------------------------------------------------------------
     * 8. Читаем тело HTTP-ответа.
     * ------------------------------------------------------------
     */
    result = read_response_body(
        request,
        response
    );


cleanup:

    if (request != NULL)
    {
        WinHttpCloseHandle(
            request
        );
    }


    if (connection != NULL)
    {
        WinHttpCloseHandle(
            connection
        );
    }


    if (session != NULL)
    {
        WinHttpCloseHandle(
            session
        );
    }


    if (result != BOT_OK)
    {
        http_response_free(
            response
        );
    }


    return result;
}
