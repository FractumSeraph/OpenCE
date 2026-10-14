/*
WIN32_MBEDTLS.C

Mbed TLS's locks on Windows. Mbed TLS is built with MBEDTLS_THREADING_C
(tools/windows_build.py), as on Linux, because its PSA crypto (its setup,
random generator and key slots) is shared by the game thread's DTLS with
browsers (port/linux/src/posix_dtls.c) and the other threads' HTTPS (the
game list's, Delta Stats' and the dedicated server's: posix_browser.c,
control_tls.c). Linux has pthread mutexes for it (MBEDTLS_THREADING_PTHREAD);
here they are slim reader/writer locks (MBEDTLS_THREADING_ALT,
port/windows/mbedtls/threading_alt.h), handed to Mbed TLS before main, so
before any thread uses it.
*/

#include <windows.h>

#include "mbedtls/threading.h"

_Static_assert(sizeof(mbedtls_threading_mutex_t) == sizeof(SRWLOCK), "threading_alt.h's mutex is an SRWLOCK");

static void mutex_init(mbedtls_threading_mutex_t *mutex)
{
	if (mutex)
		InitializeSRWLock((PSRWLOCK)&mutex->lock);
}

/* (an SRWLOCK holds nothing to free) */
static void mutex_free(mbedtls_threading_mutex_t *mutex)
{
	(void)mutex;
}

static int mutex_lock(mbedtls_threading_mutex_t *mutex)
{
	if (!mutex)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	AcquireSRWLockExclusive((PSRWLOCK)&mutex->lock);
	return 0;
}

static int mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
	if (!mutex)
		return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
	ReleaseSRWLockExclusive((PSRWLOCK)&mutex->lock);
	return 0;
}

/* (Mbed TLS's global mutexes, initialized here too, are never freed: they
last as long as the process) */
__attribute__((constructor)) static void mbedtls_locks_set_up(void)
{
	mbedtls_threading_set_alt(mutex_init, mutex_free, mutex_lock, mutex_unlock);
}
