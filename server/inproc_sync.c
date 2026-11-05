/*
 * In-process synchronization primitives
 *
 * Copyright (C) 2021-2022 Elizabeth Figura for CodeWeavers
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

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"

#include "file.h"
#include "handle.h"
#include "process.h"
#include "request.h"
#include "thread.h"
#include "user.h"

#include "wine/ntsync_shim.h"

#ifdef NTSYNC_IOC_EVENT_READ

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

/* any value >= 0, never passed to the library or sent to clients */
#define NTSYNC_ANDROID_DEVICE 0x7fffffff

const struct ntsync_android_funcs *ntsync_android;

int get_inproc_device_fd(void)
{
    static int fd = -2;

    if (fd == -2)
    {
        const char *env = getenv( "WINENTSYNC" );
        char *path;

        if (env && !atoi( env ))
        {
            fd = -1;
            fprintf( stderr, "ntsync: disabled by WINENTSYNC=0, using server sync.\n" );
        }
        else if ((fd = open( "/dev/ntsync", O_CLOEXEC | O_RDONLY )) < 0 &&
                 asprintf( &path, "%s/%s", server_dir, NTSYNC_ANDROID_SHM_NAME ) != -1)
        {
            if (!ntsync_android_load( path ))
            {
                /* objects left behind by a previous server for this prefix */
                ntsync_android->sweep_dead();
                fd = NTSYNC_ANDROID_DEVICE;
                fprintf( stderr, "ntsync: up and running (userspace, %s).\n", path );
            }
            else fprintf( stderr, "ntsync: %s not found, using server sync.\n", NTSYNC_ANDROID_LIBNAME );
            free( path );
        }
        else if (fd >= 0) fprintf( stderr, "ntsync: up and running.\n" );
    }
    return fd;
}

int inproc_sync_is_userspace(void)
{
    return ntsync_android != NULL;
}

struct inproc_sync
{
    struct object          obj;  /* object header */
    enum inproc_sync_type  type;
    int                    fd;
    struct list            entry;
};

static struct list inproc_mutexes = LIST_INIT( inproc_mutexes );

static void inproc_sync_dump( struct object *obj, int verbose );
static int inproc_sync_signal( struct object *obj, unsigned int access, int signal );
static void inproc_sync_destroy( struct object *obj );

static const struct object_ops inproc_sync_ops =
{
    sizeof(struct inproc_sync), /* size */
    &no_type,                   /* type */
    inproc_sync_dump,           /* dump */
    no_add_queue,               /* add_queue */
    NULL,                       /* remove_queue */
    NULL,                       /* signaled */
    NULL,                       /* satisfied */
    inproc_sync_signal,         /* signal */
    no_get_fd,                  /* get_fd */
    default_get_sync,           /* get_sync */
    default_map_access,         /* map_access */
    default_get_sd,             /* get_sd */
    default_set_sd,             /* set_sd */
    default_get_full_name,      /* get_full_name */
    no_lookup_name,             /* lookup_name */
    directory_link_name,        /* link_name */
    default_unlink_name,        /* unlink_name */
    no_open_file,               /* open_file */
    no_kernel_obj_list,         /* get_kernel_obj_list */
    no_close_handle,            /* close_handle */
    inproc_sync_destroy,        /* destroy */
};

int get_inproc_sync_fd( struct inproc_sync *sync )
{
    if (!sync) return -1;
    return sync->fd;
}

struct inproc_sync *create_inproc_internal_sync( int manual, int signaled )
{
    struct ntsync_event_args args = {.signaled = signaled, .manual = manual};
    struct inproc_sync *event;

    if (!(event = alloc_object( &inproc_sync_ops ))) return NULL;
    event->type = INPROC_SYNC_INTERNAL;
    event->fd   = ntsync_ioctl( get_inproc_device_fd(), NTSYNC_IOC_CREATE_EVENT, &args );
    list_init( &event->entry );

    if (event->fd == -1)
    {
        set_error( STATUS_TOO_MANY_OPENED_FILES );
        release_object( event );
        return NULL;
    }
    return event;
}

struct inproc_sync *create_inproc_event_sync( int manual, int signaled )
{
    struct ntsync_event_args args = {.signaled = signaled, .manual = manual};
    struct inproc_sync *event;

    if (!(event = alloc_object( &inproc_sync_ops ))) return NULL;
    event->type = INPROC_SYNC_EVENT;
    event->fd   = ntsync_ioctl( get_inproc_device_fd(), NTSYNC_IOC_CREATE_EVENT, &args );
    list_init( &event->entry );

    if (event->fd == -1)
    {
        set_error( STATUS_NO_MORE_FILES );
        release_object( event );
        return NULL;
    }
    return event;
}

struct inproc_sync *create_inproc_mutex_sync( thread_id_t owner, unsigned int count )
{
    struct ntsync_mutex_args args = {.owner = owner, .count = count};
    struct inproc_sync *mutex;

    if (!(mutex = alloc_object( &inproc_sync_ops ))) return NULL;
    mutex->type = INPROC_SYNC_MUTEX;
    mutex->fd   = ntsync_ioctl( get_inproc_device_fd(), NTSYNC_IOC_CREATE_MUTEX, &args );
    list_add_tail( &inproc_mutexes, &mutex->entry );

    if (mutex->fd == -1)
    {
        set_error( STATUS_NO_MORE_FILES );
        release_object( mutex );
        return NULL;
    }
    return mutex;
}

struct inproc_sync *create_inproc_semaphore_sync( unsigned int initial, unsigned int max )
{
    struct ntsync_sem_args args = {.count = initial, .max = max};
    struct inproc_sync *sem;

    if (!(sem = alloc_object( &inproc_sync_ops ))) return NULL;
    sem->type = INPROC_SYNC_SEMAPHORE;
    sem->fd   = ntsync_ioctl( get_inproc_device_fd(), NTSYNC_IOC_CREATE_SEM, &args );
    list_init( &sem->entry );

    if (sem->fd == -1)
    {
        set_error( STATUS_NO_MORE_FILES );
        release_object( sem );
        return NULL;
    }
    return sem;
}

static void inproc_sync_dump( struct object *obj, int verbose )
{
    struct inproc_sync *sync = (struct inproc_sync *)obj;
    assert( obj->ops == &inproc_sync_ops );
    fprintf( stderr, "Inproc sync type=%d, fd=%d\n", sync->type, sync->fd );
}

void signal_inproc_sync( struct inproc_sync *sync )
{
    __u32 count;
    if (debug_level) fprintf( stderr, "set_inproc_event %d\n", sync->fd );
    ntsync_ioctl( sync->fd, NTSYNC_IOC_EVENT_SET, &count );
}

void reset_inproc_sync( struct inproc_sync *sync )
{
    __u32 count;
    if (debug_level) fprintf( stderr, "reset_inproc_event %d\n", sync->fd );
    ntsync_ioctl( sync->fd, NTSYNC_IOC_EVENT_RESET, &count );
}

static int inproc_sync_signal( struct object *obj, unsigned int access, int signal )
{
    struct inproc_sync *sync = (struct inproc_sync *)obj;
    assert( obj->ops == &inproc_sync_ops );

    assert( sync->type == INPROC_SYNC_INTERNAL || sync->type == INPROC_SYNC_EVENT ); /* never called for mutex / semaphore */
    assert( signal == 0 || signal == 1 ); /* never called from signal_object */

    if (signal) signal_inproc_sync( sync );
    else reset_inproc_sync( sync );
    return 1;
}

static void inproc_sync_destroy( struct object *obj )
{
    struct inproc_sync *sync = (struct inproc_sync *)obj;
    assert( obj->ops == &inproc_sync_ops );
    list_remove( &sync->entry );
    ntsync_close_obj( sync->fd );
}

/* A userspace ntsync object stays valid only while the server keeps it, so
 * every object sent to a client is referenced for that process until the
 * client releases it, like the fd the kernel driver would have sent. */
struct inproc_ref
{
    struct list          entry;
    struct inproc_sync  *sync;
};

static void add_inproc_ref( struct process *process, struct inproc_sync *sync )
{
    struct inproc_ref *ref;

    if (!(ref = mem_alloc( sizeof(*ref) ))) return;
    ref->sync = (struct inproc_sync *)grab_object( sync );
    list_add_tail( &process->inproc_refs, &ref->entry );
}

void release_inproc_refs( struct process *process )
{
    struct inproc_ref *ref, *next;

    LIST_FOR_EACH_ENTRY_SAFE( ref, next, &process->inproc_refs, struct inproc_ref, entry )
    {
        list_remove( &ref->entry );
        release_object( ref->sync );
        free( ref );
    }
}

static void release_inproc_ref( struct process *process, int fd )
{
    struct inproc_ref *ref;

    LIST_FOR_EACH_ENTRY( ref, &process->inproc_refs, struct inproc_ref, entry )
    {
        if (ref->sync->fd != fd) continue;
        list_remove( &ref->entry );
        release_object( ref->sync );
        free( ref );
        return;
    }
}

void abandon_inproc_mutexes( thread_id_t tid )
{
    struct inproc_sync *mutex;

    LIST_FOR_EACH_ENTRY( mutex, &inproc_mutexes, struct inproc_sync, entry )
        ntsync_ioctl( mutex->fd, NTSYNC_IOC_MUTEX_KILL, &tid );
}

static struct inproc_sync *get_obj_inproc_sync( struct object *obj )
{
    struct object *sync;

    if (!(sync = get_obj_sync( obj ))) return NULL;
    if (sync->ops == &inproc_sync_ops) return (struct inproc_sync *)sync;
    release_object( sync );
    return NULL;
}

#else /* NTSYNC_IOC_EVENT_READ */

int get_inproc_device_fd(void)
{
    return -1;
}

int inproc_sync_is_userspace(void)
{
    return 0;
}

int get_inproc_sync_fd( struct inproc_sync *sync )
{
    return -1;
}

struct inproc_sync *create_inproc_internal_sync( int manual, int signaled )
{
    return NULL;
}

struct inproc_sync *create_inproc_event_sync( int manual, int signaled )
{
    return NULL;
}

struct inproc_sync *create_inproc_mutex_sync( thread_id_t owner, unsigned int count )
{
    return NULL;
}

struct inproc_sync *create_inproc_semaphore_sync( unsigned int initial, unsigned int max )
{
    return NULL;
}

void signal_inproc_sync( struct inproc_sync *sync )
{
}

void reset_inproc_sync( struct inproc_sync *sync )
{
}

void abandon_inproc_mutexes( thread_id_t tid )
{
}

void release_inproc_refs( struct process *process )
{
}

static void add_inproc_ref( struct process *process, struct inproc_sync *sync )
{
}

static void release_inproc_ref( struct process *process, int fd )
{
}

static struct inproc_sync *get_obj_inproc_sync( struct object *obj )
{
    return NULL;
}

#endif /* NTSYNC_IOC_EVENT_READ */

DECL_HANDLER(get_inproc_sync_fd)
{
    struct inproc_sync *sync;
    struct object *obj;

    if (!(obj = get_handle_obj( current->process, req->handle, 0, NULL ))) return;

    reply->access = get_handle_access( current->process, req->handle );

    if (!(sync = get_obj_inproc_sync( obj ))) set_error( STATUS_NOT_IMPLEMENTED );
    else
    {
        reply->type = sync->type;
        if (inproc_sync_is_userspace())
        {
            add_inproc_ref( current->process, sync );
            reply->sync = sync->fd;
        }
        else send_client_fd( current->process, sync->fd, req->handle );
        release_object( sync );
    }

    release_object( obj );
}

/* The client is done with a userspace ntsync object it received */
DECL_HANDLER(release_inproc_sync)
{
    release_inproc_ref( current->process, req->sync );
}
