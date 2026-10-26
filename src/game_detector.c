#include "game_detector.h"

#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

typedef struct
{
    GameDetectorWindow best;
    HWND foreground;
    int found;
} DetectorContext;

static int detector_text_equal_ci(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right)
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int detector_ignored_process(const char *name)
{
    static const char *ignored[] = {
        "obs64.exe", "obs32.exe", "twitchbot.exe", "explorer.exe",
        "chrome.exe", "msedge.exe", "firefox.exe", "opera.exe",
        "brave.exe", "browser.exe", "yandex.exe", "discord.exe",
        "steam.exe", "steamwebhelper.exe", "epicgameslauncher.exe",
        "battle.net.exe", "upc.exe", "ubisoftconnect.exe",
        "eadesktop.exe", "ealauncher.exe", "riotclientservices.exe",
        "applicationframehost.exe", "searchhost.exe",
        "startmenuexperiencehost.exe", "shellexperiencehost.exe",
        "textinputhost.exe", "taskmgr.exe", "cmd.exe", "powershell.exe",
        "windowsterminal.exe", "qtcreator.exe", "devenv.exe", "code.exe",
        "vlc.exe", "mpv.exe", "potplayer.exe", "wmplayer.exe",
        "music.ui.exe", "spotify.exe", "gamebar.exe",
        NULL
    };
    size_t i;
    for (i = 0; ignored[i]; ++i)
        if (detector_text_equal_ci(name, ignored[i])) return 1;
    return 0;
}

static int detector_wide_to_utf8(const WCHAR *input, char *output, size_t output_size)
{
    int required;
    if (!input || !output || output_size == 0) return 0;
    output[0] = '\0';
    required = WideCharToMultiByte(CP_UTF8, 0, input, -1, NULL, 0, NULL, NULL);
    if (required <= 0 || (size_t)required > output_size) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, input, -1, output,
                               (int)output_size, NULL, NULL) > 0;
}

static int detector_process_name(DWORD process_id, char *output, size_t output_size)
{
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    int found = 0;

    if (!process_id || !output || output_size == 0) return 0;
    output[0] = '\0';

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == process_id)
            {
                found = detector_wide_to_utf8(entry.szExeFile, output, output_size);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static int detector_is_fullscreen(HWND window, RECT *window_rect, unsigned long long *area)
{
    HMONITOR monitor;
    MONITORINFO info;
    LONG width, height;
    LONG monitor_width, monitor_height;
    LONG tolerance_x, tolerance_y;

    if (!GetWindowRect(window, window_rect)) return 0;
    monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!monitor) return 0;

    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return 0;

    width = window_rect->right - window_rect->left;
    height = window_rect->bottom - window_rect->top;
    monitor_width = info.rcMonitor.right - info.rcMonitor.left;
    monitor_height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width <= 0 || height <= 0 || monitor_width <= 0 || monitor_height <= 0)
        return 0;

    tolerance_x = monitor_width / 100;
    tolerance_y = monitor_height / 100;
    if (tolerance_x < 16) tolerance_x = 16;
    if (tolerance_y < 16) tolerance_y = 16;

    /* Accept exclusive and borderless fullscreen, but not ordinary maximized windows. */
    if (window_rect->left > info.rcMonitor.left + tolerance_x ||
        window_rect->top > info.rcMonitor.top + tolerance_y ||
        window_rect->right < info.rcMonitor.right - tolerance_x ||
        window_rect->bottom < info.rcMonitor.bottom - tolerance_y)
        return 0;

    *area = (unsigned long long)width * (unsigned long long)height;
    return 1;
}

static BOOL CALLBACK detector_enum_window(HWND window, LPARAM parameter)
{
    DetectorContext *context = (DetectorContext *)parameter;
    DWORD process_id = 0;
    WCHAR title_wide[512] = L"";
    char title[CONFIG_STRING_SIZE] = "";
    char process_name[CONFIG_STRING_SIZE] = "";
    RECT rect;
    unsigned long long area = 0;
    LONG style;

    if (!context || !IsWindowVisible(window) || IsIconic(window))
        return TRUE;

    style = GetWindowLongW(window, GWL_EXSTYLE);
    if ((style & WS_EX_TOOLWINDOW) != 0 || GetWindow(window, GW_OWNER) != NULL)
        return TRUE;

    if (!detector_is_fullscreen(window, &rect, &area))
        return TRUE;

    (void)GetWindowThreadProcessId(window, &process_id);
    if (!detector_process_name(process_id, process_name, sizeof(process_name)) ||
        detector_ignored_process(process_name))
        return TRUE;

    (void)GetWindowTextW(window, title_wide,
                         (int)(sizeof(title_wide) / sizeof(title_wide[0])));
    if (title_wide[0])
        (void)detector_wide_to_utf8(title_wide, title, sizeof(title));

    /*
     * Prefer the foreground fullscreen window. If a fullscreen game remains
     * visible while OBS is briefly foregrounded, retain the largest candidate.
     */
    if (!context->found ||
        window == context->foreground ||
        (context->best.window != context->foreground && area > context->best.area))
    {
        context->best.window = window;
        context->best.process_id = process_id;
        context->best.area = area;
        context->best.is_foreground = (window == context->foreground);
        snprintf(context->best.title, sizeof(context->best.title), "%s",
                 title[0] ? title : process_name);
        snprintf(context->best.process_name, sizeof(context->best.process_name),
                 "%s", process_name);
        context->found = 1;
    }

    return TRUE;
}

int game_detector_find_fullscreen(GameDetectorWindow *result)
{
    DetectorContext context;
    BOOL enumerated;

    if (!result) return -1;
    memset(result, 0, sizeof(*result));
    memset(&context, 0, sizeof(context));
    context.foreground = GetForegroundWindow();

    enumerated = EnumWindows(detector_enum_window, (LPARAM)&context);
    if (!enumerated) return -1;
    if (!context.found) return 0;

    *result = context.best;
    return 1;
}
