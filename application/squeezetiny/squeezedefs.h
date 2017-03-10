/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2014, triode1@btinternet.com
 *  (c) Philippe, philippe_44@outlook.com for raop/multi-instance modifications
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#ifndef __SQUEEZEDEFS_H
#define __SQUEEZEDEFS_H

#define VERSION "v0.2.2.0-dev-1"" ("__DATE__" @ "__TIME__")"

#include <pthread.h>
#include "platform.h"

#define STREAM_THREAD_STACK_SIZE (1024 * 64)
#define DECODE_THREAD_STACK_SIZE (1024 * 128)
#define OUTPUT_THREAD_STACK_SIZE (1024 * 64)
#define SLIMPROTO_THREAD_STACK_SIZE  (1024 * 64)

#define mutex_type pthread_mutex_t
#define mutex_create(m) pthread_mutex_init(&m, NULL)
#define mutex_lock(m) pthread_mutex_lock(&m)
#define mutex_trylock(m) pthread_mutex_trylock(&m)
#define mutex_unlock(m) pthread_mutex_unlock(&m)
#define mutex_destroy(m) pthread_mutex_destroy(&m)
#define thread_type pthread_t
#define mutex_timedlock(m, t) _mutex_timedlock(&m, t)
int _mutex_timedlock(mutex_type *m, u32_t wait);

#if LINUX || OSX || FREEBSD

#define closesocket(s) close(s)
#define last_error() errno
#define ERROR_WOULDBLOCK EWOULDBLOCK
#define mutex_create_p(m) pthread_mutexattr_t attr; pthread_mutexattr_init(&attr); pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT); pthread_mutex_init(&m, &attr); pthread_mutexattr_destroy(&attr)

#endif

#if WIN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <io.h>
#include <stdbool.h>
#include <sys/timeb.h>

#define inline __inline
#define mutex_create_p(m) pthread_mutex_init(&m, NULL)

#endif

#endif     // __SQUEEZEDEFS_H
