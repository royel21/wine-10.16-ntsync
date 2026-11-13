/*
 * DirectX setup library
 *
 * Old games ship a DirectX 3 dsetup.dll that reads the 4-byte InstalledVersion
 * value of DirectX 3 and fails on the 8-byte value written since DirectX 5,
 * refusing to install on any NT system. DirectX is part of Wine, so report it
 * as installed and up to date.
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dsetup);

#define DSETUPERR_SUCCESS 0

typedef DWORD (WINAPI *DSETUP_CALLBACK)(DWORD reason, DWORD msg_type, char *message, char *name, void *info);

INT WINAPI DirectXSetupA( HWND hwnd, char *root_path, DWORD flags )
{
    TRACE( "%p, %s, %#lx: DirectX is already installed\n", hwnd, debugstr_a(root_path), flags );
    return DSETUPERR_SUCCESS;
}

INT WINAPI DirectXSetupW( HWND hwnd, WCHAR *root_path, DWORD flags )
{
    TRACE( "%p, %s, %#lx: DirectX is already installed\n", hwnd, debugstr_w(root_path), flags );
    return DSETUPERR_SUCCESS;
}

INT WINAPI DirectXDeviceDriverSetupA( HWND hwnd, char *driver_class, char *inf_path, char *driver_path, DWORD flags )
{
    TRACE( "%p, %s, %s, %s, %#lx\n", hwnd, debugstr_a(driver_class), debugstr_a(inf_path),
           debugstr_a(driver_path), flags );
    return DSETUPERR_SUCCESS;
}

INT WINAPI DirectXDeviceDriverSetupW( HWND hwnd, WCHAR *driver_class, WCHAR *inf_path, WCHAR *driver_path, DWORD flags )
{
    TRACE( "%p, %s, %s, %s, %#lx\n", hwnd, debugstr_w(driver_class), debugstr_w(inf_path),
           debugstr_w(driver_path), flags );
    return DSETUPERR_SUCCESS;
}

INT WINAPI DirectXRegisterApplicationA( HWND hwnd, void *info )
{
    FIXME( "%p, %p: stub\n", hwnd, info );
    return TRUE;
}

INT WINAPI DirectXRegisterApplicationW( HWND hwnd, void *info )
{
    FIXME( "%p, %p: stub\n", hwnd, info );
    return TRUE;
}

INT WINAPI DirectXUnRegisterApplication( HWND hwnd, GUID *guid )
{
    FIXME( "%p, %s: stub\n", hwnd, debugstr_guid(guid) );
    return TRUE;
}

INT WINAPI DirectXSetupGetVersion( DWORD *version, DWORD *revision )
{
    TRACE( "%p, %p\n", version, revision );
    /* DirectX 9.0c */
    if (version) *version = 0x00040009;
    if (revision) *revision = 0x00000904;
    return TRUE;
}

INT WINAPI DirectXSetupSetCallback( DSETUP_CALLBACK callback )
{
    TRACE( "%p\n", callback );
    return DSETUPERR_SUCCESS;
}

BOOL WINAPI DirectXSetupIsJapan(void)
{
    return GetACP() == 932;
}

BOOL WINAPI DirectXSetupIsJapanNec(void)
{
    return FALSE;
}
