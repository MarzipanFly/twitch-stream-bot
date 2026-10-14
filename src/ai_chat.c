#include "ai_chat.h"
#include "logger.h"
#include "twitch_chat.h"
#include "cJSON.h"

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AI_QUEUE_CAPACITY 5
#define AI_COOLDOWN_CAPACITY 128
#define AI_QUESTION_BYTES 480
#define AI_RESPONSE_BYTES 320
#define AI_MAX_HTTP_BODY (128 * 1024)
#define AI_TWITCH_GAP_MS 1500

typedef struct
{
    char user_id[TWITCH_EVENTSUB_ID_SIZE];
    char login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char question[AI_QUESTION_BYTES + 1];
    TwitchConfig twitch; /* Snapshot avoids racing token refresh in the main loop. */
} AiJob;

typedef struct
{
    char user_id[TWITCH_EVENTSUB_ID_SIZE];
    ULONGLONG last_tick;
} AiCooldown;

typedef struct
{
    CRITICAL_SECTION lock;
    HANDLE wake;
    HANDLE thread;
    int started;
    int stopping;
    unsigned int cooldown_ms;
    char model[128];
    AiJob jobs[AI_QUEUE_CAPACITY];
    size_t count;
    AiCooldown cooldowns[AI_COOLDOWN_CAPACITY];
    size_t next_cooldown_slot;
} AiState;

static AiState g_ai;

/* Keep the generated reply on one line and never split a UTF-8 character. */
static void clean_reply(const char *source, char *out, size_t cap)
{
    const unsigned char *p = (const unsigned char *)source;
    size_t used = 0;
    int space = 0;
    if (!source || !out || cap == 0) return;
    out[0] = '\0';
    while (*p && used + 1 < cap)
    {
        size_t n = 1;
        size_t i;
        if (*p <= 0x20 || *p == 0x7f)
        {
            if (used > 0) space = 1;
            ++p;
            continue;
        }
        if ((*p & 0xe0) == 0xc0) n = 2;
        else if ((*p & 0xf0) == 0xe0) n = 3;
        else if ((*p & 0xf8) == 0xf0) n = 4;
        else if (*p >= 0x80) { ++p; continue; }
        for (i = 1; i < n; ++i)
        {
            if (!p[i] || (p[i] & 0xc0) != 0x80) break;
        }
        if (i != n) { ++p; continue; }
        if (used + (space ? 1 : 0) + n >= cap) break;
        if (space) out[used++] = ' ';
        space = 0;
        memcpy(out + used, p, n);
        used += n;
        p += n;
    }
    out[used] = '\0';
}

/* Direct HTTP, deliberately bypassing proxy/VPN settings for 127.0.0.1. */
static int ollama_generate(const char *model, const AiJob *job,
                           char *answer, size_t answer_cap)
{
    HINTERNET session = NULL, connection = NULL, request = NULL;
    cJSON *root = NULL, *messages = NULL, *message = NULL, *options = NULL;
    cJSON *parsed = NULL, *response_message, *content;
    char *payload = NULL, *received = NULL;
    char prompt[750];
    DWORD status = 0, status_size = sizeof(status);
    size_t total = 0;
    int ok = 0;
    if (!model || !job || !answer || !answer_cap) return 0;
    answer[0] = '\0';
    snprintf(prompt, sizeof(prompt),
             "Зритель %s спрашивает: %s\nОтветь кратко, по-русски, одной репликой для Twitch-чата.",
             job->login, job->question);
    root = cJSON_CreateObject();
    messages = cJSON_CreateArray();
    message = cJSON_CreateObject();
    options = cJSON_CreateObject();
    if (!root || !messages || !message || !options) goto cleanup;
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddBoolToObject(root, "stream", 0);
    cJSON_AddBoolToObject(root, "think", 0);
    cJSON_AddStringToObject(message, "role", "user");
    cJSON_AddStringToObject(message, "content", prompt);
    cJSON_AddItemToArray(messages, message);
    message = NULL;
    cJSON_AddItemToObject(root, "messages", messages);
    messages = NULL;
    cJSON_AddNumberToObject(options, "num_predict", 120);
    cJSON_AddNumberToObject(options, "num_ctx", 2048);
    cJSON_AddItemToObject(root, "options", options);
    options = NULL;
    payload = cJSON_PrintUnformatted(root);
    if (!payload || strlen(payload) > 4096) goto cleanup;

    session = WinHttpOpen(L"ApelsinBot-LocalAI/1.0",
                          WINHTTP_ACCESS_TYPE_NO_PROXY,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) goto cleanup;
    if (!WinHttpSetTimeouts(session, 5000, 5000, 5000, 60000))
        goto cleanup;
    connection = WinHttpConnect(session, L"127.0.0.1", 11434, 0);
    if (!connection) goto cleanup;
    request = WinHttpOpenRequest(connection, L"POST", L"/api/chat",
                                 NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) goto cleanup;
    if (!WinHttpAddRequestHeaders(request,
            L"Content-Type: application/json; charset=utf-8\r\n",
            (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD))
        goto cleanup;
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            payload, (DWORD)strlen(payload),
                            (DWORD)strlen(payload), 0))
        goto cleanup;
    if (!WinHttpReceiveResponse(request, NULL)) goto cleanup;
    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) || status != 200)
    {
        log_warning("AI Ollama HTTP status: %lu", (unsigned long)status);
        goto cleanup;
    }
    received = (char *)malloc(AI_MAX_HTTP_BODY + 1);
    if (!received) goto cleanup;
    for (;;)
    {
        DWORD available = 0, got = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) goto cleanup;
        if (available == 0) break;
        if ((size_t)available > AI_MAX_HTTP_BODY - total) goto cleanup;
        if (!WinHttpReadData(request, received + total, available, &got))
            goto cleanup;
        if (got == 0) break;
        total += got;
    }
    received[total] = '\0';
    parsed = cJSON_Parse(received);
    if (!parsed) goto cleanup;
    response_message = cJSON_GetObjectItemCaseSensitive(parsed, "message");
    content = cJSON_GetObjectItemCaseSensitive(response_message, "content");
    if (!cJSON_IsString(content) || !content->valuestring) goto cleanup;
    clean_reply(content->valuestring, answer, answer_cap);
    ok = answer[0] != '\0';

cleanup:
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    cJSON_Delete(parsed);
    free(received);
    free(payload);
    cJSON_Delete(root);
    cJSON_Delete(options);
    cJSON_Delete(message);
    cJSON_Delete(messages);
    return ok;
}

static DWORD WINAPI ai_worker_thread(LPVOID unused)
{
    (void)unused;
    for (;;)
    {
        WaitForSingleObject(g_ai.wake, INFINITE);
        for (;;)
        {
            AiJob job;
            char answer[AI_RESPONSE_BYTES + 1];
            char outgoing[480];
            int generated;
            int stopping;
            BotResult result;
            EnterCriticalSection(&g_ai.lock);
            if (g_ai.stopping)
            {
                LeaveCriticalSection(&g_ai.lock);
                return 0;
            }
            if (g_ai.count == 0)
            {
                ResetEvent(g_ai.wake);
                LeaveCriticalSection(&g_ai.lock);
                break;
            }
            job = g_ai.jobs[0];
            --g_ai.count;
            if (g_ai.count)
                memmove(&g_ai.jobs[0], &g_ai.jobs[1],
                        g_ai.count * sizeof(g_ai.jobs[0]));
            LeaveCriticalSection(&g_ai.lock);

            generated = ollama_generate(g_ai.model, &job, answer, sizeof(answer));
            EnterCriticalSection(&g_ai.lock);
            stopping = g_ai.stopping;
            LeaveCriticalSection(&g_ai.lock);
            if (stopping) return 0;

            if (!generated)
            {
                log_warning("Local AI generation failed for %s", job.login);
                snprintf(outgoing, sizeof(outgoing),
                         "@%.40s 🍊 Пока не могу ответить. Проверь Ollama.",
                         job.login);
            }
            else
                snprintf(outgoing, sizeof(outgoing), "@%.40s %s",
                         job.login, answer);
            result = twitch_chat_send_message(&job.twitch, outgoing);
            if (result != BOT_OK)
                log_warning("AI Twitch reply failed for %s (result=%d)",
                            job.login, (int)result);
            else
                log_info("AI Twitch reply sent to %s", job.login);
            Sleep(AI_TWITCH_GAP_MS);
        }
    }
}

int ai_chat_start(const AiConfig *config)
{
    if (!config || !config->enabled) return 0;
    memset(&g_ai, 0, sizeof(g_ai));
    snprintf(g_ai.model, sizeof(g_ai.model), "%s",
             config->model[0] ? config->model : "apelsinbot");
    g_ai.cooldown_ms = config->cooldown_seconds * 1000U;
    InitializeCriticalSection(&g_ai.lock);
    g_ai.wake = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_ai.wake)
        g_ai.thread = CreateThread(NULL, 0, ai_worker_thread, NULL, 0, NULL);
    if (!g_ai.wake || !g_ai.thread)
    {
        if (g_ai.thread) CloseHandle(g_ai.thread);
        if (g_ai.wake) CloseHandle(g_ai.wake);
        DeleteCriticalSection(&g_ai.lock);
        memset(&g_ai, 0, sizeof(g_ai));
        return 0;
    }
    g_ai.started = 1;
    log_info("Local AI enabled: model=%s, cooldown=%u s",
             g_ai.model, config->cooldown_seconds);
    return 1;
}

int ai_chat_running(void)
{
    return g_ai.started;
}

AiChatStatus ai_chat_enqueue(const TwitchChatMessage *message,
                             const char *question,
                             const TwitchConfig *twitch)
{
    ULONGLONG now;
    size_t i, slot = AI_COOLDOWN_CAPACITY;
    AiJob *job;
    if (!g_ai.started) return AI_CHAT_DISABLED;
    if (!message || !question || !twitch ||
        !message->chatter_user_id[0] ||
        !message->chatter_user_login[0] || !question[0])
        return AI_CHAT_INVALID;
    if (strlen(question) > AI_QUESTION_BYTES) return AI_CHAT_TOO_LONG;
    now = GetTickCount64();
    EnterCriticalSection(&g_ai.lock);
    if (g_ai.stopping)
    {
        LeaveCriticalSection(&g_ai.lock);
        return AI_CHAT_DISABLED;
    }
    for (i = 0; i < AI_COOLDOWN_CAPACITY; ++i)
    {
        if (strcmp(g_ai.cooldowns[i].user_id,
                   message->chatter_user_id) == 0)
        {
            slot = i;
            if (now - g_ai.cooldowns[i].last_tick < g_ai.cooldown_ms)
            {
                LeaveCriticalSection(&g_ai.lock);
                return AI_CHAT_COOLDOWN;
            }
            break;
        }
        if (!g_ai.cooldowns[i].user_id[0] && slot == AI_COOLDOWN_CAPACITY)
            slot = i;
    }
    if (g_ai.count >= AI_QUEUE_CAPACITY)
    {
        LeaveCriticalSection(&g_ai.lock);
        return AI_CHAT_QUEUE_FULL;
    }
    if (slot == AI_COOLDOWN_CAPACITY)
    {
        slot = g_ai.next_cooldown_slot++ % AI_COOLDOWN_CAPACITY;
    }
    snprintf(g_ai.cooldowns[slot].user_id,
             sizeof(g_ai.cooldowns[slot].user_id), "%s",
             message->chatter_user_id);
    g_ai.cooldowns[slot].last_tick = now;
    job = &g_ai.jobs[g_ai.count++];
    memset(job, 0, sizeof(*job));
    snprintf(job->user_id, sizeof(job->user_id), "%s",
             message->chatter_user_id);
    snprintf(job->login, sizeof(job->login), "%s",
             message->chatter_user_login);
    snprintf(job->question, sizeof(job->question), "%s", question);
    job->twitch = *twitch;
    SetEvent(g_ai.wake);
    LeaveCriticalSection(&g_ai.lock);
    log_info("AI question queued from %s", message->chatter_user_login);
    return AI_CHAT_QUEUED;
}

void ai_chat_stop(void)
{
    if (!g_ai.started) return;
    EnterCriticalSection(&g_ai.lock);
    g_ai.stopping = 1;
    SetEvent(g_ai.wake);
    LeaveCriticalSection(&g_ai.lock);
    WaitForSingleObject(g_ai.thread, INFINITE);
    CloseHandle(g_ai.thread);
    CloseHandle(g_ai.wake);
    DeleteCriticalSection(&g_ai.lock);
    memset(&g_ai, 0, sizeof(g_ai));
    log_info("Local AI worker stopped");
}
