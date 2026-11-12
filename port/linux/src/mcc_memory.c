/* (port, this fork: the 64-bit builds compile MCC's code, but play no MCC
map, mcc_cache.c's MCC_CACHE_64BIT_REFUSED: its tag addresses are a 32-bit
build's pointers, which this build's Xbox addresses are not) */
#if defined(HALO_64BIT) && defined(__clang__)
#pragma clang diagnostic ignored "-Wint-conversion"
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"
#pragma clang diagnostic ignored "-Wpointer-to-int-cast"
#pragma clang diagnostic ignored "-Wpointer-integer-compare"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
/* MCC cache address reservations. A file can request a free 64 MiB window;
 * MAP_FIXED_NOREPLACE never replaces an Xbox, CE or host allocation. */
#include "platform.h"
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

/* (port, this fork: a system without it, macOS: the address is a hint,
and a window placed elsewhere is given back below) */
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif

#if defined(HALO_ANDROID) && defined(HALO_ARM64_GUEST) && !defined(HALO_BROWSER)
#include "guest_host.h"
#endif

#ifdef HALO_BROWSER
/* (port, this fork: the browser build, which also says HALO_ANDROID) Its
WebAssembly memory has no mmap: xbox_memory.c keeps every window a map may
ask for out of the C heap at start-up (PLATFORM_MCC_WINDOWS_BASE, platform.h),
and one loaded cache owns one of them at a time, as in OpenCE's web build
(HALO_WEB below; its web_memory.c is not this build's) */
static void *mcc_browser_window;
#endif

#ifdef HALO_WINDOWS
/* Keep the linked MCC window out of the Windows heap/driver allocator's
 * way before starting the renderer. Reserve address space, not committed
 * pages; unloading decommits the pages while retaining our reservation. */
static void *mcc_window;
static int mcc_window_active;
static int mcc_window_startup_error;

static void *mcc_window_hold(uint32_t address)
{
    void *wanted = (void *)(uintptr_t)address;
    void *allocation = mmap(wanted, 0x04000000u, PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (allocation == wanted) return allocation;
    if (allocation != MAP_FAILED) {
        munmap(allocation, 0x04000000u);
        errno = EINVAL;
    }
    return NULL;
}

/* The CE constructor (102) reads settings through SDL. Run alongside the
 * independent Xbox reservation (101), before that first delay-loaded call. */
__attribute__((constructor(101)))
static void mcc_window_initialize(void)
{
    mcc_window = mcc_window_hold(0x50000000u);
    if (!mcc_window)
        mcc_window_startup_error = errno;
}

/* Called after Windows has connected stderr to the console, pipe or halo.log.
 * Remember the original error: intervening startup calls can change errno. */
void mcc_memory_log_startup(void)
{
    if (mcc_window_startup_error) {
        platform_log("mcc memory: early reservation at 50000000 (64 MiB) failed (%s); will retry at map load",
            strerror(mcc_window_startup_error));
        mcc_window_startup_error = 0;
    }
}

__attribute__((destructor))
static void mcc_window_dispose(void)
{
    if (mcc_window) munmap(mcc_window, 0x04000000u);
}
#endif

void *mcc_memory_reserve(uint32_t address, uint32_t size)
{
#if defined(HALO_BROWSER)
    void *wanted = (void *)(uintptr_t)address;
    if ((address & 0xFFFFu) || size != 0x04000000u ||
        address < PLATFORM_MCC_WINDOWS_BASE || address > PLATFORM_MCC_WINDOWS_END - size) {
        errno = EINVAL;
        return NULL;
    }
    if (!platform_mcc_windows_ready) {
        errno = ENOMEM;
        return NULL;
    }
    if (mcc_browser_window) {
        errno = EBUSY;
        return NULL;
    }
    /* (cleared, as fresh pages would be) */
    memset(wanted, 0, size);
    mcc_browser_window = wanted;
    return wanted;
#elif defined(HALO_ANDROID) && defined(HALO_ARM64_GUEST)
    long long result = host_mcc_memory_reserve(address, size);
    if (result < 0) {
        errno = (int)-result;
        return NULL;
    }
    return (void *)(uintptr_t)result;
#elif defined(HALO_WEB)
    (void)address;
    (void)size;
    errno = ENOSYS;
    return NULL;
#else
    void *wanted = (void *)(uintptr_t)address;
    void *allocation;
    /* Keep clear of the Xbox arena, Custom Edition window and low addresses.
     * MCC Tool's current base is 0x50000000; other non-overlapping linked
     * addresses are accepted only if the OS confirms the window is free. */
    if ((address & 0xFFFFu) || size != 0x04000000u ||
        address < 0x42000000u || address > 0x70000000u) {
        errno = EINVAL;
        return NULL;
    }
#ifdef HALO_WINDOWS
    if (mcc_window_active) {
        errno = EBUSY;
        return NULL;
    }
    /* Other accepted linked bases retain their previous support. Only our
     * own inactive reservation can be released or recommitted. */
    if (mcc_window && mcc_window != wanted) {
        if (munmap(mcc_window, size)) return NULL;
        mcc_window = NULL;
    }
    if (!mcc_window) mcc_window = mcc_window_hold(address);
    if (!mcc_window) return NULL;
    allocation = mmap(mcc_window, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
    if (allocation == mcc_window) {
        mcc_window_active = 1;
        return allocation;
    }
#else
    allocation = mmap(wanted, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (allocation == wanted)
        return allocation;
#endif
    if (allocation != MAP_FAILED) {
        munmap(allocation, size);
        errno = EINVAL;
    }
    return NULL;
#endif
}

void mcc_memory_release(void *allocation, uint32_t size)
{
#if defined(HALO_BROWSER)
    (void)size;
    if (allocation && allocation == mcc_browser_window)
        mcc_browser_window = NULL;
#elif defined(HALO_ANDROID) && defined(HALO_ARM64_GUEST)
    if (allocation) {
        int error = host_mcc_memory_release((unsigned int)(uintptr_t)allocation, size);
        if (error)
            platform_log("mcc memory: could not decommit tag window (%s)", strerror(-error));
    }
#elif defined(HALO_WEB)
    (void)allocation;
    (void)size;
#elif defined(HALO_WINDOWS)
    if (allocation && allocation == mcc_window && size == 0x04000000u && mcc_window_active) {
        if (mmap(allocation, size, PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == allocation)
            mcc_window_active = 0;
        else
            platform_log("mcc memory: could not decommit tag window (%s)", strerror(errno));
    }
#else
    if (allocation)
        munmap(allocation, size);
#endif
}
