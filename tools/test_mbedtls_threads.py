"""Mbed TLS used by several threads at once, as the game uses it: the game
thread's DTLS with browsers (port/linux/src/posix_dtls.c) beside the other
threads' HTTPS (tools/mbedtls_threads_check.c), built as the builds build it
(tools/linux_build.py MBEDTLS_DEFINES) and with ThreadSanitizer, which must
see no race.

    python -m pytest tools/test_mbedtls_threads.py
"""

import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools.linux_build import MBEDTLS_DEFINES  # noqa: E402

MBEDTLS = ROOT / "port" / "third_party" / "mbedtls"
SOURCES = [ROOT / "tools" / "mbedtls_threads_check.c", ROOT / "port" / "linux" / "src" / "posix_dtls.c"]
SANITIZER = ["-fsanitize=thread"]
# (Mbed TLS's elliptic curves' operation counts, ecp.c's, which only its
# self-test reads, the game never: counted without a lock, as Mbed TLS has
# them in every build with MBEDTLS_SELF_TEST, its default)
SUPPRESSIONS = "".join(f"race:{name}\n" for name in ("add_count", "dbl_count", "mul_count"))


def compiler():
    for name in ("clang", "cc", "gcc"):
        path = shutil.which(name)
        if path:
            return path
    return None


def build(cc, folder, defines):
    """the check, with Mbed TLS's library, all with ThreadSanitizer; the
    compiler's result and the program"""
    flags = ["-std=gnu11", "-O1", "-g", *SANITIZER, "-D_GNU_SOURCE", f"-I{MBEDTLS / 'include'}", *defines]
    sources = sorted((MBEDTLS / "library").glob("*.c"))

    def compile_one(source):
        obj = folder / (source.stem + ".o")
        return subprocess.run([cc, *flags, "-w", f"-I{MBEDTLS / 'library'}", "-c", str(source), "-o", str(obj)],
                              capture_output=True, text=True), obj

    folder.mkdir(parents=True, exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        compiled = list(pool.map(compile_one, sources))
    for result, _ in compiled:
        if result.returncode != 0:
            return result, None
    program = folder / "mbedtls_threads_check"
    result = subprocess.run([cc, *flags, "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'port' / 'linux' / 'src'}",
                             *map(str, SOURCES), *(str(obj) for _, obj in compiled), "-o", str(program), "-lpthread"],
                            capture_output=True, text=True)
    return result, program


def test_threads_share_mbedtls_without_races(tmp_path):
    cc = compiler()
    if not cc:
        pytest.skip("no C compiler")
    if os.name != "posix":
        pytest.skip("the check is POSIX's (pthreads)")
    result, program = build(cc, tmp_path, MBEDTLS_DEFINES)
    if result.returncode != 0 and "tsan" in (result.stdout + result.stderr).lower():
        pytest.skip("a compiler without ThreadSanitizer's runtime")
    assert result.returncode == 0, result.stdout + result.stderr
    suppressions = tmp_path / "suppressions.txt"
    suppressions.write_text(SUPPRESSIONS)
    environment = dict(os.environ, TSAN_OPTIONS=f"halt_on_error=1 exitcode=66 suppressions={suppressions}")
    run = subprocess.run([str(program)], capture_output=True, text=True, timeout=600, env=environment)
    if "unexpected memory mapping" in run.stderr:
        # (an older ThreadSanitizer, under a kernel's wider address
        # randomization: without it, where the container allows that)
        setarch = shutil.which("setarch")
        if not setarch:
            pytest.skip("ThreadSanitizer cannot run here (unexpected memory mapping)")
        run = subprocess.run([setarch, os.uname().machine, "-R", str(program)], capture_output=True, text=True,
                             timeout=600, env=environment)
        if "Operation not permitted" in run.stderr:
            pytest.skip("ThreadSanitizer cannot run here (unexpected memory mapping, and no setarch -R)")
    assert "ThreadSanitizer" not in run.stderr, run.stderr[-6000:]
    assert run.returncode == 0 and "PASS" in run.stdout, run.stdout + run.stderr[-6000:]
