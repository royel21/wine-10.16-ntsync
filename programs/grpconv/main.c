/*
 * Program group converter
 *
 * Old installers write their Start menu items to %windir%\setup.ini and run
 * "grpconv -o", which turns the [progman.groups] entries into shortcuts.
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

#define COBJMACROS

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(grpconv);

#define MAX_FIELDS 8

/* split one item line: "Title","Command line","Icon file",Icon index,... ,"Work dir" */
static int split_fields( WCHAR *line, WCHAR **fields )
{
    int count = 0;
    WCHAR *p = line;

    while (count < MAX_FIELDS)
    {
        WCHAR *out;

        while (*p == ' ' || *p == '\t') p++;
        fields[count++] = out = p;
        if (*p == '"')
        {
            p++;
            while (*p && *p != '"') *out++ = *p++;
            if (*p == '"') p++;
            while (*p && *p != ',') p++;
        }
        else
        {
            while (*p && *p != ',') *out++ = *p++;
            while (out > fields[count - 1] && (out[-1] == ' ' || out[-1] == '\t')) out--;
        }
        if (!*p)
        {
            *out = 0;
            break;
        }
        *out = 0;
        p++;
    }
    return count;
}

static void remove_bad_chars( WCHAR *name )
{
    for (; *name; name++) if (wcschr( L"\\/:*?\"<>|", *name )) *name = '_';
}

static void create_link( const WCHAR *dir, WCHAR *line )
{
    WCHAR *fields[MAX_FIELDS] = {0}, path[MAX_PATH], link[MAX_PATH], workdir[MAX_PATH], *args = NULL;
    IShellLinkW *sl;
    IPersistFile *pf;
    int count;

    count = split_fields( line, fields );
    if (count < 2 || !fields[0][0] || !fields[1][0]) return;

    /* the command line may hold arguments after the program path */
    if (GetFileAttributesW( fields[1] ) == INVALID_FILE_ATTRIBUTES)
    {
        WCHAR *space = fields[1];
        while ((space = wcschr( space, ' ' )))
        {
            *space = 0;
            if (GetFileAttributesW( fields[1] ) != INVALID_FILE_ATTRIBUTES)
            {
                args = space + 1;
                break;
            }
            *space++ = ' ';
        }
    }
    lstrcpynW( path, fields[1], MAX_PATH );

    if (count > 5 && fields[5][0]) lstrcpynW( workdir, fields[5], MAX_PATH );
    else
    {
        lstrcpynW( workdir, path, MAX_PATH );
        PathRemoveFileSpecW( workdir );
    }

    remove_bad_chars( fields[0] );
    if (!PathCombineW( link, dir, fields[0] )) return;
    lstrcatW( link, L".lnk" );

    if (FAILED(CoCreateInstance( &CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                 &IID_IShellLinkW, (void **)&sl )))
        return;
    IShellLinkW_SetPath( sl, path );
    if (args) IShellLinkW_SetArguments( sl, args );
    IShellLinkW_SetWorkingDirectory( sl, workdir );
    if (count > 2 && fields[2][0])
        IShellLinkW_SetIconLocation( sl, fields[2], count > 3 ? wcstol( fields[3], NULL, 10 ) : 0 );
    IShellLinkW_SetDescription( sl, fields[0] );

    if (SUCCEEDED(IShellLinkW_QueryInterface( sl, &IID_IPersistFile, (void **)&pf )))
    {
        HRESULT hr = IPersistFile_Save( pf, link, TRUE );
        TRACE( "%s -> %s: %#lx\n", debugstr_w(link), debugstr_w(path), hr );
        IPersistFile_Release( pf );
    }
    IShellLinkW_Release( sl );
}

static void convert_setup_ini( const WCHAR *ini )
{
    static const DWORD size = 32768;
    WCHAR *groups, *items, *group, *item, programs[MAX_PATH], dir[MAX_PATH];

    if (FAILED(SHGetFolderPathW( NULL, CSIDL_PROGRAMS | CSIDL_FLAG_CREATE, NULL, 0, programs ))) return;
    if (!(groups = malloc( size * sizeof(WCHAR) ))) return;
    if (!(items = malloc( size * sizeof(WCHAR) )))
    {
        free( groups );
        return;
    }

    GetPrivateProfileSectionW( L"progman.groups", groups, size, ini );
    for (group = groups; *group; group += lstrlenW( group ) + 1)
    {
        WCHAR *name = wcschr( group, '=' );

        if (!name) continue;
        *name++ = 0;
        TRACE( "group %s %s\n", debugstr_w(group), debugstr_w(name) );

        if (!PathCombineW( dir, programs, name )) continue;
        SHCreateDirectoryExW( NULL, dir, NULL );

        GetPrivateProfileSectionW( group, items, size, ini );
        for (item = items; *item; item += lstrlenW( item ) + 1)
        {
            WCHAR line[1024];
            lstrcpynW( line, item, ARRAY_SIZE(line) );
            create_link( dir, line );
        }
    }

    free( items );
    free( groups );
}

int __cdecl wmain( int argc, WCHAR *argv[] )
{
    WCHAR ini[MAX_PATH];

    if (!GetWindowsDirectoryW( ini, MAX_PATH - ARRAY_SIZE(L"\\setup.ini") )) return 1;
    lstrcatW( ini, L"\\setup.ini" );
    if (GetFileAttributesW( ini ) == INVALID_FILE_ATTRIBUTES) return 0;

    CoInitialize( NULL );
    convert_setup_ini( ini );
    CoUninitialize();
    return 0;
}
