/*
THREADING_ALT.H

Mbed TLS's mutex on Windows (MBEDTLS_THREADING_ALT, which
tools/windows_build.py defines for every unit that includes Mbed TLS's
headers): a slim reader/writer lock, held exclusively. It is a pointer's
size, and all zeros is an unlocked one (SRWLOCK_INIT), so this header needs
no <windows.h>; port/windows/src/win32_mbedtls.c has the functions and hands
them to Mbed TLS before main.
*/

#ifndef HALO_WINDOWS_MBEDTLS_THREADING_ALT_H
#define HALO_WINDOWS_MBEDTLS_THREADING_ALT_H

typedef struct mbedtls_threading_mutex_t
{
	/* (an SRWLOCK) */
	void *lock;
} mbedtls_threading_mutex_t;

#endif
