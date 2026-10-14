# Mbed TLS

A TLS library in C99, by the Mbed TLS contributors, licensed under the
Apache License 2.0 or the GNU General Public License 2.0 or later (see
`LICENSE`).

Upstream: https://github.com/Mbed-TLS/mbedtls, release `mbedtls-3.6.7` (the
3.6 long-term support branch), `mbedtls-3.6.7.tar.bz2`, SHA-256
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`.

Only `library/`, `include/` and `LICENSE` of the release are kept, without
their build files, and nothing in them is changed. The default configuration
(`include/mbedtls/mbedtls_config.h`) is used, with TLS 1.2 and 1.3, and with
its locks: the builds define `MBEDTLS_THREADING_C` for every unit that
includes its headers, with `MBEDTLS_THREADING_PTHREAD` on Linux, macOS and
Android (`tools/linux_build.py` `MBEDTLS_DEFINES`) and, on Windows,
`MBEDTLS_THREADING_ALT` with slim reader/writer locks
(`port/windows/mbedtls/threading_alt.h`, `port/windows/src/win32_mbedtls.c`).
Several threads use it at once: the game thread's DTLS, and the HTTPS of the
game list, Delta Stats, the dedicated server's control link and the
downloads, which share its PSA crypto (its setup, random generator and key
slots).

Every native build's internet play uses it for DTLS with browsers
(`port/linux/src/posix_dtls.c`: WebRTC's DTLS 1.2 server, with a
self-signed certificate made each run): the Linux and Windows builds, and
the Android app's host library. The web build has none of it (a browser has
its own WebRTC).

The Linux build's self-updater (`port/linux/src/posix_update.c`) uses it to
download new builds over HTTPS, with the system's certificate authorities.
The Windows build uses WinHTTP instead, and the Android app Java's HTTPS.
