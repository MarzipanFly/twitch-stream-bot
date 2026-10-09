#include "obs_websocket.h"
#include "cJSON.h"
#include "logger.h"

#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBS_WS_UPGRADE_OPTION 114
#define OBS_WS_UTF8_MESSAGE 2
#define OBS_WS_UTF8_FRAGMENT 3
#define OBS_WS_BINARY_MESSAGE 0
#define OBS_WS_BINARY_FRAGMENT 1
#define OBS_WS_CLOSE 4

typedef HINTERNET (WINAPI *ObsCompleteUpgrade)(HINTERNET, DWORD_PTR);
typedef DWORD (WINAPI *ObsReceive)(HINTERNET, PVOID, DWORD, DWORD *, DWORD *);
typedef DWORD (WINAPI *ObsSend)(HINTERNET, DWORD, PVOID, DWORD);
typedef DWORD (WINAPI *ObsClose)(HINTERNET, USHORT, PVOID, DWORD);

void obs_websocket_close(ObsWebSocket *client)
{
    if (client == NULL) return;
    if (client->websocket != NULL)
    {
        /* Release even when OBS has already disconnected. */
        WinHttpCloseHandle(client->websocket);
        client->websocket = NULL;
    }
    if (client->request != NULL) { WinHttpCloseHandle(client->request); client->request = NULL; }
    if (client->connection != NULL) { WinHttpCloseHandle(client->connection); client->connection = NULL; }
    if (client->session != NULL) { WinHttpCloseHandle(client->session); client->session = NULL; }
    if (client->winhttp_module != NULL) { FreeLibrary(client->winhttp_module); client->winhttp_module = NULL; }
    client->authenticated = 0;
}

/* SHA256 bytes -> Base64; Windows CryptoAPI, no third-party dependency. */
static int sha256_base64(const char *input, char *output, DWORD output_size)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[32];
    DWORD digest_size = sizeof(digest);
    DWORD encoded_size = output_size;
    int ok = 0;

    if (!CryptAcquireContextA(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return 0;
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
        goto cleanup;
    if (!CryptHashData(hash, (const BYTE *)input, (DWORD)strlen(input), 0))
        goto cleanup;
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0))
        goto cleanup;
    if (!CryptBinaryToStringA(digest, digest_size,
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, output, &encoded_size))
        goto cleanup;
    ok = 1;
cleanup:
    SecureZeroMemory(digest, sizeof(digest));
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ok;
}

/* OBS v5: base64(sha256(base64(sha256(password + salt)) + challenge)). */
static int make_auth(const char *password, const char *salt,
                     const char *challenge, char result[64])
{
    char joined[1024];
    char secret[64];
    int ok = 0;
    if (snprintf(joined, sizeof(joined), "%s%s", password, salt) >= (int)sizeof(joined))
        return 0;
    if (!sha256_base64(joined, secret, sizeof(secret))) goto cleanup;
    if (snprintf(joined, sizeof(joined), "%s%s", secret, challenge) >= (int)sizeof(joined))
        goto cleanup;
    ok = sha256_base64(joined, result, 64);
cleanup:
    SecureZeroMemory(joined, sizeof(joined));
    SecureZeroMemory(secret, sizeof(secret));
    return ok;
}

/* Receive one complete UTF-8 message; supports fragmented frames. */
static BotResult receive_json(ObsWebSocket *client, cJSON **json)
{
    char message[16384];
    size_t used = 0;
    DWORD count = 0, kind = 0, error;
    ObsReceive receive = (ObsReceive)client->websocket_receive;
    *json = NULL;
    for (;;)
    {
        if (used >= sizeof(message) - 1) return BOT_ERR_JSON;
        error = receive(client->websocket, message + used,
                        (DWORD)(sizeof(message) - used - 1), &count, &kind);
        if (error != NO_ERROR) return BOT_ERR_NETWORK;
        if (kind == OBS_WS_CLOSE) return BOT_ERR_NETWORK;
        if (kind == OBS_WS_BINARY_MESSAGE || kind == OBS_WS_BINARY_FRAGMENT)
            return BOT_ERR_JSON;
        used += count;
        if (kind == OBS_WS_UTF8_MESSAGE) break;
        if (kind != OBS_WS_UTF8_FRAGMENT) return BOT_ERR_JSON;
    }
    message[used] = '\0';
    *json = cJSON_Parse(message);
    return *json != NULL ? BOT_OK : BOT_ERR_JSON;
}

/* Minimal cJSON in this repository only parses; encode JSON locally. */
static int json_quote(const char *src, char *dst, size_t capacity)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)src;
    if (!src || capacity < 3) return 0;
    dst[n++] = '"';
    for (; *p; ++p)
    {
        const char *escape = NULL;
        char unicode[7];
        size_t len;
        if (*p == '"' ) escape = "\\\"";
        else if (*p == '\\') escape = "\\\\";
        else if (*p == '\n') escape = "\\n";
        else if (*p == '\r') escape = "\\r";
        else if (*p == '\t') escape = "\\t";
        else if (*p < 0x20)
        {
            snprintf(unicode, sizeof(unicode), "\\u%04x", (unsigned)*p);
            escape = unicode;
        }
        if (escape)
        {
            len = strlen(escape);
            if (n + len + 2 > capacity) return 0;
            memcpy(dst + n, escape, len);
            n += len;
        }
        else
        {
            if (n + 2 >= capacity) return 0;
            dst[n++] = (char)*p;
        }
    }
    dst[n++] = '"';
    dst[n] = '\0';
    return 1;
}

static BotResult send_json(ObsWebSocket *client, const char *json)
{
    ObsSend send = (ObsSend)client->websocket_send;
    DWORD error;
    if (!json) return BOT_ERR_JSON;
    error = send(client->websocket, OBS_WS_UTF8_MESSAGE,
                 (PVOID)json, (DWORD)strlen(json));
    return error == NO_ERROR ? BOT_OK : BOT_ERR_NETWORK;
}

/* Serialize parser nodes for responseData; no dependency on upstream cJSON. */
static int append_json(char *out, size_t capacity, size_t *used, const cJSON *node)
{
    const cJSON *child;
    const char *literal = NULL;
    char number[64];
    char quoted[16384];
    size_t len;
    int first;
    if (!node) return 0;
    if (node->type & cJSON_Object)
    {
        if (*used + 2 >= capacity) return 0;
        out[(*used)++] = '{';
        first = 1;
        for (child = node->child; child; child = child->next)
        {
            if (!first) { if (*used + 2 >= capacity) return 0; out[(*used)++] = ','; }
            if (!json_quote(child->string ? child->string : "", quoted, sizeof(quoted))) return 0;
            len = strlen(quoted);
            if (*used + len + 2 >= capacity) return 0;
            memcpy(out + *used, quoted, len); *used += len;
            out[(*used)++] = ':';
            if (!append_json(out, capacity, used, child)) return 0;
            first = 0;
        }
        if (*used + 2 > capacity) return 0;
        out[(*used)++] = '}';
    }
    else if (node->type & cJSON_Array)
    {
        if (*used + 2 >= capacity) return 0;
        out[(*used)++] = '[';
        first = 1;
        for (child = node->child; child; child = child->next)
        {
            if (!first) { if (*used + 2 >= capacity) return 0; out[(*used)++] = ','; }
            if (!append_json(out, capacity, used, child)) return 0;
            first = 0;
        }
        if (*used + 2 > capacity) return 0;
        out[(*used)++] = ']';
    }
    else if (node->type & cJSON_String)
    {
        if (!json_quote(node->valuestring ? node->valuestring : "", quoted, sizeof(quoted))) return 0;
        literal = quoted;
    }
    else if (node->type & cJSON_True) literal = "true";
    else if (node->type & cJSON_False) literal = "false";
    else if (node->type & cJSON_NULL) literal = "null";
    else if (node->type & cJSON_Number)
    {
        snprintf(number, sizeof(number), "%.17g", node->valuedouble);
        literal = number;
    }
    else return 0;
    if (literal)
    {
        len = strlen(literal);
        if (*used + len + 1 > capacity) return 0;
        memcpy(out + *used, literal, len);
        *used += len;
    }
    out[*used] = '\0';
    return 1;
}

static BotResult identify(ObsWebSocket *client, const char *password)
{
    cJSON *hello = NULL, *identified = NULL;
    const cJSON *auth;
    char packet[512];
    char quoted_auth[160];
    const cJSON *op, *data, *version, *salt, *challenge;
    char response[64] = {0};
    BotResult result = receive_json(client, &hello);
    if (result != BOT_OK) return result;

    op = cJSON_GetObjectItemCaseSensitive(hello, "op");
    data = cJSON_GetObjectItemCaseSensitive(hello, "d");
    version = cJSON_GetObjectItemCaseSensitive(data, "rpcVersion");
    auth = cJSON_GetObjectItemCaseSensitive(data, "authentication");

    if (!cJSON_IsNumber(op) || op->valueint != 0 ||
        !cJSON_IsObject(data) || !cJSON_IsNumber(version) ||
        version->valueint < 1)
    {
        result = BOT_ERR_JSON;
        goto cleanup;
    }

    if (auth != NULL)
    {
        salt = cJSON_GetObjectItemCaseSensitive(auth, "salt");
        challenge = cJSON_GetObjectItemCaseSensitive(auth, "challenge");
        if (!cJSON_IsString(salt) || !cJSON_IsString(challenge) ||
            password == NULL || password[0] == '\0' ||
            !make_auth(password, salt->valuestring, challenge->valuestring, response))
        {
            result = BOT_ERR_AUTH;
            goto cleanup;
        }
    }

    if (auth != NULL)
    {
        if (!json_quote(response, quoted_auth, sizeof(quoted_auth)))
        {
            result = BOT_ERR_JSON;
            goto cleanup;
        }
        snprintf(packet, sizeof(packet),
                 "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0,\"authentication\":%s}}",
                 quoted_auth);
    }
    else
    {
        snprintf(packet, sizeof(packet),
                 "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0}}");
    }
    result = send_json(client, packet);
    if (result != BOT_OK) goto cleanup;

    result = receive_json(client, &identified);
    if (result != BOT_OK) goto cleanup;
    op = cJSON_GetObjectItemCaseSensitive(identified, "op");
    data = cJSON_GetObjectItemCaseSensitive(identified, "d");
    if (!cJSON_IsNumber(op) || op->valueint != 2 ||
        !cJSON_IsObject(data))
    {
        result = BOT_ERR_AUTH;
        goto cleanup;
    }
    client->authenticated = 1;
    log_info("OBS WebSocket v5 authentication completed");
    result = BOT_OK;
cleanup:
    SecureZeroMemory(response, sizeof(response));
    cJSON_Delete(hello);
    cJSON_Delete(identified);
    return result;
}

BotResult obs_websocket_connect(ObsWebSocket *client, const char *password)
{
    DWORD status = 0, status_size = sizeof(status);
    ObsCompleteUpgrade upgrade;
    BotResult result = BOT_ERR_NETWORK;

    if (client == NULL) return BOT_ERR_CONFIG;
    memset(client, 0, sizeof(*client));
    client->winhttp_module = LoadLibraryW(L"winhttp.dll");
    if (client->winhttp_module == NULL) goto fail;
    client->websocket_complete_upgrade = GetProcAddress(client->winhttp_module, "WinHttpWebSocketCompleteUpgrade");
    client->websocket_receive = GetProcAddress(client->winhttp_module, "WinHttpWebSocketReceive");
    client->websocket_send = GetProcAddress(client->winhttp_module, "WinHttpWebSocketSend");
    client->websocket_close = GetProcAddress(client->winhttp_module, "WinHttpWebSocketClose");
    if (!client->websocket_complete_upgrade || !client->websocket_receive ||
        !client->websocket_send || !client->websocket_close) goto fail;

    client->session = WinHttpOpen(L"TwitchBot/0.8", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!client->session) goto fail;
    WinHttpSetTimeouts(client->session, 5000, 5000, 5000, 5000);
    client->connection = WinHttpConnect(client->session, L"127.0.0.1", 4455, 0);
    if (!client->connection) goto fail;
    client->request = WinHttpOpenRequest(client->connection, L"GET", L"/", NULL,
                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!client->request) goto fail;
    if (!WinHttpSetOption(client->request, OBS_WS_UPGRADE_OPTION, NULL, 0)) goto fail;
    if (!WinHttpSendRequest(client->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto fail;
    if (!WinHttpReceiveResponse(client->request, NULL)) goto fail;
    if (!WinHttpQueryHeaders(client->request, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &status_size, WINHTTP_NO_HEADER_INDEX) || status != 101) goto fail;

    upgrade = (ObsCompleteUpgrade)client->websocket_complete_upgrade;
    client->websocket = upgrade(client->request, 0);
    if (!client->websocket) goto fail;
    WinHttpCloseHandle(client->request);
    client->request = NULL;

    result = identify(client, password);
    if (result != BOT_OK) goto fail;
    return BOT_OK;
fail:
    log_error("OBS WebSocket connection/authentication failed (result=%d, Win32=%lu)",
              (int)result, (unsigned long)GetLastError());
    obs_websocket_close(client);
    return result;
}

/*
 * Synchronous OBS WebSocket v5 requests.
 * Call from one thread at a time; no event subscriptions are requested.
 */
BotResult obs_websocket_request(ObsWebSocket *client, const char *request_type,
                                const char *request_data_json,
                                char *response, size_t response_size)
{
    static unsigned long sequence = 0;
    char id[40], quoted_type[256], quoted_id[80];
    char packet[16384];
    cJSON *request_data = NULL, *reply = NULL;
    const cJSON *op, *d, *status, *success, *response_data, *returned_id;
    BotResult result = BOT_ERR_JSON;
    size_t used = 0;
    int written;

    if (!client || !client->authenticated || !client->websocket ||
        !request_type || !response || response_size == 0)
        return BOT_ERR_CONFIG;
    response[0] = '\0';
    snprintf(id, sizeof(id), "twitchbot-%lu", ++sequence);
    if (!json_quote(request_type, quoted_type, sizeof(quoted_type)) ||
        !json_quote(id, quoted_id, sizeof(quoted_id))) return BOT_ERR_JSON;

    if (request_data_json && request_data_json[0])
    {
        request_data = cJSON_Parse(request_data_json);
        if (!cJSON_IsObject(request_data)) goto cleanup;
        written = snprintf(packet, sizeof(packet),
            "{\"op\":6,\"d\":{\"requestType\":%s,\"requestId\":%s,\"requestData\":%s}}",
            quoted_type, quoted_id, request_data_json);
    }
    else
    {
        written = snprintf(packet, sizeof(packet),
            "{\"op\":6,\"d\":{\"requestType\":%s,\"requestId\":%s}}",
            quoted_type, quoted_id);
    }
    if (written < 0 || (size_t)written >= sizeof(packet)) goto cleanup;
    result = send_json(client, packet);
    if (result != BOT_OK) goto cleanup;
    result = receive_json(client, &reply);
    if (result != BOT_OK) goto cleanup;
    op = cJSON_GetObjectItemCaseSensitive(reply, "op");
    d = cJSON_GetObjectItemCaseSensitive(reply, "d");
    returned_id = cJSON_GetObjectItemCaseSensitive(d, "requestId");
    status = cJSON_GetObjectItemCaseSensitive(d, "requestStatus");
    success = cJSON_GetObjectItemCaseSensitive(status, "result");
    if (!cJSON_IsNumber(op) || op->valueint != 7 ||
        !cJSON_IsString(returned_id) || strcmp(returned_id->valuestring, id) ||
        !success || !(success->type & cJSON_True))
    {
        log_warning("OBS request failed: %s", request_type);
        result = BOT_ERR_NETWORK;
        goto cleanup;
    }
    response_data = cJSON_GetObjectItemCaseSensitive(d, "responseData");
    if (response_data && !append_json(response, response_size, &used, response_data))
    {
        result = BOT_ERR_JSON;
        goto cleanup;
    }
    result = BOT_OK;
cleanup:
    cJSON_Delete(request_data);
    cJSON_Delete(reply);
    return result;
}

BotResult obs_websocket_get_version(ObsWebSocket *client, char *response, size_t size)
{
    return obs_websocket_request(client, "GetVersion", NULL, response, size);
}

BotResult obs_websocket_get_scene(ObsWebSocket *client, char *response, size_t size)
{
    return obs_websocket_request(client, "GetCurrentProgramScene", NULL, response, size);
}

static BotResult obs_request_string(ObsWebSocket *client, const char *request,
                                    const char *key, const char *value,
                                    const char *extra)
{
    char quoted[8192], json[10000], response[64];
    int written;
    if (!value || !json_quote(value, quoted, sizeof(quoted))) return BOT_ERR_CONFIG;
    written = snprintf(json, sizeof(json), "{\"%s\":%s%s}", key, quoted,
                       extra ? extra : "");
    if (written < 0 || (size_t)written >= sizeof(json)) return BOT_ERR_JSON;
    return obs_websocket_request(client, request, json, response, sizeof(response));
}

BotResult obs_websocket_set_scene(ObsWebSocket *client, const char *scene_name)
{
    return obs_request_string(client, "SetCurrentProgramScene",
                              "sceneName", scene_name, NULL);
}

BotResult obs_websocket_restart_media(ObsWebSocket *client, const char *input_name)
{
    return obs_request_string(client, "TriggerMediaInputAction", "inputName",
             input_name, ",\"mediaAction\":\"OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART\"");
}

BotResult obs_websocket_set_input_mute(ObsWebSocket *client, const char *input_name, int muted)
{
    return obs_request_string(client, "SetInputMute", "inputName", input_name,
                              muted ? ",\"inputMuted\":true" : ",\"inputMuted\":false");
}
