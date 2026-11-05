/*
 * NT synchronization primitives through /dev/ntsync or a userspace library
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

#ifndef __WINE_WINE_NTSYNC_SHIM_H
#define __WINE_WINE_NTSYNC_SHIM_H

#ifdef __linux__

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef HAVE_LINUX_NTSYNC_H
# include <linux/ntsync.h>
#endif

#ifndef NTSYNC_IOC_EVENT_READ

/* Kernel interface (include/uapi/linux/ntsync.h), for headers older than Linux 6.14 */
#include <linux/types.h>

struct ntsync_sem_args
{
    __u32 count;
    __u32 max;
};

struct ntsync_mutex_args
{
    __u32 owner;
    __u32 count;
};

struct ntsync_event_args
{
    __u32 manual;
    __u32 signaled;
};

#define NTSYNC_WAIT_REALTIME 0x1

struct ntsync_wait_args
{
    __u64 timeout;
    __u64 objs;
    __u32 count;
    __u32 index;
    __u32 flags;
    __u32 owner;
    __u32 alert;
    __u32 pad;
};

#define NTSYNC_MAX_WAIT_COUNT 64

#define NTSYNC_IOC_CREATE_SEM   _IOW ('N', 0x80, struct ntsync_sem_args)
#define NTSYNC_IOC_WAIT_ANY     _IOWR('N', 0x82, struct ntsync_wait_args)
#define NTSYNC_IOC_WAIT_ALL     _IOWR('N', 0x83, struct ntsync_wait_args)
#define NTSYNC_IOC_CREATE_MUTEX _IOW ('N', 0x84, struct ntsync_mutex_args)
#define NTSYNC_IOC_CREATE_EVENT _IOW ('N', 0x87, struct ntsync_event_args)

#define NTSYNC_IOC_SEM_RELEASE  _IOWR('N', 0x81, __u32)
#define NTSYNC_IOC_MUTEX_UNLOCK _IOWR('N', 0x85, struct ntsync_mutex_args)
#define NTSYNC_IOC_MUTEX_KILL   _IOW ('N', 0x86, __u32)
#define NTSYNC_IOC_EVENT_SET    _IOR ('N', 0x88, __u32)
#define NTSYNC_IOC_EVENT_RESET  _IOR ('N', 0x89, __u32)
#define NTSYNC_IOC_EVENT_PULSE  _IOR ('N', 0x8a, __u32)
#define NTSYNC_IOC_SEM_READ     _IOR ('N', 0x8b, struct ntsync_sem_args)
#define NTSYNC_IOC_MUTEX_READ   _IOR ('N', 0x8c, struct ntsync_mutex_args)
#define NTSYNC_IOC_EVENT_READ   _IOR ('N', 0x8d, struct ntsync_event_args)

#endif /* NTSYNC_IOC_EVENT_READ */

/* Userspace ntsync (libntsync_android.so) for systems without /dev/ntsync.
 * Objects are u32 handles in a shared mapping instead of fds; the server
 * reports this mode to clients by sending this value as inproc_device. */
#define NTSYNC_ANDROID_USED_BY_SERVER 0x7eadfe01
#define NTSYNC_ANDROID_LIBNAME "libntsync_android.so"
#define NTSYNC_ANDROID_SHM_NAME "ntsync.shm"

struct ntsync_android_funcs
{
    int32_t (*init)(const char *path);
    int32_t (*sweep_dead)(void);
    int32_t (*create_sem)(uint32_t *handle, const struct ntsync_sem_args *args);
    int32_t (*create_mutex)(uint32_t *handle, const struct ntsync_mutex_args *args);
    int32_t (*create_event)(uint32_t *handle, const struct ntsync_event_args *args);
    int32_t (*close)(uint32_t handle);
    int32_t (*sem_release)(uint32_t handle, uint32_t *count);
    int32_t (*sem_read)(uint32_t handle, struct ntsync_sem_args *args);
    int32_t (*mutex_unlock)(uint32_t handle, struct ntsync_mutex_args *args);
    int32_t (*mutex_kill)(uint32_t handle, uint32_t owner);
    int32_t (*mutex_read)(uint32_t handle, struct ntsync_mutex_args *args);
    int32_t (*event_set)(uint32_t handle, uint32_t *prev);
    int32_t (*event_reset)(uint32_t handle, uint32_t *prev);
    int32_t (*event_pulse)(uint32_t handle, uint32_t *prev);
    int32_t (*event_read)(uint32_t handle, struct ntsync_event_args *args);
    int32_t (*wait_any)(struct ntsync_wait_args *args);
    int32_t (*wait_all)(struct ntsync_wait_args *args);
};

/* set by ntsync_android_load(), defined once per binary */
extern const struct ntsync_android_funcs *ntsync_android;

/* Load the library and open the shared region; returns 0 on success. */
static inline int ntsync_android_load( const char *shm_path )
{
    static struct ntsync_android_funcs funcs;
    const char *libname = getenv( "WINE_NTSYNC_ANDROID_LIB" );
    void *lib;

    if (!libname) libname = NTSYNC_ANDROID_LIBNAME;
    if (!(lib = dlopen( libname, RTLD_NOW | RTLD_LOCAL ))) return -1;

#define LOAD_FUNC(f) if (!(*(void **)&funcs.f = dlsym( lib, "ntsync_" #f ))) goto failed;
    LOAD_FUNC( init );
    LOAD_FUNC( sweep_dead );
    LOAD_FUNC( create_sem );
    LOAD_FUNC( create_mutex );
    LOAD_FUNC( create_event );
    LOAD_FUNC( close );
    LOAD_FUNC( sem_release );
    LOAD_FUNC( sem_read );
    LOAD_FUNC( mutex_unlock );
    LOAD_FUNC( mutex_kill );
    LOAD_FUNC( mutex_read );
    LOAD_FUNC( event_set );
    LOAD_FUNC( event_reset );
    LOAD_FUNC( event_pulse );
    LOAD_FUNC( event_read );
    LOAD_FUNC( wait_any );
    LOAD_FUNC( wait_all );
#undef LOAD_FUNC

    if (funcs.init( shm_path ) < 0) goto failed;
    ntsync_android = &funcs;
    return 0;

failed:
    dlclose( lib );
    return -1;
}

/* Same contract as ioctl() on an ntsync fd: returns -1 and sets errno on
 * failure, and the new object for the create requests. */
static inline int ntsync_ioctl( int obj, unsigned long request, void *arg )
{
    const struct ntsync_android_funcs *funcs = ntsync_android;
    uint32_t handle = 0;
    int32_t ret;

    if (!funcs) return ioctl( obj, request, arg );

    switch (request)
    {
    case NTSYNC_IOC_CREATE_SEM:   ret = funcs->create_sem( &handle, arg ); break;
    case NTSYNC_IOC_CREATE_MUTEX: ret = funcs->create_mutex( &handle, arg ); break;
    case NTSYNC_IOC_CREATE_EVENT: ret = funcs->create_event( &handle, arg ); break;
    case NTSYNC_IOC_SEM_RELEASE:  ret = funcs->sem_release( obj, arg ); break;
    case NTSYNC_IOC_SEM_READ:     ret = funcs->sem_read( obj, arg ); break;
    case NTSYNC_IOC_MUTEX_UNLOCK: ret = funcs->mutex_unlock( obj, arg ); break;
    case NTSYNC_IOC_MUTEX_KILL:   ret = funcs->mutex_kill( obj, *(__u32 *)arg ); break;
    case NTSYNC_IOC_MUTEX_READ:   ret = funcs->mutex_read( obj, arg ); break;
    case NTSYNC_IOC_EVENT_SET:    ret = funcs->event_set( obj, arg ); break;
    case NTSYNC_IOC_EVENT_RESET:  ret = funcs->event_reset( obj, arg ); break;
    case NTSYNC_IOC_EVENT_PULSE:  ret = funcs->event_pulse( obj, arg ); break;
    case NTSYNC_IOC_EVENT_READ:   ret = funcs->event_read( obj, arg ); break;
    case NTSYNC_IOC_WAIT_ANY:     ret = funcs->wait_any( arg ); break;
    case NTSYNC_IOC_WAIT_ALL:     ret = funcs->wait_all( arg ); break;
    default:                      ret = -ENOTTY; break;
    }

    if (ret < 0)
    {
        errno = -ret;
        return -1;
    }
    return handle;
}

/* Destroy an object created by the server. */
static inline void ntsync_close_obj( int obj )
{
    if (ntsync_android) ntsync_android->close( obj );
    else close( obj );
}

#endif /* __linux__ */

#endif /* __WINE_WINE_NTSYNC_SHIM_H */
