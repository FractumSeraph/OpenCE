/*
CHAT_KEYS.C (test)

Text chat and the keyboard: the real keystroke filter
(port/linux/src/xinput_sdl.c: XInputDebugGetKeystroke, platform_chat_typing)
feeding the real chat line (port/linux/game/chat.c: typing_keys, typing_end).
While a chat line is up, every key the player types reaches it, Enter says it,
Escape closes it, and either gives the keyboard back to the game
(platform_chat_typing: while it says chat is up, no bound key drives the game).
ChupathingyCE's first chat build passed no key press while the line was up, so
the line could never be closed and the keyboard drove nothing for the rest of
the game. test_chat_keys.py takes the code (keys.inc, under_test.inc).
*/

#include "harness.h"

#include <stdio.h>
#include <string.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned char BYTE;
typedef char CHAR;
typedef int INT;
#define WINAPI
#define ERROR_SUCCESS 0
#define ERROR_HANDLE_EOF 38
#define XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP 0x40
#define VK_OEM_3_BACKQUOTE 0xc0
#define csmemcpy memcpy

#include "keys.inc"

typedef struct _XINPUT_DEBUG_KEYSTROKE *PXINPUT_DEBUG_KEYSTROKE;

/* the key layer's state (xinput_sdl.c's statics) */
static BOOL text_typing;
static BOOL chat_typing;
/* (escape seen as the key itself while a line is up: xinput_sdl.c) */
static BOOL chat_escape_pressed;
static boolean console_active;
#define console_is_active() console_active

/* the platform's queue of key events (sdl_platform.c) */
static struct platform_keystroke queue[64];
static int queue_head, queue_count;

static void queue_key(BYTE virtual_key, CHAR ascii, BOOL up)
{
	struct platform_keystroke *keystroke = &queue[(queue_head + queue_count++) % 64];

	keystroke->virtual_key = virtual_key;
	keystroke->ascii = up ? 0 : ascii;
	keystroke->flags = up ? XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP : 0;
}

static BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	if (!queue_count)
		return FALSE;
	*keystroke = queue[queue_head];
	queue_head = (queue_head + 1) % 64;
	queue_count--;
	return TRUE;
}

/* the chat line's state (chat.c's) */
enum
{
	INPUT_SIZE = 81
};

static struct
{
	boolean typing;
	short typing_scope;
	boolean log_open;
	char input[INPUT_SIZE];
	short input_length;
} chat;

static char said[INPUT_SIZE];
static int said_count;

static void say(short scope, char const *text, int length)
{
	(void)scope;
	snprintf(said, sizeof(said), "%.*s", length, text);
	said_count++;
}

/* the game's key buffer (input_xbox.c: key presses, key ups only release) */
static boolean input_get_key(struct key_stroke *key);

#include "under_test.inc"

static boolean input_get_key(struct key_stroke *key)
{
	XINPUT_DEBUG_KEYSTROKE keystroke;

	while (XInputDebugGetKeystroke(&keystroke) == ERROR_SUCCESS)
	{
		if (keystroke.Flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP)
			continue;
		key->modifier_flags = 0;
		key->ascii_code = keystroke.Ascii;
		key->key_code = keystroke.VirtualKey == 0x1b ? _key_escape : keystroke.VirtualKey == 0x0d ? _key_return :
			keystroke.VirtualKey == 0x08 ? _key_backspace : _key_a;
		return TRUE;
	}
	return FALSE;
}

/* a key pressed and let go */
static void press(BYTE virtual_key, CHAR ascii)
{
	queue_key(virtual_key, ascii, FALSE);
	queue_key(virtual_key, 0, TRUE);
}

static void type(char const *text)
{
	for (; *text; text++)
		press(*text == ' ' ? 0x20 : (BYTE)(*text >= 'a' && *text <= 'z' ? *text - 'a' + 'A' : *text), *text);
}

/* T: the line comes up (chat.c's typing_begin, as far as the keys go) */
static void open_line(void)
{
	chat.typing = TRUE;
	chat.input[0] = 0;
	chat.input_length = 0;
	/* (an escape pressed before the line came up does not close it) */
	chat_escape_pressed = TRUE;
	platform_chat_typing(TRUE);
	CHECK(chat_typing && !chat_escape_pressed, "the key layer's chat %d, escape pressed %d", chat_typing,
		chat_escape_pressed);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a line typed and said with Enter: the keyboard is the game's again */
	if (!strcmp(case_name, "enter"))
	{
		open_line();
		type("gg all");
		press(0x0d, '\r');
		typing_keys();
		CHECK(said_count == 1 && !strcmp(said, "gg all"), "said %d lines, the last \"%s\"", said_count, said);
		CHECK(!chat.typing && !chat_typing, "the line is still up (typing %d, the key layer's %d)", chat.typing,
			chat_typing);
		return 0;
	}
	/* Escape closes the line unsaid: the keyboard is the game's again */
	if (!strcmp(case_name, "escape"))
	{
		open_line();
		type("oops");
		press(0x1b, 0x1b);
		typing_keys();
		CHECK(said_count == 0, "a line was said");
		CHECK(!chat.typing && !chat_typing, "the line is still up (typing %d, the key layer's %d)", chat.typing,
			chat_typing);
		return 0;
	}
	/* Backspace takes back a letter */
	if (!strcmp(case_name, "backspace"))
	{
		open_line();
		type("hix");
		press(0x08, 0x08);
		press(0x0d, '\r');
		typing_keys();
		CHECK(said_count == 1 && !strcmp(said, "hi"), "said \"%s\"", said);
		return 0;
	}
	/* no line up: a key press is the game's, not text (only key ups pass) */
	if (!strcmp(case_name, "closed"))
	{
		XINPUT_DEBUG_KEYSTROKE keystroke;

		press('W', 'w');
		CHECK(XInputDebugGetKeystroke(&keystroke) == ERROR_SUCCESS &&
			(keystroke.Flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP), "a key press passed with no line up");
		CHECK(XInputDebugGetKeystroke(&keystroke) == ERROR_HANDLE_EOF, "more keys than the key up");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
