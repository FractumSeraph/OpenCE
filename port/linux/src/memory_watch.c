/*
MEMORY_WATCH.C

Write tracking for guest memory that the renderer caches.

Textures live in the Xbox contiguous window, where the game (or its
streaming threads) can rewrite them at any time. Instead of hashing their
contents every frame, the pages behind a cached texture are made read-only;
the first write faults, the handler records a new generation for the page,
makes it writable again and lets the write proceed. A cache entry is stale
when any of its pages has a generation newer than the entry.
#ifdef HALO_64BIT

Tracking works in host pages (16 KB on Apple silicon), so a write next to a
texture can mark it stale; that costs a re-upload, never a missed change.
#endif

Writes that the kernel performs on the game's behalf (read() into a
buffer) would fail with EFAULT instead of faulting, so the file layer reads
guest memory through a bounce buffer (xbox_files.c). Unprotecting ahead of
such a write (memory_watch_prepare_write) is not enough on its own: the
renderer can protect the pages again before the kernel writes them.

The same handler reports a crash (SIGSEGV, and SIGBUS, SIGILL, SIGFPE and
SIGABRT too): the signal, the registers and the calls, to the terminal and
debug.txt, and then hands the signal to the handler before it, or the
default action. It is installed before main, so that a crash at any time
is reported; a library that installs its own later would take the watch's
faults, so the renderer's device installs it again, chained to that one.
(The 64-bit builds' watch faults come as SIGSEGV, or SIGBUS on macOS.)
*/

#include "platform.h"

#ifdef HALO_BROWSER

/* Browsers do not expose mprotect or synchronous fault handlers.  WebGL
uploads therefore use an always-changing generation and bypass the large
vertex mirror (d3d8_gl.c).  This is deliberately conservative: resources
are uploaded again rather than ever rendering stale guest memory. */
static volatile unsigned long current_generation = 1;

void memory_watch_initialize(void)
{
}

void memory_watch_protect(unsigned long address, unsigned long size)
{
	(void)address;
	(void)size;
}

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	(void)address;
	return size ? __sync_add_and_fetch(&current_generation, 1) : 0;
}

unsigned long memory_watch_serial(void)
{
	return __sync_add_and_fetch(&current_generation, 1);
}

void memory_watch_prepare_write(void *address, unsigned long size)
{
	(void)address;
	if (size)
		__sync_add_and_fetch(&current_generation, 1);
}

void memory_watch_forget(void *address, unsigned long size)
{
	(void)address;
	if (size)
		__sync_add_and_fetch(&current_generation, 1);
}

/* the web build's generations see each write at once: nothing to do per
frame */
void memory_watch_begin_frame(void)
{
}

#else

#if __has_include(<execinfo.h>) && !(defined(__ANDROID__) && __ANDROID_API__ < 33)
#include <execinfo.h>
#else
/* (musl has none: the server's own, server/platform/backtrace.c; bionic's
are Android 13's, API 33: the native 64-bit Android app has its own,
port/android/native64/android64_main.c) */
int backtrace(void **frames, int count);
void backtrace_symbols_fd(void *const *frames, int count, int descriptor);
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#ifndef HALO_64BIT
#include <ucontext.h>
#endif
#include <sys/mman.h>
#ifdef HALO_64BIT
#include <sys/ucontext.h>
#endif
#include <unistd.h>

/* (build_identity.h's, through platform.h; tools/test_memory_watch.py
compiles this file beside a platform.h of its own, without one) */
#ifndef PLATFORM_LOG_PREFIX
#define PLATFORM_LOG_PREFIX "halo-linux: "
#endif

#ifdef HALO_64BIT
/* enough entries for 4 KB host pages; larger pages use fewer */
#define WATCH_PAGE_COUNT_MAXIMUM (PLATFORM_CONTIGUOUS_SIZE / 0x1000U)
#else
#define WATCH_PAGE_SIZE 0x1000UL
#define WATCH_PAGE_COUNT (PLATFORM_CONTIGUOUS_SIZE / WATCH_PAGE_SIZE)
/* (the tables: as many as the largest window has, platform.h) */
#define WATCH_PAGE_COUNT_MAXIMUM (PLATFORM_CONTIGUOUS_MAXIMUM / WATCH_PAGE_SIZE)
#endif

/* (each page: 0 not watched, 1 watched and read-only, 2 made writable by a
watched write. A fault on a page at 2 is a write another thread made as
this one was making the page writable, and is made again; one on a page at
0, freed memory or none of the watch's, is a crash) */
#ifdef HALO_64BIT
static unsigned char page_protected[WATCH_PAGE_COUNT_MAXIMUM];
static unsigned int page_generation[WATCH_PAGE_COUNT_MAXIMUM];
static volatile unsigned int current_generation = 1;
#else
static unsigned char page_protected[WATCH_PAGE_COUNT_MAXIMUM];
static unsigned long page_generation[WATCH_PAGE_COUNT_MAXIMUM];
static volatile unsigned long current_generation = 1;
#endif
static BOOL watch_active = FALSE;
#ifdef HALO_64BIT
static unsigned int watch_page_size = 0x1000U;
static unsigned int watch_page_count = WATCH_PAGE_COUNT_MAXIMUM;
/* a page was protected: the page size stays as it is */
static BOOL watch_used = FALSE;
#endif

/* the signals reported as a crash, SIGSEGV first (the write tracking's), and
what handled each before this file's handler */
static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
#define CRASH_SIGNAL_COUNT (int)(sizeof(crash_signals) / sizeof(crash_signals[0]))
static struct sigaction previous_actions[CRASH_SIGNAL_COUNT];
/* a crash being reported: a handler chained to that hands the signal back
here (one installed after this file's, which took this file's as its
previous) gets the default action instead of a loop */
static volatile sig_atomic_t crash_chaining = 0;

#ifdef HALO_64BIT
static BOOL xbox_address_is_contiguous(unsigned int address)
{
	return address >= PLATFORM_CONTIGUOUS_BASE && address - PLATFORM_CONTIGUOUS_BASE < PLATFORM_CONTIGUOUS_SIZE;
}

static unsigned int page_index(unsigned int address)
{
	return (address - PLATFORM_CONTIGUOUS_BASE) / watch_page_size;
}

static void *page_pointer(unsigned int page)
{
	return xbox_pointer(PLATFORM_CONTIGUOUS_BASE + page * watch_page_size);
}

static void mark_written(unsigned int page)
{
	page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	page_protected[page] = 2;
	mprotect(page_pointer(page), watch_page_size, PROT_READ | PROT_WRITE);
}
#else
static unsigned long page_index(unsigned long address)
{
	return (address - PLATFORM_CONTIGUOUS_BASE) / WATCH_PAGE_SIZE;
}

static void mark_written(unsigned long page)
{
	page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	page_protected[page] = 2;
	mprotect((void *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE, PROT_READ | PROT_WRITE);
}
#endif

/* errors.c's: debug.txt (a player sends it; the terminal's lines may be
gone) */
void write_to_error_file(char *string, unsigned char date);

/* a line of a crash report (ending in a newline) to debug.txt too, best
effort: the crash may be in the middle of writing it */
static void crash_debug_line(const char *line)
{
	char text[200];
	size_t length = strcspn(line, "\n");

	if (length > sizeof(text) - 3)
		length = sizeof(text) - 3;
	memcpy(text, line, length);
	memcpy(text + length, "\r\n", 3);
	write_to_error_file(text, 1);
}

static const char *crash_signal_name(int signal_number)
{
	switch (signal_number)
	{
	case SIGSEGV:
		return "segmentation fault";
	case SIGBUS:
		return "bus error";
	case SIGILL:
		return "illegal instruction";
	case SIGFPE:
		return "arithmetic exception";
	case SIGABRT:
		return "abort";
	}
	return "signal";
}

static int crash_signal_index(int signal_number)
{
	int index;

	for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
	{
		if (crash_signals[index] == signal_number)
			return index;
	}
	return -1;
}

/* the report: the signal, the registers, the top of the stack (32-bit) and
the calls, to the terminal and debug.txt */
static void crash_report(int signal_number, siginfo_t *information, void *context)
{
	ucontext_t *ucontext = context;
	char line[200];
	void *frames[48];
	int count, length;
#ifdef HALO_64BIT
	unsigned long long pc = 0, frame = 0, stack = 0;
#else
	unsigned long pc, frame, stack;
#endif

#ifdef HALO_64BIT
#if defined(__APPLE__) && defined(__aarch64__)
	pc = ucontext->uc_mcontext->__ss.__pc;
	frame = ucontext->uc_mcontext->__ss.__fp;
	stack = ucontext->uc_mcontext->__ss.__sp;
#elif defined(__APPLE__) && defined(__x86_64__)
	pc = ucontext->uc_mcontext->__ss.__rip;
	frame = ucontext->uc_mcontext->__ss.__rbp;
	stack = ucontext->uc_mcontext->__ss.__rsp;
#elif defined(__linux__) && defined(__aarch64__)
	pc = ucontext->uc_mcontext.pc;
	frame = ucontext->uc_mcontext.regs[29];
	stack = ucontext->uc_mcontext.sp;
#elif defined(__linux__) && defined(__x86_64__)
	pc = ucontext->uc_mcontext.gregs[REG_RIP];
	frame = ucontext->uc_mcontext.gregs[REG_RBP];
	stack = ucontext->uc_mcontext.gregs[REG_RSP];
#else
	(void)ucontext;
#endif
	length = snprintf(line, sizeof(line), PLATFORM_LOG_PREFIX "%s (signal %d, code %d) at %p, pc %llx fp %llx sp %llx\n",
		crash_signal_name(signal_number), signal_number, information ? information->si_code : 0,
		information ? information->si_addr : NULL, pc, frame, stack);
	write(STDERR_FILENO, line, (size_t)length);
	crash_debug_line(line);
#else
#if defined(__i386__)
	pc = (unsigned long)ucontext->uc_mcontext.gregs[REG_EIP];
	frame = (unsigned long)ucontext->uc_mcontext.gregs[REG_EBP];
	stack = (unsigned long)ucontext->uc_mcontext.gregs[REG_ESP];
#elif defined(__x86_64__)
	pc = (unsigned long)ucontext->uc_mcontext.gregs[REG_RIP];
	frame = (unsigned long)ucontext->uc_mcontext.gregs[REG_RBP];
	stack = (unsigned long)ucontext->uc_mcontext.gregs[REG_RSP];
#else
	pc = frame = stack = 0;
	(void)ucontext;
#endif
	length = snprintf(line, sizeof(line), PLATFORM_LOG_PREFIX "%s (signal %d, code %d) at %p, eip %08lx ebp %08lx esp %08lx\n",
		crash_signal_name(signal_number), signal_number, information ? information->si_code : 0,
		information ? information->si_addr : NULL, pc, frame, stack);
	write(STDERR_FILENO, line, (size_t)length);
	crash_debug_line(line);
	if (stack)
	{
		/* the return address a call through a bad pointer left behind */
		const unsigned *words = (const unsigned *)stack;

		length = snprintf(line, sizeof(line), PLATFORM_LOG_PREFIX "stack %08x %08x %08x %08x %08x %08x\n",
			words[0], words[1], words[2], words[3], words[4], words[5]);
		write(STDERR_FILENO, line, (size_t)length);
		crash_debug_line(line);
	}
#endif
	count = backtrace(frames, 48);
	backtrace_symbols_fd(frames, count, STDERR_FILENO);
	/* (debug.txt too: a player sends that) */
	{
		int index;

		for (index = 0; index < count; index++)
		{
			snprintf(line, sizeof(line), PLATFORM_LOG_PREFIX "called from %p\n", frames[index]);
			crash_debug_line(line);
		}
	}
}

/* hands the signal to what handled it before this file's handler, which
is put back: a fault returned from runs its instruction again under it, and
a signal sent (kill, abort's raise) is raised again for it, once this
handler returns */
static void crash_chain(int index, int signal_number, siginfo_t *information, void *context)
{
	struct sigaction previous = previous_actions[index];

	if (crash_chaining)
	{
		/* (back here from the handler chained to: its default ends it) */
		signal(signal_number, SIG_DFL);
		if (!information || information->si_code <= 0)
			raise(signal_number);
		return;
	}
	crash_chaining = 1;
	sigaction(signal_number, &previous, NULL);
	if (previous.sa_flags & SA_SIGINFO)
	{
		if (previous.sa_sigaction)
			previous.sa_sigaction(signal_number, information, context);
	}
	else if (previous.sa_handler == SIG_DFL)
	{
		if (!information || information->si_code <= 0)
			raise(signal_number);
	}
	else if (previous.sa_handler != SIG_IGN)
	{
		previous.sa_handler(signal_number);
	}
	crash_chaining = 0;
}

static void crash_handler(int signal_number, siginfo_t *information, void *context)
{
	int index = crash_signal_index(signal_number);

#ifdef HALO_64BIT
	/* (macOS reports a write to a read-only page as SIGBUS, Linux as
	SIGSEGV) */
	if ((signal_number == SIGSEGV || signal_number == SIGBUS) && information)
	{
		unsigned long long offset = (unsigned long long)(uintptr_t)information->si_addr - XBOX_ADDRESS_SPACE_BASE;

		if (offset < XBOX_ADDRESS_SPACE_SIZE && xbox_address_is_contiguous((unsigned int)offset))
		{
			unsigned int page = page_index((unsigned int)offset);

			/* a write to a page the renderer watches: noted, and let through */
			if (page < watch_page_count && page_protected[page] == 1)
			{
				mark_written(page);
				return;
			}
			if (page < watch_page_count && page_protected[page] == 2)
				return;
		}
	}
#else
	if (signal_number == SIGSEGV && information && platform_is_contiguous(information->si_addr))
	{
		unsigned long page = page_index((unsigned long)information->si_addr);

		/* a write to a page the renderer watches: noted, and let through */
		if (page < WATCH_PAGE_COUNT && page_protected[page] == 1)
		{
			mark_written(page);
			return;
		}
		if (page < WATCH_PAGE_COUNT && page_protected[page] == 2)
			return;
	}
#endif
	if (index < 0)
		return;
	/* (a signal sent, not a fault, that was ignored before: still ignored) */
	if (!crash_chaining && previous_actions[index].sa_handler == SIG_IGN &&
		!(previous_actions[index].sa_flags & SA_SIGINFO) && information && information->si_code <= 0)
	{
		return;
	}
	/* a genuine crash: report it (once), then hand it on */
	if (!crash_chaining)
		crash_report(signal_number, information, context);
	crash_chain(index, signal_number, information, context);
}

static BOOL crash_handler_installed(int signal_number, struct sigaction *current)
{
	return sigaction(signal_number, NULL, current) == 0 && (current->sa_flags & SA_SIGINFO) &&
		current->sa_sigaction == crash_handler;
}

/* the handler, for each signal where it is not the one installed (first, or
a library put its own in since: a GL driver's, a sound library's), chained
to the one that is; TRUE if SIGSEGV's (and on 64-bit, SIGBUS's) is this
file's */
static BOOL crash_handlers_install(void)
{
	struct sigaction action, current;
	int index;

	memset(&action, 0, sizeof(action));
	action.sa_sigaction = crash_handler;
	/* (SA_NODEFER: a fault in the handler, or a write fault of the watch
	in a nested one, comes back here) */
	action.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&action.sa_mask);
	for (index = 0; index < CRASH_SIGNAL_COUNT; index++)
	{
		if (crash_handler_installed(crash_signals[index], &current))
			continue;
		previous_actions[index] = current;
		sigaction(crash_signals[index], &action, NULL);
		/* (a handler chained to that jumped out of the signal, instead of
		returning, left the chaining unfinished) */
		crash_chaining = 0;
	}
#ifdef HALO_64BIT
	return crash_handler_installed(SIGSEGV, &current) && crash_handler_installed(SIGBUS, &current);
#else
	return crash_handler_installed(SIGSEGV, &current);
#endif
}

/* at the renderer's device (d3d8_gl.c), before any page is protected; and
from the start (below). A library that installed a SIGSEGV handler in
between (the GL driver's, as the window opened) would get the protected
pages' faults, and crash or hang at the first write to a texture: the
handler goes in again, chained to that one */
void memory_watch_initialize(void)
{
#ifdef HALO_64BIT
	/* (the host's page size, which xbox_memory.c knows once it mapped the
	address space, after the start's call: taken while no page is
	protected) */
	if (!watch_used && platform_host_page_size)
	{
		watch_page_size = platform_host_page_size;
		watch_page_count = PLATFORM_CONTIGUOUS_SIZE / watch_page_size;
	}
#endif
	watch_active = crash_handlers_install();
}

/* the handler from the start, for its crash report: the renderer's device
(d3d8_gl.c) installs it too, but a game with debug.null_renderer (the
network tests, net_compare) creates none, and a crash before the device
went unreported */
__attribute__((constructor))
static void memory_watch_install(void)
{
	memory_watch_initialize();
}

#ifdef HALO_64BIT
/* the watched pages [first, last] a range touches; FALSE if none */
static BOOL page_range(unsigned int address, unsigned int size, unsigned int *first, unsigned int *last)
{
	unsigned int end;

	if (!size || !xbox_address_is_contiguous(address))
		return FALSE;
	end = address + size - 1;
	if (end < address || !xbox_address_is_contiguous(end))
		end = PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE - 1;
	*first = page_index(address);
	*last = page_index(end);
	return TRUE;
}

void memory_watch_protect(unsigned int address, unsigned int size)
{
	unsigned int first, last, page;

	if (!watch_active || !page_range(address, size, &first, &last))
		return;
	watch_used = TRUE;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page] != 1)
		{
			page_protected[page] = 1;
			mprotect(page_pointer(page), watch_page_size, PROT_READ);
		}
	}
}
#else
void memory_watch_protect(unsigned long address, unsigned long size)
{
	unsigned long first, last, page;

	if (!watch_active || !size || !platform_is_contiguous((void *)address))
		return;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
	for (page = first; page <= last; page++)
	{
		if (page_protected[page] != 1)
		{
			page_protected[page] = 1;
			mprotect((void *)(PLATFORM_CONTIGUOUS_BASE + page * WATCH_PAGE_SIZE), WATCH_PAGE_SIZE, PROT_READ);
		}
	}
}
#endif

unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
#ifdef HALO_64BIT
	unsigned int first, last, page;
	unsigned long newest = 0;

	if (!page_range(address, size, &first, &last))
		return 0;
#else
	unsigned long first, last, page, newest = 0;

	if (!size || !platform_is_contiguous((void *)address))
		return 0;
	first = page_index(address);
	last = page_index(address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
#endif
	for (page = first; page <= last; page++)
	{
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

unsigned long memory_watch_serial(void)
{
	return current_generation;
}

#ifdef HALO_64BIT
/* the part of a host range inside the contiguous window, as Xbox addresses */
static BOOL clip_to_window(void *address, unsigned int size, unsigned int *start, unsigned int *clipped_size)
{
	unsigned long long begin = (unsigned long long)(uintptr_t)address - XBOX_ADDRESS_SPACE_BASE;
	unsigned long long end = begin + size;
	unsigned long long window_begin = PLATFORM_CONTIGUOUS_BASE;
	unsigned long long window_end = window_begin + PLATFORM_CONTIGUOUS_SIZE;

	if ((uintptr_t)address < XBOX_ADDRESS_SPACE_BASE || !size || end <= window_begin || begin >= window_end)
		return FALSE;
	if (begin < window_begin)
		begin = window_begin;
	if (end > window_end)
		end = window_end;
	*start = (unsigned int)begin;
	*clipped_size = (unsigned int)(end - begin);
	return TRUE;
}
#endif

void memory_watch_prepare_write(void *address, unsigned long size)
{
#ifdef HALO_64BIT
	unsigned int start, clipped_size, first, last, page;

	if (!watch_active || !clip_to_window(address, size, &start, &clipped_size) ||
		!page_range(start, clipped_size, &first, &last))
	{
		return;
	}
#else
	unsigned long start = (unsigned long)address;
	unsigned long first, last, page;

	if (!watch_active || !size)
		return;
	if (start + size <= PLATFORM_CONTIGUOUS_BASE || start >= PLATFORM_CONTIGUOUS_BASE + PLATFORM_CONTIGUOUS_SIZE)
		return;
	if (start < PLATFORM_CONTIGUOUS_BASE)
		start = PLATFORM_CONTIGUOUS_BASE;
	first = page_index(start);
	last = page_index((unsigned long)address + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
#endif
	for (page = first; page <= last; page++)
	{
		if (page_protected[page] == 1)
			mark_written(page);
	}
}

void memory_watch_forget(void *address, unsigned long size)
{
#ifdef HALO_64BIT
	unsigned int start, clipped_size, first, last, page;

	if (!clip_to_window(address, size, &start, &clipped_size) || !page_range(start, clipped_size, &first, &last))
		return;
#else
	unsigned long start = (unsigned long)address;
	unsigned long first, last, page;

	if (!size || !platform_is_contiguous(address))
		return;
	first = page_index(start);
	last = page_index(start + size - 1);
	if (last >= WATCH_PAGE_COUNT)
		last = WATCH_PAGE_COUNT - 1;
#endif
	for (page = first; page <= last; page++)
	{
		page_protected[page] = 0;
		page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	}
}

/* page protection sees each write at once; once a frame, the handler is
checked to be the one installed (a library may put its own in at any time:
a sound device opened, a gamepad's) */
void memory_watch_begin_frame(void)
{
	if (watch_active)
		crash_handlers_install();
}
#endif
