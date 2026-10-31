"""The approved map list as published between releases (port/linux/src/map_index.h):
the index's serial as tools/map_torrents.py writes it, its signature as
"map_torrents.py sign" makes it (the fork's key: a test key here), and the
game's checks (port/linux/src/map_index.c), built by themselves with that key
and held to the same files."""
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delta_table  # noqa: E402
import map_torrents  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "port" / "linux" / "src"
MONOCYPHER = ROOT / "port" / "third_party" / "monocypher"
TOOL = ROOT / "tools" / "map_torrents.py"

DRIVER = r"""
#include "map_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	*size = fread(data, 1, (size_t)length, file);
	data[*size] = 0;
	fclose(file);
	return data;
}

/* index <file> <sig>: format, serial, check; cache <file>: split, check */
int main(int argc, char **argv)
{
	unsigned char signature[MAP_INDEX_SIGNATURE_SIZE];
	size_t size = 0, signature_size = 0, offset = 0;
	char *document, *text;

	if (argc == 4 && !strcmp(argv[1], "index"))
	{
		document = slurp(argv[2], &size);
		text = slurp(argv[3], &signature_size);
		if (!document || !text)
			return 2;
		if (!map_index_signature_parse(text, signature_size, signature))
		{
			printf("format=%d serial=%llu signature=bad\n", map_index_format(document, size),
				map_index_serial(document, size));
			return 0;
		}
		printf("format=%d serial=%llu check=%d\n", map_index_format(document, size),
			map_index_serial(document, size), map_index_signature_check(signature, document, size));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "cache"))
	{
		text = slurp(argv[2], &size);
		if (!text)
			return 2;
		if (!map_index_split(text, size, signature, &offset))
		{
			printf("split=0\n");
			return 0;
		}
		printf("split=1 serial=%llu check=%d\n", map_index_serial(text + offset, size - offset),
			map_index_signature_check(signature, text + offset, size - offset));
		return 0;
	}
	return 2;
}
"""

INDEX = """# halo map torrents v1
# serial 1793481173
# a comment
bloodgulch.map\t1234ABCD\t1000000\t262144\t0123456789abcdef0123456789abcdef01234567\tshare:{share}
""".replace("{share}", "ab" * 32)


def compiler():
    for name in ("clang", "cc", "gcc"):
        if shutil.which(name):
            return name
    return None


@pytest.fixture(scope="module")
def keys(tmp_path_factory):
    try:
        delta_table.openssl()
    except SystemExit:
        pytest.skip("needs OpenSSL 3")
    folder = tmp_path_factory.mktemp("keys")
    primary, other = folder / "primary.pem", folder / "other.pem"
    return (primary, delta_table.keygen(primary)), (other, delta_table.keygen(other))


@pytest.fixture(scope="module")
def checker(keys, tmp_path_factory):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    folder = tmp_path_factory.mktemp("checker")
    driver = folder / "driver.c"
    driver.write_text(DRIVER)
    key = ",".join(f"0x{byte:02x}" for byte in keys[0][1])
    program = folder / "checker"
    # (Monocypher by itself, as C99; map_index.c as the port's C: gnu89)
    objects = []
    for source in (MONOCYPHER / "monocypher.c", MONOCYPHER / "monocypher-ed25519.c"):
        objects.append(folder / (source.stem + ".o"))
        built = subprocess.run([cc, "-std=c99", "-O1", "-c", "-I", str(MONOCYPHER), str(source), "-o",
                                str(objects[-1])], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr[-4000:]
    built = subprocess.run([cc, "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wdeclaration-after-statement", "-O1",
                            "-g", f"-DHALO_MAP_INDEX_TEST_KEY={key}", "-I", str(SRC), "-I", str(MONOCYPHER),
                            str(driver), str(SRC / "map_index.c"), *map(str, objects), "-o", str(program)],
                           capture_output=True, text=True)
    assert built.returncode == 0, built.stderr[-4000:]
    return program


def run(program, *arguments):
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    return result.stdout.strip()


def sign(key, index):
    result = subprocess.run([sys.executable, str(TOOL), "sign", str(key), "--index", str(index)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return Path(f"{index}.sig")


def test_signed_index_checks(keys, checker, tmp_path):
    index = tmp_path / "map_torrents.txt"
    index.write_bytes(INDEX.encode())
    sig = sign(keys[0][0], index)
    assert run(checker, "index", index, sig) == "format=1 serial=1793481173 check=1"
    verified = subprocess.run([sys.executable, str(TOOL), "verify", "--public", keys[0][1].hex(), "--index",
                               str(index)], capture_output=True, text=True)
    assert verified.returncode == 0, verified.stdout + verified.stderr
    # (the cache's form: the signature's digits, a line feed, the index)
    cache = tmp_path / "map_torrents.signed"
    cache.write_bytes(sig.read_bytes().strip() + b"\n" + index.read_bytes())
    assert run(checker, "cache", cache) == "split=1 serial=1793481173 check=1"


def test_changed_or_other_key_or_bare_signature_refused(keys, checker, tmp_path):
    index = tmp_path / "map_torrents.txt"
    index.write_bytes(INDEX.encode())
    sig = sign(keys[0][0], index)
    # a map slipped in after signing
    index.write_bytes(INDEX.encode() + b"evil.map\t00000001\t5\t262144\t" + b"f" * 40 + b"\n")
    assert run(checker, "index", index, sig).endswith("check=0")
    # another key's signature
    index.write_bytes(INDEX.encode())
    other = sign(keys[1][0], index)
    assert run(checker, "index", index, other).endswith("check=0")
    # the key's signature of the bare index, without the context (as a
    # release's raw signature would be): not the list's
    sys.path.insert(0, str(ROOT / "tools"))
    from update_sign import sign_bytes
    sig.write_text(sign_bytes(keys[0][0], index.read_bytes()).hex() + "\n")
    assert run(checker, "index", index, sig).endswith("check=0")
    verified = subprocess.run([sys.executable, str(TOOL), "verify", "--public", keys[0][1].hex(), "--index",
                               str(index)], capture_output=True, text=True)
    assert verified.returncode == 1


def test_serial_reading(keys, checker, tmp_path):
    sig = tmp_path / "x.sig"
    sig.write_text("00" * 64 + "\n")
    cases = {
        INDEX: 1793481173,
        INDEX.replace("\n", "\r\n"): 1793481173,
        INDEX.replace("# serial 1793481173\n", ""): 0,
        INDEX.replace("# serial 1793481173", "# serial 12abc"): 0,
        INDEX.replace("# serial 1793481173", "# serial " + "9" * 20): 0,
        # (only among the comment lines the index opens with)
        INDEX.replace("# serial 1793481173\n", "") + "# serial 99\n": 0,
    }
    for text, serial in cases.items():
        index = tmp_path / "case.txt"
        index.write_bytes(text.encode())
        assert run(checker, "index", index, sig).startswith(f"format=1 serial={serial} "), text
        assert map_torrents.index_serial(text.replace("\r\n", "\n")) == serial, text
    index = tmp_path / "case.txt"
    index.write_bytes(b"# halo map torrents v2\n# serial 5\n")
    assert run(checker, "index", index, sig).startswith("format=0 ")


def test_cache_form_refused_when_broken(checker, tmp_path):
    cache = tmp_path / "map_torrents.signed"
    for text in (b"", b"00" * 64, b"00" * 64 + b" " + INDEX.encode(), b"zz" * 64 + b"\n" + INDEX.encode()):
        cache.write_bytes(text)
        assert run(checker, "cache", cache) == "split=0"


def test_write_index_raises_the_serial(tmp_path, monkeypatch):
    index = tmp_path / "map_torrents.txt"
    index.write_bytes(INDEX.encode())
    entries = map_torrents.read_index(index)
    # (unchanged: kept)
    map_torrents.write_index(index, entries)
    first = map_torrents.index_serial(index.read_text())
    map_torrents.write_index(index, entries)
    assert map_torrents.index_serial(index.read_text()) == first
    # (changed: raised, past the old one even with the clock behind it)
    monkeypatch.setattr(map_torrents.time, "time", lambda: 1000.0)
    entries.pop(next(iter(entries)))
    map_torrents.write_index(index, entries)
    assert map_torrents.index_serial(index.read_text()) == first + 1
