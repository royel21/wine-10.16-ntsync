/*
 * msvcrt40 implementation
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
#include <mbctype.h>

#include "windef.h"

/*********************************************************************
 *		_mbclen (MSVCRT40.@)
 *
 * Unlike later msvcrt versions, this one only looks at the lead byte;
 * the following byte is not checked for NUL. Some applications pass a
 * buffer whose second byte is not set yet.
 */
size_t CDECL MSVCRT40__mbclen(const unsigned char *str)
{
    return _ismbblead(*str) ? 2 : 1;
}
