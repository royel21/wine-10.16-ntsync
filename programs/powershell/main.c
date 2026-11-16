/*
 * Copyright 2017 Jactry Zeng for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(powershell);

#define MAX_TOKENS 64

/* splits a statement into words, honouring '...' and "..." quoting */
static int tokenize(WCHAR *str, WCHAR **tokens)
{
    int count = 0;
    WCHAR *p = str, *out;

    while (*p && count < MAX_TOKENS)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        tokens[count++] = out = p;
        while (*p && *p != ' ' && *p != '\t')
        {
            if (*p == '"' || *p == '\'')
            {
                WCHAR quote = *p++;
                while (*p && *p != quote) *out++ = *p++;
                if (*p) p++;
            }
            else *out++ = *p++;
        }
        if (*p) p++;
        *out = 0;
    }
    return count;
}

/* joins the values of a comma separated parameter list ("a","b" or a,b) with spaces */
static void append_arguments(WCHAR *buf, size_t size, const WCHAR *value)
{
    const WCHAR *p;

    for (p = value; *p; p++)
    {
        size_t len = wcslen(buf);
        if (len + 2 >= size) break;
        buf[len] = (*p == ',') ? ' ' : *p;
        buf[len + 1] = 0;
    }
}

/* Start-Process <file> [-ArgumentList ...] [-WorkingDirectory ...] [-Verb ...] [-Wait]
 * Programs already run with full rights in Wine, so -Verb RunAs just starts the program. */
static BOOL start_process(WCHAR **tokens, int count)
{
    WCHAR args[2048] = {0};
    const WCHAR *file = NULL, *dir = NULL;
    SHELLEXECUTEINFOW info = {sizeof(info)};
    BOOL wait = FALSE;
    int i;

    for (i = 1; i < count; i++)
    {
        const WCHAR *name = tokens[i];

        if (name[0] != '-')
        {
            if (!file) file = name;
            else append_arguments(args, ARRAY_SIZE(args), name);
            continue;
        }
        if (!wcsicmp(name, L"-Wait")) wait = TRUE;
        else if (!wcsicmp(name, L"-PassThru") || !wcsicmp(name, L"-NoNewWindow")) continue;
        else if (i + 1 < count)
        {
            const WCHAR *value = tokens[++i];

            if (!wcsicmp(name, L"-FilePath") || !wcsicmp(name, L"-File")) file = value;
            else if (!wcsicmp(name, L"-ArgumentList") || !wcsicmp(name, L"-Args"))
            {
                if (args[0]) append_arguments(args, ARRAY_SIZE(args), L",");
                append_arguments(args, ARRAY_SIZE(args), value);
                /* -ArgumentList a, b, c */
                while (i + 1 < count && tokens[i][wcslen(tokens[i]) - 1] == ',')
                    append_arguments(args, ARRAY_SIZE(args), tokens[++i]);
            }
            else if (!wcsicmp(name, L"-WorkingDirectory")) dir = value;
            else if (wcsicmp(name, L"-Verb") && wcsicmp(name, L"-WindowStyle"))
                WINE_FIXME("ignoring Start-Process parameter %s %s\n", wine_dbgstr_w(name), wine_dbgstr_w(value));
        }
    }

    if (!file) return FALSE;

    WINE_TRACE("starting %s %s in %s, wait %d\n", wine_dbgstr_w(file), wine_dbgstr_w(args), wine_dbgstr_w(dir), wait);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpFile = file;
    info.lpParameters = args[0] ? args : NULL;
    info.lpDirectory = dir;
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
    {
        WINE_WARN("failed to start %s, error %lu\n", wine_dbgstr_w(file), GetLastError());
        return FALSE;
    }
    if (info.hProcess)
    {
        if (wait) WaitForSingleObject(info.hProcess, INFINITE);
        CloseHandle(info.hProcess);
    }
    return TRUE;
}

/* runs the statements of a -Command string; only Start-Process is understood */
static void run_command(WCHAR *command)
{
    WCHAR *statement = command, *next;

    for (; statement; statement = next)
    {
        WCHAR *tokens[MAX_TOKENS];
        int count;

        if ((next = wcschr(statement, ';'))) *next++ = 0;
        while (*statement == ' ' || *statement == '&') statement++;
        if (*statement == '{')
        {
            WCHAR *end = wcsrchr(statement, '}');
            statement++;
            if (end) *end = 0;
        }
        if (!(count = tokenize(statement, tokens))) continue;

        if (!wcsicmp(tokens[0], L"Start-Process") || !wcsicmp(tokens[0], L"start") || !wcsicmp(tokens[0], L"saps"))
            start_process(tokens, count);
        else
            WINE_FIXME("unsupported command %s\n", wine_dbgstr_w(tokens[0]));
    }
}

int __cdecl wmain(int argc, WCHAR *argv[])
{
    int i;

    WINE_FIXME("stub.\n");
    for (i = 0; i < argc; i++)
    {
        WINE_FIXME("argv[%d] %s\n", i, wine_dbgstr_w(argv[i]));
        if ((!wcsicmp(argv[i], L"-command") || !wcsicmp(argv[i], L"-c")) && i < argc - 1)
        {
            WCHAR *command;
            size_t len = 0;
            int j;

            if (!wcscmp(argv[i + 1], L"-"))
            {
                char line[4096], *p;

                ++i;
                while (fgets(line, sizeof(line), stdin))
                {
                    WINE_FIXME("command %s.\n", debugstr_a(line));
                    p = line;
                    while (*p && !isspace(*p)) ++p;
                    *p = 0;
                    if (!stricmp(line, "exit"))
                        break;
                }
                continue;
            }

            /* everything after -Command is the command text */
            for (j = i + 1; j < argc; j++) len += wcslen(argv[j]) + 1;
            if (!(command = calloc(len + 1, sizeof(WCHAR)))) break;
            for (j = i + 1; j < argc; j++)
            {
                if (j > i + 1) wcscat(command, L" ");
                wcscat(command, argv[j]);
            }
            WINE_TRACE("command %s\n", wine_dbgstr_w(command));
            run_command(command);
            free(command);
            break;
        }
    }
    return 0;
}
