"""Ninja rules for the browser build (``ninja web``).

The browser port is the same 32-bit game code and SDL platform layer used by
the native ports, compiled to WebAssembly with Emscripten.  ``HALO_ANDROID``
selects the existing ILP32/OpenGL ES code paths while ``HALO_WEB`` lets the
small browser-specific parts of the platform layer distinguish themselves
from Android.

The link uses a pinned SDL3 port, WebGL 2 and pthreads.  It deliberately
starts with a memory larger than 2 GiB: the Xbox-compatible allocator owns the
fixed 0x80000000..0x88000000 address range.  FetchFS and OPFS are linked for
the browser platform layer to expose streamed game data and persistent saves.
"""

import json
import os
import shutil
from pathlib import Path
from typing import Any, Dict, List

from .android_build import VARIADIC_PROTOTYPE_FILES
from .embed_assets import hud_assets_build, hud_configure_inputs
from .linux_build import (
    EXPAT_DIR,
    EXPAT_SOURCES,
    KCP_DIR,
    MONOCYPHER_DIR,
    MUSL_MATH_DIR,
    TOML_DIR,
    XDK_INCLUDE,
    ZLIB_DEFINES,
    ZLIB_DIR,
    ZLIB_SOURCES,
    compile_launcher,
    game_defines_and_includes,
    game_sources,
    musl_math_sources,
    opus_cflags,
    opus_sources,
    xdk_headers,
)
from .ninja_syntax import Writer


LINUX_DIR = Path("port/linux")
ANDROID_DIR = Path("port/android")
WEB_DIR = Path("port/web")
PORT_CONFIG = LINUX_DIR / "port.json"
WEB_SDL_PORT = WEB_DIR / "halo_sdl3.py"
WEB_SDL_FLAG = f"--use-port={WEB_SDL_PORT}"

# Emscripten's wasm32 ABI already has the pointer and long sizes the original
# Xbox code expects.  The remaining flags reproduce the source-level MSVC ABI
# assumptions shared by the other ports.
WEB_ABI_FLAGS = [
    "-DHALO_WEB=1",
    # (ChupathingyCE's Custom Edition maps, as every native build has them:
    # linux_build.py's CUSTOM_EDITION_DEFINES)
    "-DHALO_CUSTOM_EDITION",
    # (OpenCE's three parts of what HALO_ANDROID meant, all of which the
    # browser build is: the 32-bit guest's code paths, the OpenGL ES
    # renderer (WebGL 2), and the app's; ChupathingyCE's code still says
    # HALO_ANDROID for all three)
    "-DHALO_ARM64_GUEST=1",
    "-DHALO_GLES=1",
    "-DHALO_ANDROID=1",
    "-fms-extensions",
    "-fshort-wchar",
    "-fcommon",
    "-fno-strict-aliasing",
    "-fwrapv",
    "-fno-delete-null-pointer-checks",
    "-fno-omit-frame-pointer",
    "-ffp-contract=off",
    "-O2",
    "-pthread",
    WEB_SDL_FLAG,
    # The C library's wide string functions assume a 32-bit wchar_t; stop
    # clang turning the 16-bit loops of msvc_wide.c (and the game's own) back
    # into calls to them, as the Linux build does (linux_build.py). Without
    # this msvc_wcslen became libc's wcslen and overran 16-bit strings.
    *(f"-fno-builtin-{name}" for name in (
        "wcslen", "wcsnlen", "wcschr", "wcsrchr", "wcscmp", "wcsncmp", "wcscpy",
        "wcsncpy", "wcscat", "wcsncat", "wmemchr", "wmemcmp", "wmemcpy",
        "wmemmove", "wmemset",
    )),
]

GAME_FLAGS = [
    "-std=gnu89",
    "-D__STRICT_ANSI__",
    "-w",
    "-Wno-error=incompatible-pointer-types",
    "-Wno-error=incompatible-function-pointer-types",
    "-Wno-error=int-conversion",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-Wno-error=return-type",
]

PLATFORM_FLAGS = [
    "-std=gnu11",
    "-D_GNU_SOURCE",
    "-DHALO_LINUX_PLATFORM_LAYER",
    "-w",
]

# A browser build has no self-updater (the page is always the current build)
# and no UPnP (a browser cannot open router ports; online play goes through
# WebRTC). posix_trace_marker.c keeps a Linux GPU driver's open() of the
# kernel's trace marker (SteamOS), which a browser has neither of. The xiso
# unit is empty when the HALO_ANDROID data-import path is selected, so leaving
# it in is harmless.
WEB_EXCLUDED_PLATFORM_SOURCES = {
    "posix_trace_marker.c",
    "posix_update.c",
    "posix_upnp.c",
    "updater.c",
    # The Custom Edition maps' BitTorrent client (docs/map_torrents.md): a
    # browser has no TCP or UDP sockets for BitTorrent, so the engine is left
    # out and port/web/src/web_map_torrents.c stands in for map_torrents.h.
    "map_torrents.c",
    "torrent.c",
    "torrent_peer.c",
    "torrent_tracker.c",
    "torrent_dht.c",
    "torrent_bencode.c",
    "torrent_sha1.c",
    # ChupathingyCE's Android touchscreen (its host's events): the page has its
    # own touch controls (port/web/assets/touch); port/web/src/web_touch_input.c
    # stands in for touch_input.h
    "touch_input.c",
}


def _quote(path: Any) -> str:
    text = str(path).replace(os.sep, "/")
    return f'"{text}"' if " " in text else text


def _emcc(sln: Any) -> str:
    configured = getattr(sln, "web_cc", None)
    if configured:
        return configured
    local = Path("build/emsdk/upstream/emscripten/emcc")
    if local.is_file():
        return str(local)
    return shutil.which("emcc") or "emcc"


def _load_port_config() -> Dict[str, Any]:
    with PORT_CONFIG.open("r", encoding="utf-8") as file:
        return json.load(file)


def web_configure_inputs() -> List[Path]:
    """Files whose changes must regenerate ``build.ninja``."""
    if not PORT_CONFIG.is_file():
        return [Path(__file__)]
    # (the folders of the game's sources, so that adding or removing one
    # re-runs it, as for the native builds)
    game_folders = sorted({source.parent for source in game_sources(_load_port_config())})
    return [
        Path(__file__),
        PORT_CONFIG,
        WEB_DIR / "shell.html",
        WEB_SDL_PORT,
        WEB_DIR / "assets",
        WEB_DIR / "src",
        LINUX_DIR / "src",
        LINUX_DIR / "game",
        XDK_INCLUDE,
        *game_folders,
        *hud_configure_inputs(),
    ]


def generate_web_build(n: Writer, sln: Any) -> None:
    if not PORT_CONFIG.is_file() or not (WEB_DIR / "shell.html").is_file():
        return

    config = _load_port_config()
    build_dir: Path = sln.build_dir / "web"
    obj_dir = build_dir / "obj"
    output = build_dir / "halo.html"
    javascript_output = build_dir / "halo.js"
    wasm_output = build_dir / "halo.wasm"
    cc = _emcc(sln)
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"
    semantics_header = build_dir / "halo_msvc_semantics.h"
    platform_semantics_header = build_dir / "platform_msvc_semantics.h"
    port_include = LINUX_DIR / "include"
    platform_dir = Path(config["platform_sources"])

    n.comment("Browser WebAssembly build (ninja web)")
    # Windows: CreateProcess cannot start emcc.bat by itself; run it via cmd.
    if os.name == "nt" and str(cc).lower().endswith((".bat", ".cmd")):
        n.variable("web_cc", "cmd /c " + str(cc).replace("/", "\\"))
    else:
        n.variable("web_cc", _quote(cc))

    n.rule(
        name="web_msvc_semantics",
        command="$python tools/linux_msvc_semantics.py --output $out $scan",
        description="WEB MSVC SEMANTICS $out",
        restat=True,
    )
    game_headers = sorted(
        path for path in Path("source").rglob("*") if path.suffix in (".c", ".h")
    )
    n.build(
        outputs=semantics_header,
        rule="web_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers(), *game_headers],
        variables={
            "scan": f"--all-inlines --tags source --inlines source --inlines {XDK_INCLUDE}"
        },
    )
    n.build(
        outputs=platform_semantics_header,
        rule="web_msvc_semantics",
        implicit=[Path("tools/linux_msvc_semantics.py"), *xdk_headers()],
        variables={"scan": f"--inlines {XDK_INCLUDE}"},
    )

    n.rule(
        name="web_cc",
        command=f"{compile_launcher(sln)}$web_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="WEB CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="web_link",
        command="$web_cc $ldflags -o $out @$out.rsp $libs",
        description="WEB LINK $out",
        rspfile="$out.rsp",
        # emcc splits response files with POSIX shell rules, so Windows
        # backslash paths would be read as escapes; use forward slashes there.
        rspfile_content="$link_rsp" if os.name == "nt" else "$in_newline",
    )

    release = getattr(sln, "port_release", False)
    release_flags = ["-DHALO_RELEASE"] if release else []
    debug_flags = [] if release else ["-g"]
    abi_flags = " ".join([*WEB_ABI_FLAGS, *release_flags, *debug_flags])
    sdk_flags = f"-idirafter {XDK_INCLUDE}"
    implicit_headers = [
        *xdk_headers(),
        prefix_header,
        semantics_header,
        platform_semantics_header,
    ]
    objects: List[Path] = []

    def add_object(source: Path, cflags: str, prefix: str = "") -> None:
        relative = Path(str(source).lstrip("/"))
        obj = obj_dir / prefix / relative.with_suffix(".o")
        objects.append(obj)
        n.build(
            outputs=obj,
            rule="web_cc",
            inputs=source,
            implicit=implicit_headers,
            variables={"cflags": cflags},
        )

    game_cflags = " ".join(
        [
            abi_flags,
            " ".join(GAME_FLAGS),
            f"-include {prefix_header}",
            f"-include {semantics_header}",
            f"-I{port_include}",
            # the headers of the port's own game units (port/linux/game)
            f"-iquote {Path(config['game_sources'])}",
            game_defines_and_includes(config),
            sdk_flags,
        ]
    )
    for source in game_sources(config):
        cflags = game_cflags
        if source.as_posix() in VARIADIC_PROTOTYPE_FILES:
            cflags += f" -include {ANDROID_DIR}/include/halo_android_variadic_prototypes.h"
        add_object(source, cflags)
    for source in sorted(Path(config["game_sources"]).glob("*.c")):
        add_object(source, game_cflags)

    platform_cflags = " ".join(
        [
            abi_flags,
            " ".join(PLATFORM_FLAGS),
            f"-include {prefix_header}",
            f"-include {platform_semantics_header}",
            f"-I{platform_dir}",
            f"-I{port_include}",
            f"-I{WEB_DIR}/src",
            f"-I{TOML_DIR}",
            f"-I{EXPAT_DIR}",
            f"-I{KCP_DIR}",
            f"-I{MONOCYPHER_DIR}",
            f"-I{ZLIB_DIR}",
            "-Isource",
            "-Isource/cseries",
            sdk_flags,
        ]
    )
    # These units are the libc boundary in the native build.  Compile them
    # without the force-included MSVC compatibility header so Emscripten's
    # system structures and inline definitions retain their normal ABI.
    posix_cflags = " ".join(
        [
            "-DHALO_WEB=1",
            "-DHALO_ARM64_GUEST=1",
            "-DHALO_GLES=1",
            "-DHALO_ANDROID=1",
            "-std=gnu11",
            "-D_GNU_SOURCE",
            "-O2",
            "-g",
            "-pthread",
            "-w",
            f"-I{platform_dir}",
        ]
    )
    for source in sorted(platform_dir.glob("*.c")):
        if source.name in WEB_EXCLUDED_PLATFORM_SOURCES:
            continue
        add_object(source, posix_cflags if source.name.startswith("posix_") else platform_cflags)

    # Browser-only adapters live beside the shell and use the same platform
    # ABI.  The glob intentionally works when that directory is still empty.
    for source in sorted((WEB_DIR / "src").glob("*.c")):
        # The loopback socket backend is part of the libc boundary and needs
        # the host sockaddr ABI, just like posix_net.c.
        add_object(
            source,
            posix_cflags if source.name == "web_loopback_net.c" else platform_cflags,
        )

    # the high-res HUD's textures, the menus' titles, fonts and XML menus
    # (port/assets; port/linux/src/hud_hires.c), generated as C data
    for source in hud_assets_build(n, "web", build_dir / "generated" / "hud_hires_assets.c"):
        add_object(source, platform_cflags)
    add_object(TOML_DIR / "tomlc17.c", f"{abi_flags} -std=gnu11 -w")
    # the menus' XML parser (menu_files.c)
    # the port's zlib (zlib_prefixed.h: what inflates the maps and the menus'
    # and HUD's PNGs), built as linux_build.py builds it
    for name in ZLIB_SOURCES:
        add_object(ZLIB_DIR / name, " ".join([abi_flags, "-std=gnu11", *ZLIB_DEFINES, "-w"]))
    for name in EXPAT_SOURCES:
        add_object(EXPAT_DIR / name, f"{abi_flags} -std=gnu11 -I{EXPAT_DIR} -w")
    add_object(KCP_DIR / "ikcp.c", f"{abi_flags} -std=gnu11 -w")
    # voice chat's codec (port/third_party/opus; network_voice.c), built as
    # linux_build.py builds it
    for source in opus_sources():
        add_object(source, opus_cflags(abi_flags))
    # public games' listing signatures (p2p_crypto.c)
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        add_object(MONOCYPHER_DIR / name, f"{abi_flags} -std=gnu11 -w")
    math_cflags = " ".join(
        [
            abi_flags,
            "-std=gnu11",
            "-w",
            f"-I{MUSL_MATH_DIR}/include",
            f"-include {MUSL_MATH_DIR}/include/libm.h",
        ]
    )
    for source in musl_math_sources():
        add_object(source, math_cflags, "musl-math")

    assertions = "0" if release else "1"
    link_flags = [
        "-O2",
        *debug_flags,
        "-pthread",
        WEB_SDL_FLAG,
        "-sPROXY_TO_PTHREAD=1",
        "-sPTHREAD_POOL_SIZE=16",
        "-sOFFSCREENCANVAS_SUPPORT=1",
        "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas",
        "-sMIN_WEBGL_VERSION=2",
        "-sMAX_WEBGL_VERSION=2",
        "-sFULL_ES3=1",
        "-sGL_ENABLE_GET_PROC_ADDRESS=1",
        "-sWASMFS=1",
        "-sFORCE_FILESYSTEM=1",
        "-sINITIAL_MEMORY=2415919104",
        "-sMAXIMUM_MEMORY=4294967296",
        "-sALLOW_MEMORY_GROWTH=1",
        "-sSTACK_SIZE=5242880",
        "-sDEFAULT_PTHREAD_STACK_SIZE=2097152",
        "-sEXIT_RUNTIME=0",
        f"-sASSERTIONS={assertions}",
        "-sENVIRONMENT=web,worker",
        "-sERROR_ON_UNDEFINED_SYMBOLS=1",
        # wasm-ld's "function signature mismatch" is a call that traps at
        # run time in WebAssembly (an x86 build shrugs it off, e.g. a C89
        # call without a prototype assumed to return int): make it an error
        "-Wl,--fatal-warnings",
        f"--shell-file {WEB_DIR}/shell.html",
        f"--pre-js {WEB_DIR}/xiso.js",
        f"--pre-js {WEB_DIR}/fetch_path_normalization.js",
        f"--pre-js {WEB_DIR}/online_client.js",
    ]
    n.build(
        outputs=output,
        implicit_outputs=[javascript_output, wasm_output],
        rule="web_link",
        inputs=objects,
        implicit=[
            WEB_DIR / "shell.html",
            WEB_SDL_PORT,
            WEB_DIR / "xiso.js",
            WEB_DIR / "fetch_path_normalization.js",
            WEB_DIR / "online_client.js",
            WEB_DIR / "library_web_transport.js",
            WEB_DIR / "library_web_fetchfs.js",
        ],
        variables={
            "link_rsp": " ".join(
                '"' + str(obj).replace(os.sep, "/") + '"' for obj in objects
            ),
            "ldflags": " ".join(link_flags),
            "libs": (
                f"-lfetchfs.js -lopfs.js "
                f"--js-library {WEB_DIR}/library_web_transport.js "
                # (WasmFS's fetch backend with a size that costs no download)
                f"--js-library {WEB_DIR}/library_web_fetchfs.js"
            ),
        },
    )

    n.rule(
        name="web_copy_asset",
        command=(
            "$python -c \"from pathlib import Path; import shutil,sys; "
            "Path(sys.argv[2]).parent.mkdir(parents=True, exist_ok=True); "
            "shutil.copy2(sys.argv[1], sys.argv[2])\" $in $out"
        ),
        description="WEB ASSET $out",
    )
    ui_asset_outputs: List[Path] = []
    for source in sorted(path for path in (WEB_DIR / "assets").rglob("*") if path.is_file()):
        asset_output = build_dir / source.relative_to(WEB_DIR)
        ui_asset_outputs.append(asset_output)
        n.build(outputs=asset_output, rule="web_copy_asset", inputs=source)
    n.build(
        outputs="web",
        rule="phony",
        inputs=[output, javascript_output, wasm_output, *ui_asset_outputs],
    )
    n.newline()
