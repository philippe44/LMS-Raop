#ifndef __SQUEEZEDEFS_H
#define __SQUEEZEDEFS_H

#define VERSION "v0.1.0.0-dev-8"" ("__DATE__" @ "__TIME__")"

#include "platform.h"

#if LINUX || OSX || FREEBSD

#define STREAM_THREAD_STACK_SIZE  64 * 1024
#define DECODE_THREAD_STACK_SIZE 32 * 1024
#define OUTPUT_THREAD_STACK_SIZE  64 * 1024
#define SLIMPROTO_THREAD_STACK_SIZE  64 * 1024
#define thread_t pthread_t;
#define closesocket(s) close(s)
#define last_error() errno
#define ERROR_WOULDBLOCK EWOULDBLOCK

#define mutex_type pthread_mutex_t
#define mutex_create(m) pthread_mutex_init(&m, NULL)
#define mutex_create_p(m) pthread_mutexattr_t attr; pthread_mutexattr_init(&attr); pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT); pthread_mutex_init(&m, &attr); pthread_mutexattr_destroy(&attr)
#define mutex_lock(m) pthread_mutex_lock(&m)
#define mutex_unlock(m) pthread_mutex_unlock(&m)
#define mutex_destroy(m) pthread_mutex_destroy(&m)
#define thread_type pthread_t
#define mutex_timedlock(m, t) _mutex_timedlock(&m, t)
int _mutex_timedlock(mutex_type *m, u32_t wait);

#endif

#if WIN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <io.h>
#include <stdbool.h>
#include <sys/timeb.h>
#include "pthread.h"

#define STREAM_THREAD_STACK_SIZE (1024 * 64)
#define DECODE_THREAD_STACK_SIZE (1024 * 128)
#define OUTPUT_THREAD_STACK_SIZE (1024 * 64)
#define SLIMPROTO_THREAD_STACK_SIZE  (1024 * 64)

#define inline __inline

#define mutex_type HANDLE
#define mutex_create(m) m = CreateMutex(NULL, FALSE, NULL)
#define mutex_create_p mutex_create
// in Windows, a mutex never locks if the owner tries to lock it
#define mutex_lock(m) WaitForSingleObject(m, INFINITE)
#define mutex_timedlock(m,t) WaitForSingleObject(m, t)
#define mutex_unlock(m) ReleaseMutex(m)
#define mutex_destroy(m) CloseHandle(m)
#define thread_type HANDLE

#endif

#endif     // __SQUEEZEDEFS_H
