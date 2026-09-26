#include "platform.h"

#include <windows.h>

BotResult platform_create_directory(const char *path)
{
	DWORD attributes;
	DWORD error_code;

	if (path == NULL || path[0] == '\0')
	{
		return BOT_ERR_FILE;
	}

	/* Проверяем, существует ли объект
	 * с таким путем
	 */
	attributes = GetFileAttributesA(path);

	if (attributes != INVALID_FILE_ATTRIBUTES)
	{
		/*
		 * Объект существует.
		 * Проверяем, что это действительно директория
		 */
		if (attributes & FILE_ATTRIBUTE_DIRECTORY)
		{
			return BOT_OK;
		}

		/*
		 * Например, существует файл "logs",
		 * а мы хотим создать папку "logs".
		 */
		return BOT_ERR_FILE;
	}

	/*
	 * Директории нет - пробуем создать
	 */
	if (CreateDirectoryA(path, NULL))
	{
		return BOT_OK;
	}

	error_code = GetLastError();

	/*
	 * Теоритечиски директории могла появиться
	 * между GetFileAttributesA() и createDirectoryA()
	 */
	if(error_code == ERROR_ALREADY_EXISTS)
	{
		attributes = GetFileAttributesA(path);

		if(attributes != INVALID_FILE_ATTRIBUTES &&
				(attributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			return BOT_OK;
		}
	}

	return BOT_ERR_FILE;
}
