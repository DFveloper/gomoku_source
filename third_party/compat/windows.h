// Minimal WinAPI compatibility shim so the vendored Piskvork engines
// (Carbon, XL) build unmodified on POSIX systems.
#pragma once

#include <pthread.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <cstdint>

#define UInt32x32To64(a, b) (static_cast<unsigned long long>(a) * static_cast<unsigned long long>(b))
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#ifdef XL_ENGINE_BUILD
// XL uses plain min()/max(); none of its sources declare conflicting symbols.
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
// Windows clock() is wall-clock milliseconds; POSIX clock() is CPU microseconds,
// which made XL's millisecond-based time management abort every search instantly.
inline unsigned long win_clock_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<unsigned long>(now.tv_sec) * 1000ul + now.tv_nsec / 1000000ul;
}
#define clock() win_clock_ms()
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned long ULONG;
typedef unsigned int UINT;
typedef void* LPVOID;
typedef void* HANDLE;
typedef unsigned long* LPDWORD;
#define WINAPI
#define TRUE_ 1
#define FALSE_ 0
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 1u
#define STD_INPUT_HANDLE ((DWORD)-10)
#define INVALID_HANDLE_VALUE ((HANDLE)-1)

inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

// Auto-reset or manual-reset event backed by a mutex + condition variable.
struct win_event {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signaled;
    bool manual;
};

inline HANDLE CreateEvent(void*, BOOL manual, BOOL initial, void*) {
    win_event* event = new win_event();
    pthread_mutex_init(&event->mutex, nullptr);
    pthread_cond_init(&event->cond, nullptr);
    event->signaled = initial != 0;
    event->manual = manual != 0;
    return event;
}

inline void close_win_event(HANDLE handle) {
    win_event* event = static_cast<win_event*>(handle);
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
    delete event;
}

inline BOOL SetEvent(HANDLE handle) {
    win_event* event = static_cast<win_event*>(handle);
    pthread_mutex_lock(&event->mutex);
    event->signaled = true;
    pthread_cond_broadcast(&event->cond);
    pthread_mutex_unlock(&event->mutex);
    return 1;
}

inline BOOL ResetEvent(HANDLE handle) {
    win_event* event = static_cast<win_event*>(handle);
    pthread_mutex_lock(&event->mutex);
    event->signaled = false;
    pthread_mutex_unlock(&event->mutex);
    return 1;
}

inline DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    win_event* event = static_cast<win_event*>(handle);
    timespec deadline{};
    bool timed = milliseconds != INFINITE;
    if (timed) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += static_cast<long long>(milliseconds % 1000u) * 1000000L;
        deadline.tv_sec += milliseconds / 1000u + deadline.tv_nsec / 1000000000L;
        deadline.tv_nsec %= 1000000000L;
    }
    pthread_mutex_lock(&event->mutex);
    while (!event->signaled) {
        if (timed) {
            int error = pthread_cond_timedwait(&event->cond, &event->mutex, &deadline);
            if (error == ETIMEDOUT) { pthread_mutex_unlock(&event->mutex); return WAIT_TIMEOUT; }
        } else {
            pthread_cond_wait(&event->cond, &event->mutex);
        }
    }
    if (!event->manual) event->signaled = false;
    pthread_mutex_unlock(&event->mutex);
    return WAIT_OBJECT_0;
}

struct win_thread_arg { DWORD (*entry)(LPVOID); LPVOID param; };

inline void* win_thread_trampoline(void* raw) {
    win_thread_arg arg = *static_cast<win_thread_arg*>(raw);
    delete static_cast<win_thread_arg*>(raw);
    arg.entry(arg.param);
    return nullptr;
}

inline HANDLE CreateThread(void*, size_t, DWORD (*entry)(LPVOID), LPVOID param, size_t, LPDWORD) {
    auto* arg = new win_thread_arg{entry, param};
    pthread_t thread;
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attributes, win_thread_trampoline, arg) != 0) {
        pthread_attr_destroy(&attributes);
        delete arg;
        return nullptr;
    }
    pthread_attr_destroy(&attributes);
    return reinterpret_cast<HANDLE>(thread);
}

inline DWORD GetTickCount() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<DWORD>(now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}

inline HANDLE GetStdHandle(DWORD) { return nullptr; }
inline BOOL GetConsoleMode(HANDLE, LPDWORD) { return FALSE; }
inline DWORD SetErrorMode(DWORD) { return 0; }
