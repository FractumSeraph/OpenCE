/*
SERVER_WIN32.C

What the dedicated server's control thread (server_control.c) waits on that
Winsock's poll cannot, on Windows (server_win32.h): a pipe to be woken by,
which is a pair of loopback sockets here; and standard input, which a
thread of its own reads, handing its bytes over and waking the control
thread as the pipe does. Built only for Windows (tools/windows_build.py).
*/

#include "server_win32.h"

/* ---------- the wake pair */

int server_wake_pair(int fds[2])
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int receiver = socket(AF_INET, SOCK_DGRAM, 0);
	int sender = socket(AF_INET, SOCK_DGRAM, 0);

	fds[0] = fds[1] = -1;
	if (receiver < 0 || sender < 0)
		goto failed;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	/* (the receiver on a port of the system's choosing, which the sender
	is connected to: nothing else's datagrams arrive) */
	if (bind(receiver, (struct sockaddr *)&address, sizeof(address)) ||
		getsockname((SOCKET)receiver, (struct sockaddr *)&address, &length) == SOCKET_ERROR ||
		connect((SOCKET)sender, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
	{
		errno = server_winsock_errno();
		goto failed;
	}
	fds[0] = receiver;
	fds[1] = sender;
	return 0;

failed:
	if (receiver >= 0)
		server_close_socket(receiver);
	if (sender >= 0)
		server_close_socket(sender);
	return -1;
}

/* ---------- standard input */

enum
{
	CONSOLE_BUFFER_SIZE = 4096,
};

static struct
{
	CRITICAL_SECTION lock;
	char buffer[CONSOLE_BUFFER_SIZE];
	int length;
	/* standard input ended (or failed) */
	int ended;
	int wake;
} console;

/* standard input, a read at a time, into the buffer (a full buffer waits
for the control thread to take some) */
static DWORD WINAPI console_reader(LPVOID argument)
{
	HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

	(void)argument;
	for (;;)
	{
		char bytes[512];
		DWORD count = 0;
		DWORD done = 0;

		if (!ReadFile(input, bytes, sizeof(bytes), &count, NULL) || !count)
		{
			EnterCriticalSection(&console.lock);
			console.ended = 1;
			LeaveCriticalSection(&console.lock);
			send(console.wake, "", 1, 0);
			return 0;
		}
		while (done < count)
		{
			int taken;

			EnterCriticalSection(&console.lock);
			taken = (int)(count - done);
			if (taken > CONSOLE_BUFFER_SIZE - console.length)
				taken = CONSOLE_BUFFER_SIZE - console.length;
			memcpy(console.buffer + console.length, bytes + done, (size_t)taken);
			console.length += taken;
			LeaveCriticalSection(&console.lock);
			done += (DWORD)taken;
			send(console.wake, "", 1, 0);
			if (done < count)
				Sleep(10);
		}
	}
}

int server_console_start(int wake_fd)
{
	HANDLE thread;

	InitializeCriticalSection(&console.lock);
	console.wake = wake_fd;
	thread = CreateThread(NULL, 64 * 1024, console_reader, NULL, 0, NULL);
	if (!thread)
		return -1;
	CloseHandle(thread);
	return 0;
}

int server_console_read(void *buffer, size_t size)
{
	int count;

	EnterCriticalSection(&console.lock);
	count = console.length < (int)size ? console.length : (int)size;
	if (count)
	{
		memcpy(buffer, console.buffer, (size_t)count);
		memmove(console.buffer, console.buffer + count, (size_t)(console.length - count));
		console.length -= count;
	}
	else if (!console.ended)
	{
		count = -1;
		errno = EAGAIN;
	}
	LeaveCriticalSection(&console.lock);
	return count;
}

/* ---------- stopping */

static volatile int *stop_flag;

/* Ctrl+C, Ctrl+Break, the console window closed, the user logging off or
the system shutting down: the server stops at its next frame, as on SIGTERM
(server_platform.c); for the last three, Windows ends the program when this
returns, so it waits a little for that frame */
static BOOL WINAPI console_event(DWORD type)
{
	if (stop_flag)
		*stop_flag = 1;
	if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT)
		Sleep(5000);
	return TRUE;
}

void server_win32_stop_events(volatile int *flag)
{
	stop_flag = flag;
	SetConsoleCtrlHandler(console_event, TRUE);
}
