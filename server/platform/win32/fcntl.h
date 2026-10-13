/* POSIX <fcntl.h> on Windows, for the dedicated server: the C runtime's,
and what server_win32.h adds to it (O_NONBLOCK, O_CLOEXEC, fcntl) */

#ifndef __SERVER_WIN32_FCNTL_H
#define __SERVER_WIN32_FCNTL_H

#include_next <fcntl.h>
#include "server_win32.h"

#endif
