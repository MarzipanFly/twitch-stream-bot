#include "platform.h"

#include <windows.h>

BotResult platform_create_directory(
    const char *path
)
{
    DWORD attributes;

    if (path == NULL ||
        path[0] == '\0')
    {
        return BOT_ERR_FILE;
    }

    attributes =
        GetFileAttributesA(
            path
        );

    if (attributes !=
        INVALID_FILE_ATTRIBUTES)
    {
        if (attributes &
            FILE_ATTRIBUTE_DIRECTORY)
        {
            return BOT_OK;
        }

        return BOT_ERR_FILE;
    }

    if (CreateDirectoryA(
            path,
            NULL))
    {
        return BOT_OK;
    }

    if (GetLastError() ==
        ERROR_ALREADY_EXISTS)
    {
        return BOT_OK;
    }

    return BOT_ERR_FILE;
}
