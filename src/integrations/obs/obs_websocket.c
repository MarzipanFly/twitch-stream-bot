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

static BotResult send_json(ObsWebSocket *client, cJSON *json)
{
    char *text = cJSON_PrintUnformatted(json);
    DWORD error;
    ObsSend send = (ObsSend)client->websocket_send;
    if (text == NULL) return BOT_ERR_JSON;
    error = send(client->websocket, OBS_WS_UTF8_MESSAGE,
                 text, (DWORD)strlen(text));
    free(text);
    return error == NO_ERROR ? BOT_OK : BOT_ERR_NETWORK;
}

static BotResult identify(ObsWebSocket *client, const char *password)
{
    cJSON *hello = NULL, *body, *auth, *identified = NULL, *packet = NULL;
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

    packet = cJSON_CreateObject();
    body = cJSON_CreateObject();
    if (packet == NULL || body == NULL)
    {
        cJSON_Delete(body);
        result = BOT_ERR_JSON;
        goto cleanup;
    }
    cJSON_AddNumberToObject(packet, "op", 1);
    cJSON_AddItemToObject(packet, "d", body);
    cJSON_AddNumberToObject(body, "rpcVersion", 1);
    cJSON_AddNumberToObject(body, "eventSubscriptions", 0);
    if (auth != NULL) cJSON_AddStringToObject(body, "authentication", response);
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
    cJSON_Delete(packet);
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
    static unsigned long request_sequence = 0;
    char id[40];
    cJSON *packet = NULL, *body = NULL, *data = NULL, *reply = NULL;
    const cJSON *op, *d, *status, *success, *response_data, *returned_id;
    char *encoded = NULL;
    BotResult result = BOT_ERR_JSON;

    if (!client || !client->authenticated || !client->websocket ||
        !request_type || !response || response_size == 0)
        return BOT_ERR_CONFIG;
    response[0] = '\0';
    snprintf(id, sizeof(id), "twitchbot-%lu", ++request_sequence);
    packet = cJSON_CreateObject();
    body = cJSON_CreateObject();
    if (!packet || !body) goto cleanup;
    cJSON_AddNumberToObject(packet, "op", 6);
    cJSON_AddItemToObject(packet, "d", body);
    cJSON_AddStringToObject(body, "requestType", request_type);
    cJSON_AddStringToObject(body, "requestId", id);
    if (request_data_json && request_data_json[0])
    {
        data = cJSON_Parse(request_data_json);
        if (!cJSON_IsObject(data)) goto cleanup;
        cJSON_AddItemToObject(body, "requestData", data);
        data = NULL;
    }
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
        !cJSON_IsTrue(success))
    {
        log_warning("OBS request failed: %s", request_type);
        result = BOT_ERR_NETWORK;
        goto cleanup;
    }
    response_data = cJSON_GetObjectItemCaseSensitive(d, "responseData");
    if (response_data)
    {
        encoded = cJSON_PrintUnformatted(response_data);
        if (!encoded || strlen(encoded) >= response_size)
        {
            result = BOT_ERR_JSON;
            goto cleanup;
        }
        strcpy(response, encoded);
    }
    result = BOT_OK;
cleanup:
    cJSON_Delete(packet);
    cJSON_Delete(data);
    cJSON_Delete(reply);
    if (encoded) free(encoded);
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

BotResult obs_websocket_set_scene(ObsWebSocket *client, const char *scene_name)
{
    cJSON *data = cJSON_CreateObject();
    char *json;
    char response[64];
    BotResult result;
    if (!scene_name || !data) { cJSON_Delete(data); return BOT_ERR_CONFIG; }
    cJSON_AddStringToObject(data, "sceneName", scene_name);
    json = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);
    if (!json) return BOT_ERR_JSON;
    result = obs_websocket_request(client, "SetCurrentProgramScene", json, response, sizeof(response));
    free(json);
    return result;
}

BotResult obs_websocket_restart_media(ObsWebSocket *client, const char *input_name)
{
    cJSON *data = cJSON_CreateObject();
    char *json;
    char response[64];
    BotResult result;
    if (!input_name || !data) { cJSON_Delete(data); return BOT_ERR_CONFIG; }
    cJSON_AddStringToObject(data, "inputName", input_name);
    cJSON_AddStringToObject(data, "mediaAction", "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART");
    json = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);
    if (!json) return BOT_ERR_JSON;
    result = obs_websocket_request(client, "TriggerMediaInputAction", json, response, sizeof(response));
    free(json);
    return result;
}

BotResult obs_websocket_set_input_mute(ObsWebSocket *client, const char *input_name, int muted)
{
    cJSON *data = cJSON_CreateObject();
    char *json;
    char response[64];
    BotResult result;
    if (!input_name || !data) { cJSON_Delete(data); return BOT_ERR_CONFIG; }
    cJSON_AddStringToObject(data, "inputName", input_name);
    cJSON_AddBoolToObject(data, "inputMuted", muted != 0);
    json = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);
    if (!json) return BOT_ERR_JSON;
    result = obs_websocket_request(client, "SetInputMute", json, response, sizeof(response));
    free(json);
    return result;
}
