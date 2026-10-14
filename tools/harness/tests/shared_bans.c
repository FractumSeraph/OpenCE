/*
SHARED_BANS.C (test)

The real filter of the shared ban lists (port/linux/src/shared_bans.c): what
a list another host publishes may put in this host's shared_bans.txt, which
its join check reads as bans. test_shared_bans.py takes the constants
(config.inc) and the code (under_test.inc).
*/

#include "harness.h"
#include "config.inc"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "under_test.inc"

static char out[4096];

static size_t filtered(char const *text)
{
	return shared_bans_filter(text, strlen(text), "example.org/bans.txt", out, sizeof(out));
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a ban line kept whole, with the list it came from after it */
	CASE("keeps-bans")
	{
		filtered("2026-10-14 10:00:00\thwid=0123456789abcdef0123456789abcdef\tplayers=Cheater\treason=aimbot\n");
		CHECK(!strcmp(out, "2026-10-14 10:00:00\thwid=0123456789abcdef0123456789abcdef\tplayers=Cheater\t"
			"reason=aimbot\tlist=example.org/bans.txt\n"), "kept: %s", out);
		filtered("ip=203.0.113.9\tuntil=2000000000\r\n");
		CHECK(!strcmp(out, "ip=203.0.113.9\tuntil=2000000000\tlist=example.org/bans.txt\n"), "an address's: %s", out);
		return 0;
	}
	/* comments, blank lines and anything not a ban left out */
	CASE("leaves-out-the-rest")
	{
		filtered("#\thwid=abc\n\n<html>hwid=x</html>\n");
		CHECK(out[0] == 0, "a comment kept: %s", out);
		filtered("<html><body>players=hwid=abc</body></html>\nreason=no ban here\n");
		CHECK(out[0] == 0, "not a ban, kept: %s", out);
		return 0;
	}
	/* a field's name counts only at a field's start: "xhwid=" is none */
	CASE("whole-field-names")
	{
		filtered("players=xhwid=1\treason=aip=2\n");
		CHECK(out[0] == 0, "a name inside another kept: %s", out);
		return 0;
	}
	/* no control characters (but tabs), nothing longer than a ban line */
	CASE("printable-and-short")
	{
		char line[600];

		filtered("hwid=abc\x1b[2J\n");
		CHECK(out[0] == 0, "a control character kept: %s", out);
		memset(line, 'a', sizeof(line) - 1);
		memcpy(line, "hwid=", 5);
		line[sizeof(line) - 1] = 0;
		filtered(line);
		CHECK(out[0] == 0, "a line of %d kept", (int)strlen(line));
		return 0;
	}
	/* a list larger than the room: whole lines only, NUL terminated */
	CASE("whole-lines-in-the-room")
	{
		char small[60];
		size_t length = shared_bans_filter("hwid=1\nhwid=2\nhwid=3\n", 21, "example.org/bans.txt", small,
			sizeof(small));

		CHECK(length == strlen(small), "%d written, %d long", (int)length, (int)strlen(small));
		CHECK(!strcmp(small, "hwid=1\tlist=example.org/bans.txt\n"), "kept: %s", small);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
