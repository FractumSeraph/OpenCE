/*
SERVER_WIN32.H

The POSIX the dedicated server's console and control API use
(server_control.c, control_tls.c, control_link.c, server_roles.c), on
Windows: Winsock for the sockets, the C runtime for the files. The
server's Windows build (tools/windows_build.py, ``ninja server-windows64``)
puts this folder's headers (<sys/socket.h>, <netinet/in.h>, <arpa/inet.h>,
<poll.h>, <strings.h>, <fcntl.h>) ahead of the system's; each includes this.

A socket is kept in an int, as on POSIX (a Winsock handle fits one); the
calls that fail set errno from Winsock's error (EWOULDBLOCK, EINTR, ...), as
the code checks errno after a send, a receive or an accept. A file is
replaced by rename (MoveFileEx), as POSIX's rename does and the C runtime's
does not. What Windows has no notion of (a descriptor closed on exec, a
file's mode beyond its owner's) does nothing.
*/

#ifndef __SERVER_WIN32_H
#define __SERVER_WIN32_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- sockets */

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
typedef ULONG nfds_t;

/* Winsock's last error as errno's */
static __inline int server_winsock_errno(void)
{
	switch (WSAGetLastError())
	{
	case WSAEWOULDBLOCK:
		return EWOULDBLOCK;
	case WSAEINTR:
		return EINTR;
	case WSAEINPROGRESS:
		return EINPROGRESS;
	case WSAECONNRESET:
		return ECONNRESET;
	case WSAECONNABORTED:
		return ECONNABORTED;
	case WSAEADDRINUSE:
		return EADDRINUSE;
	case WSAEADDRNOTAVAIL:
		return EADDRNOTAVAIL;
	case WSAEACCES:
		return EACCES;
	case WSAEAFNOSUPPORT:
		return EAFNOSUPPORT;
	default:
		return EIO;
	}
}

/* Winsock started for this program's own sockets (the game's start it too:
each start is counted, and one more is harmless) */
static __inline void server_winsock_start(void)
{
	static volatile LONG started;

	if (!InterlockedExchange(&started, 1))
	{
		WSADATA data;

		WSAStartup(MAKEWORD(2, 2), &data);
	}
}

static __inline int server_socket(int family, int type, int protocol)
{
	SOCKET handle;

	server_winsock_start();
	handle = socket(family, type & ~SOCK_CLOEXEC, protocol);
	if (handle == INVALID_SOCKET)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return (int)handle;
}

static __inline int server_accept(int fd, struct sockaddr *address, int *length)
{
	SOCKET handle = accept((SOCKET)fd, address, length);

	if (handle == INVALID_SOCKET)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return (int)handle;
}

static __inline int server_bind(int fd, const struct sockaddr *address, int length)
{
	if (bind((SOCKET)fd, address, length) == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return 0;
}

static __inline int server_listen(int fd, int backlog)
{
	if (listen((SOCKET)fd, backlog) == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return 0;
}

/* (SO_REUSEADDR on Windows lets another program take a port in use: left
out, as a listener there needs none to be bound again at once) */
static __inline int server_setsockopt(int fd, int level, int name, const void *value, int length)
{
	if (level == SOL_SOCKET && name == SO_REUSEADDR)
		return 0;
	if (setsockopt((SOCKET)fd, level, name, (const char *)value, length) == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return 0;
}

static __inline int server_send(int fd, const void *buffer, size_t size, int flags)
{
	int count = send((SOCKET)fd, (const char *)buffer, (int)(size > INT_MAX ? INT_MAX : size), flags);

	if (count == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return count;
}

static __inline int server_recv(int fd, void *buffer, size_t size, int flags)
{
	int count = recv((SOCKET)fd, (char *)buffer, (int)(size > INT_MAX ? INT_MAX : size), flags);

	if (count == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return count;
}

static __inline int server_shutdown(int fd, int how)
{
	return shutdown((SOCKET)fd, how) == SOCKET_ERROR ? -1 : 0;
}

static __inline int server_poll(struct pollfd *fds, nfds_t count, int timeout)
{
	int result = WSAPoll(fds, count, timeout);

	if (result == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		return -1;
	}
	return result;
}

/* a socket closed (a file is close's) */
static __inline int server_close_socket(int fd)
{
	return closesocket((SOCKET)fd) == SOCKET_ERROR ? -1 : 0;
}

#define socket server_socket
#define accept server_accept
#define bind server_bind
#define listen server_listen
#define setsockopt server_setsockopt
#define send server_send
#define recv server_recv
#define shutdown server_shutdown
#define poll server_poll

/* ---------- descriptors */

#ifndef F_GETFD
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#endif
/* (no flag of the C runtime's: _O_ flags end at 0x80000) */
#ifndef O_NONBLOCK
#define O_NONBLOCK 0x10000000
#endif
/* a file opened not inherited by a program started (the server starts none) */
#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT
#endif

/* a socket's blocking, the one flag the server sets; the rest do nothing */
static __inline int server_fcntl(int fd, int command, ...)
{
	if (command == F_SETFL)
	{
		va_list arguments;
		int flags;
		u_long nonblocking;

		va_start(arguments, command);
		flags = va_arg(arguments, int);
		va_end(arguments);
		nonblocking = (flags & O_NONBLOCK) ? 1 : 0;
		if (ioctlsocket((SOCKET)fd, FIONBIO, &nonblocking) == SOCKET_ERROR)
		{
			errno = server_winsock_errno();
			return -1;
		}
	}
	return 0;
}
#define fcntl server_fcntl

/* ---------- files */

#define fsync _commit
/* (only the owner's mode bits exist on Windows) */
#define fchmod(fd, mode) ((void)(fd), (void)(mode), 0)

/* a file replaced, as POSIX's rename replaces one */
static __inline int server_rename(const char *from, const char *to)
{
	if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return 0;
	errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT;
	return -1;
}
#define rename server_rename

/* ---------- what poll cannot wait on (server_win32.c) */

/* a pipe to wake the control thread by: two loopback sockets, the first
read, the second written (recv and send, not read and write) */
int server_wake_pair(int fds[2]);
/* standard input, read on a thread of its own: started with the wake
pair's writing end, which it writes when bytes come; server_console_read
takes them (-1 with errno EAGAIN when none came yet, 0 once it ended) */
int server_console_start(int wake_fd);
int server_console_read(void *buffer, size_t size);

/* ---------- the rest */

typedef int mode_t;

static __inline unsigned int sleep(unsigned int seconds)
{
	Sleep(seconds * 1000);
	return 0;
}

/* (POSIX's argument order: the C runtime's gmtime_s has them the other way) */
static __inline struct tm *gmtime_r(const time_t *time, struct tm *result)
{
	return gmtime_s(result, time) ? NULL : result;
}

/* ---------- strings */

#define strcasecmp _stricmp
#define strncasecmp _strnicmp

#endif
