#include "token_store.h"

#include "logger.h"

#include <windows.h>
#include <dpapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOKEN_STORE_FILE "data/twitch_tokens.dat"
#define TOKEN_STORE_MAGIC   0x4B544254UL
#define TOKEN_STORE_VERSION 1UL

typedef struct
{
    DWORD magic;
    DWORD version;

    TwitchAuthToken token;

} TokenStorePayload;

BotResult token_store_save(
    const TwitchAuthToken *token
)
{
    TokenStorePayload payload;

    DATA_BLOB input_blob;
    DATA_BLOB encrypted_blob;

    FILE *file = NULL;

    size_t written;

    if (token == NULL)
    {
        return BOT_ERR_STORAGE;
    }

    if (token->access_token[0] == '\0' ||
        token->refresh_token[0] == '\0')
    {
        log_error(
            "Cannot save empty Twitch OAuth tokens"
        );

        return BOT_ERR_STORAGE;
    }

    memset(
        &payload,
        0,
        sizeof(payload)
    );

    payload.magic =
        TOKEN_STORE_MAGIC;

    payload.version =
        TOKEN_STORE_VERSION;

    memcpy(
        &payload.token,
        token,
        sizeof(payload.token)
    );

    input_blob.pbData =
        (BYTE *)&payload;

    input_blob.cbData =
        (DWORD)sizeof(payload);

    memset(
        &encrypted_blob,
        0,
        sizeof(encrypted_blob)
    );

    /*
     * Windows DPAPI binds the encrypted data to
     * the current Windows user profile.
     */
    if (!CryptProtectData(
            &input_blob,
            L"TwitchBot OAuth tokens",
            NULL,
            NULL,
            NULL,
            CRYPTPROTECT_UI_FORBIDDEN,
            &encrypted_blob))
    {
        log_error(
            "CryptProtectData failed: %lu",
            GetLastError()
        );

        SecureZeroMemory(
            &payload,
            sizeof(payload)
        );

        return BOT_ERR_STORAGE;
    }

    file =
        fopen(
            TOKEN_STORE_FILE,
            "wb"
        );

    if (file == NULL)
    {
        log_error(
            "Failed to open token storage file for writing"
        );

        SecureZeroMemory(
            &payload,
            sizeof(payload)
        );

        SecureZeroMemory(
            encrypted_blob.pbData,
            encrypted_blob.cbData
        );

        LocalFree(
            encrypted_blob.pbData
        );

        return BOT_ERR_FILE;
    }

    written =
        fwrite(
            encrypted_blob.pbData,
            1,
            encrypted_blob.cbData,
            file
        );

    fclose(file);

    if (written !=
        encrypted_blob.cbData)
    {
        log_error(
            "Failed to write complete token storage file"
        );

        SecureZeroMemory(
            &payload,
            sizeof(payload)
        );

        SecureZeroMemory(
            encrypted_blob.pbData,
            encrypted_blob.cbData
        );

        LocalFree(
            encrypted_blob.pbData
        );

        return BOT_ERR_FILE;
    }

    SecureZeroMemory(
        &payload,
        sizeof(payload)
    );

    SecureZeroMemory(
        encrypted_blob.pbData,
        encrypted_blob.cbData
    );

    LocalFree(
        encrypted_blob.pbData
    );

    log_info(
        "Twitch OAuth tokens saved securely"
    );

    return BOT_OK;
}

BotResult token_store_load(
    TwitchAuthToken *token
)
{
    FILE *file = NULL;

    long file_size;

    BYTE *buffer = NULL;

    size_t read_size;

    DATA_BLOB encrypted_blob;
    DATA_BLOB decrypted_blob;

    TokenStorePayload payload;

    if (token == NULL)
    {
        return BOT_ERR_STORAGE;
    }

    memset(
        token,
        0,
        sizeof(*token)
    );

    file =
        fopen(
            TOKEN_STORE_FILE,
            "rb"
        );

    /*
     * Missing file is normal on the first run.
     */
    if (file == NULL)
    {
        return BOT_ERR_FILE;
    }

    if (fseek(
            file,
            0,
            SEEK_END) != 0)
    {
        fclose(file);
        return BOT_ERR_FILE;
    }

    file_size =
        ftell(file);

    if (file_size <= 0)
    {
        fclose(file);
        return BOT_ERR_STORAGE;
    }

    rewind(file);

    buffer =
        (BYTE *)malloc(
            (size_t)file_size
        );

    if (buffer == NULL)
    {
        fclose(file);
        return BOT_ERR_STORAGE;
    }

    read_size =
        fread(
            buffer,
            1,
            (size_t)file_size,
            file
        );

    fclose(file);

    if (read_size !=
        (size_t)file_size)
    {
        SecureZeroMemory(
            buffer,
            (SIZE_T)file_size
        );

        free(buffer);

        return BOT_ERR_FILE;
    }

    encrypted_blob.pbData =
        buffer;

    encrypted_blob.cbData =
        (DWORD)file_size;

    memset(
        &decrypted_blob,
        0,
        sizeof(decrypted_blob)
    );

    if (!CryptUnprotectData(
            &encrypted_blob,
            NULL,
            NULL,
            NULL,
            NULL,
            CRYPTPROTECT_UI_FORBIDDEN,
            &decrypted_blob))
    {
        log_error(
            "CryptUnprotectData failed: %lu",
            GetLastError()
        );

        SecureZeroMemory(
            buffer,
            (SIZE_T)file_size
        );

        free(buffer);

        return BOT_ERR_STORAGE;
    }

    SecureZeroMemory(
        buffer,
        (SIZE_T)file_size
    );

    free(buffer);

    if (decrypted_blob.cbData !=
        sizeof(TokenStorePayload))
    {
        log_error(
            "Invalid Twitch token storage size"
        );

        SecureZeroMemory(
            decrypted_blob.pbData,
            decrypted_blob.cbData
        );

        LocalFree(
            decrypted_blob.pbData
        );

        return BOT_ERR_STORAGE;
    }

    memcpy(
        &payload,
        decrypted_blob.pbData,
        sizeof(payload)
    );

    SecureZeroMemory(
        decrypted_blob.pbData,
        decrypted_blob.cbData
    );

    LocalFree(
        decrypted_blob.pbData
    );

    if (payload.magic !=
            TOKEN_STORE_MAGIC ||
        payload.version !=
            TOKEN_STORE_VERSION)
    {
        log_error(
            "Invalid Twitch token storage format"
        );

        SecureZeroMemory(
            &payload,
            sizeof(payload)
        );

        return BOT_ERR_STORAGE;
    }

    if (payload.token.access_token[0] == '\0' ||
        payload.token.refresh_token[0] == '\0')
    {
        log_error(
            "Stored Twitch OAuth tokens are empty"
        );

        SecureZeroMemory(
            &payload,
            sizeof(payload)
        );

        return BOT_ERR_STORAGE;
    }

    memcpy(
        token,
        &payload.token,
        sizeof(*token)
    );

    SecureZeroMemory(
        &payload,
        sizeof(payload)
    );

    return BOT_OK;
}

BotResult token_store_delete(void)
{
    DWORD attributes;

    attributes =
        GetFileAttributesA(
            TOKEN_STORE_FILE
        );

    if (attributes ==
        INVALID_FILE_ATTRIBUTES)
    {
        return BOT_OK;
    }

    if (!DeleteFileA(
            TOKEN_STORE_FILE))
    {
        log_error(
            "Failed to delete Twitch token storage: %lu",
            GetLastError()
        );

        return BOT_ERR_FILE;
    }

    return BOT_OK;
}
