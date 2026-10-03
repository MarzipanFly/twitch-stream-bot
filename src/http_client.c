#include "http_client.h"
#include "logger.h"

#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>
#include <string.h>

static void log_winhttp_error(
    const char *operation
)
{
    DWORD error_code;

    error_code =
        GetLastError();

    log_error(
        "%s failed. Windows error code: %lu",
        operation,
        (unsigned long)error_code
    );
}

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

        if (!WinHttpQueryDataAvailable(
                request,
                &available))
        {
            log_winhttp_error(
                "WinHttpQueryDataAvailable"
            );

            return BOT_ERR_NETWORK;
        }

        if (available == 0)
        {
            break;
        }

        new_size =
            response->body_size +
            (size_t)available +
            1;

        new_buffer =
            (char *)realloc(
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

        response->body =
            new_buffer;

        bytes_read = 0;

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

        response->body[
            response->body_size
        ] = '\0';
    }

    if (response->body == NULL)
    {
        response->body =
            (char *)malloc(1);

        if (response->body == NULL)
        {
            return BOT_ERR_UNKNOWN;
        }

        response->body[0] =
            '\0';
    }

    return BOT_OK;
}

static BotResult query_status_and_body(
    HINTERNET request,
    HttpResponse *response
)
{
    DWORD response_status = 0;
    DWORD response_status_size;

    BotResult result;

    response_status_size =
        sizeof(response_status);

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

        return BOT_ERR_NETWORK;
    }

    response->status_code =
        (unsigned long)response_status;

    result =
        read_response_body(
            request,
            response
        );

    return result;
}

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

    BotResult result =
        BOT_ERR_NETWORK;

    if (host == NULL ||
        path == NULL ||
        response == NULL)
    {
        return BOT_ERR_NETWORK;
    }

    response->status_code = 0;
    response->body = NULL;
    response->body_size = 0;

    session =
        WinHttpOpen(
            L"TwitchBot/0.6",
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

    connection =
        WinHttpConnect(
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

    request =
        WinHttpOpenRequest(
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

    if (!WinHttpReceiveResponse(
            request,
            NULL))
    {
        log_winhttp_error(
            "WinHttpReceiveResponse"
        );

        goto cleanup;
    }

    result =
        query_status_and_body(
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

    return result;
}

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

    DWORD body_size = 0;

    BotResult result =
        BOT_ERR_NETWORK;

    if (host == NULL ||
        path == NULL ||
        response == NULL)
    {
        return BOT_ERR_NETWORK;
    }

    response->status_code = 0;
    response->body = NULL;
    response->body_size = 0;

    if (body != NULL)
    {
        body_size =
            (DWORD)strlen(body);
    }

    session =
        WinHttpOpen(
            L"TwitchBot/0.6",
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

    connection =
        WinHttpConnect(
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

    request =
        WinHttpOpenRequest(
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

    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            body_size > 0
                ? (LPVOID)body
                : WINHTTP_NO_REQUEST_DATA,
            body_size,
            body_size,
            0))
    {
        log_winhttp_error(
            "WinHttpSendRequest"
        );

        goto cleanup;
    }

    if (!WinHttpReceiveResponse(
            request,
            NULL))
    {
        log_winhttp_error(
            "WinHttpReceiveResponse"
        );

        goto cleanup;
    }

    result =
        query_status_and_body(
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

    return result;
}

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

        response->body =
            NULL;
    }

    response->body_size = 0;
    response->status_code = 0;
}
