"""Ninja rules for the native 32-bit ARM Android build (``ninja android32``).

For a device whose Android is 32-bit only (``ro.product.cpu.abilist`` is
``armeabi-v7a``), which runs neither of the other Android builds: the guest
build's AArch64 code needs a 64-bit process, and the native 64-bit build is
an arm64-v8a library. Samsung's Galaxy Watch 8 Classic is one: a 64-bit
processor and kernel with a 32-bit Android (port/android/README.md, "Wear
OS").

The game is 32-bit code, as on the Xbox, so this build compiles it as the
32-bit Linux build does (tools/linux_build.py, Linux32Units: the Xbox
addresses are host addresses, no `long` rewrite), for armeabi-v7a with the
NDK's clang and bionic, and leaves out what only x86 has: the struct layout
of -malign-double is ARM's own, small structures come back in r0 as
-freg-struct-return has them on x86, and the library is position
independent. armeabi-v7a's softfp calling convention passes floating point
arguments in core registers, variadic or not, so the game's calls to
variadic functions without a prototype (tools/android_build.py,
VARIADIC_PROTOTYPE_FILES) work as they do on x86.

What the app needs of the library is the native 64-bit build's
(tools/android64_build.py): its entry point and services
(port/android/native64), the OpenGL ES helpers, the touch controls' state and
the master servers' requests of the guest build's host library.

The Xbox's contiguous memory window cannot be at its linked address,
0x80000000, in every process: a Galaxy Watch 8 Classic's Java runtime has
its JIT code cache there. The build moves it (HALO_WINDOW_REBASE, as the
Switch's does: source/cseries/xbox_address.h) to the first of a few
addresses that is free (port/linux/src/xbox_memory.c), 128 MB of it, and
HALO_ANDROID without HALO_64BIT gives it the guest build's memory sizes
(halo_port_capacity.h), which suit a watch.

The result, build/android32/jniLibs/armeabi-v7a (libmain.so and SDL3's), is
packaged by the app's Gradle module with -Pnative32 (``ninja android32_apk``;
port/android/app/build.gradle): the same app, ID and key.
"""

from pathlib import Path
from typing import Any, List

from .android_build import (
    ANDROID_API,
    SDL_DIR,
    _find_ndk,
    fetch_third_party,
    ndk_toolchain,
)
from .embed_assets import hud_assets_build, hud_configure_inputs, ui_fonts_build
from .linux_build import (
    PORT_CONFIG as LINUX_PORT_CONFIG,
    Linux32Units,
    _load_port_config,
    _quote,
    compile_launcher,
    linux32_objects,
    lto_flags,
)
from .ninja_syntax import Writer

PORT_DIR = Path("port/android")
NATIVE_DIR = PORT_DIR / "native64"
ANDROID32_TARGET = f"--target=armv7a-linux-androideabi{ANDROID_API}"
# ARMv7 with NEON and VFPv4, which every armeabi-v7a device of Android 9
# (API 28) and later has, scheduled for the Cortex-A55 (the watch's small
# cores); Thumb-2, the NDK's default
ANDROID32_MARCH = "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=softfp -mthumb -mtune=cortex-a55"
SDL_BUILD = Path("build/android32/sdl3-build")
ANDROID_SDL = SDL_BUILD / "libSDL3.so"

# The game's data is full of packed structures (#pragma pack(1)), whose
# members and whose globals can be at any address: main.c's
# _screenshot_and_framerate_globals is at an odd one, and its render windows
# are reached through pointers. x86 and AArch64 load and store at any
# address; 32-bit ARM's VLDR, VSTR, LDRD, STRD, LDM and STM fault on an
# address that is not a multiple of four (SIGBUS, BUS_ADRALN: the kernel of a
# 64-bit watch does not fix them up for 32-bit processes). So nothing reached
# through a pointer is assumed aligned, and clang loads it with LDR and STR,
# which ARMv7 does at any address.
ALIGNMENT_FLAGS = ["-fmax-type-align=1"]

# what only x86 has (tools/linux_build.py, LINUX_ABI_FLAGS and POSIX_FLAGS)
X86_ONLY_FLAGS = {"-malign-double", "-freg-struct-return", "-fno-pic"}

LIBRARIES = ["SDL3", "GLESv3", "EGL", "log", "android", "m", "dl"]


def android32_configure_inputs() -> List[Path]:
    """Files whose change must re-run configure.py."""
    if not LINUX_PORT_CONFIG.is_file():
        return [Path(__file__)]
    return [Path(__file__), NATIVE_DIR, LINUX_PORT_CONFIG, *hud_configure_inputs()]


def generate_android32_build(n: Writer, sln: Any) -> None:
    if not LINUX_PORT_CONFIG.is_file() or not NATIVE_DIR.is_dir():
        return
    ndk = Path(sln.android_ndk) if getattr(sln, "android_ndk", None) else _find_ndk()
    if not ndk or not ndk.is_dir():
        n.comment("Native 32-bit ARM Android build: no NDK found (set ANDROID_NDK_HOME or pass --android-ndk)")
        return
    toolchain = ndk_toolchain(ndk)
    if not toolchain:
        n.comment("Native 32-bit ARM Android build: the NDK has no LLVM toolchain")
        return
    try:
        # (SDL3's sources, which the Android build fetches)
        fetch_third_party()
    except Exception as error:  # noqa: BLE001 - reported, the build left out
        n.comment(f"Native 32-bit ARM Android build: cannot fetch SDL3 ({error})")
        return
    config = _load_port_config()
    build_dir: Path = sln.build_dir / "android32"
    jni_dir = build_dir / "jniLibs" / "armeabi-v7a"
    libmain = jni_dir / "libmain.so"
    staged_sdl = jni_dir / "libSDL3.so"
    cc = toolchain / "bin" / "clang"
    # (the 32-bit game's MSVC semantics headers, ``ninja linux``'s, as the
    # 32-bit dedicated server takes them: tools/server_build.py)
    linux_dir = sln.build_dir / "linux"

    n.comment("Native 32-bit ARM Android build (ninja android32); see port/android/README.md")
    n.variable("android32_cc", _quote(cc))
    n.rule(
        name="android32_cc",
        command=f"{compile_launcher(sln)}$android32_cc -MMD -MF $out.d $cflags -c $in -o $out",
        description="ANDROID32 CC $out",
        depfile="$out.d",
        deps="gcc",
    )

    # SDL3 for armeabi-v7a, with the NDK's CMake toolchain file, as the
    # Android build builds it for arm64-v8a (tools/android_build.py)
    toolchain_file = ndk / "build" / "cmake" / "android.toolchain.cmake"
    n.rule(
        name="android32_sdl",
        command=(f"cmake -S {SDL_DIR} -B {SDL_BUILD} -G Ninja -DCMAKE_TOOLCHAIN_FILE={_quote(toolchain_file)} "
                 f"-DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-{ANDROID_API} -DCMAKE_BUILD_TYPE=Release "
                 "-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF "
                 f"&& cmake --build {SDL_BUILD} --target SDL3-shared"),
        description="ANDROID32 SDL3 (armeabi-v7a)",
        pool="console",
    )
    n.build(outputs=ANDROID_SDL, rule="android32_sdl", implicit=[Path("build/android/third_party/SDL3/CMakeLists.txt")])

    target_flags = [
        ANDROID32_TARGET,
        # a shared library (SDLActivity loads it)
        "-fPIC",
        "-DHALO_ANDROID=1",
        "-DHALO_GLES=1",
        # (char is unsigned on ARM; the game, as MSVC, has it signed)
        "-fsigned-char",
    ]
    generated_sources = (hud_assets_build(n, "android32", build_dir / "generated" / "hud_hires_assets.c")
                         + ui_fonts_build(n, "android32", build_dir / "generated" / "ui_fonts.c", sln))
    units = Linux32Units(
        sln=sln, config=config, game_browser=getattr(sln, "game_browser", False),
        semantics_header=linux_dir / "halo_msvc_semantics.h",
        platform_semantics_header=linux_dir / "platform_msvc_semantics.h",
        embedded_assets=generated_sources, rule="android32_cc", target_flags=target_flags,
        march=ANDROID32_MARCH, dropped_flags=set(X86_ONLY_FLAGS),
        # (the window wherever the process has room: above)
        extra_flags=["-DHALO_WINDOW_REBASE", *ALIGNMENT_FLAGS],
        # (SDL3's headers, and the app's own: port/android/include, host)
        include_flags=[f"-I{SDL_DIR / 'include'}", f"-I{PORT_DIR / 'include'}", f"-I{PORT_DIR / 'host'}"],
        # the app's entry point and services, the OpenGL ES helpers, the
        # touch controls and the master servers' requests, as the native
        # 64-bit build has them (tools/android64_build.py)
        native_sources=[*sorted(NATIVE_DIR.glob("*.c")), PORT_DIR / "host" / "host_gl.c",
                        PORT_DIR / "host" / "host_touch.c", PORT_DIR / "host" / "host_https.c"],
        # (no movies: the stubs, and the game skips them)
    )
    lto_cflags, lto_ldflags = lto_flags(sln, build_dir / "thinlto-cache")
    objects = linux32_objects(n, units, build_dir / "obj", lto_cflags, [ANDROID_SDL])

    n.rule(
        name="android32_link",
        command="$android32_cc $ldflags -o $out @$out.rsp $libs",
        description="ANDROID32 LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(
        outputs=libmain,
        rule="android32_link",
        inputs=objects,
        implicit=[ANDROID_SDL],
        variables={
            "ldflags": " ".join([
                ANDROID32_TARGET, "-shared", "-g", "-fuse-ld=lld", *lto_ldflags,
                # the library's own definitions first (tools/android64_build.py)
                "-Wl,-Bsymbolic",
                "-Wl,-z,max-page-size=16384",
                "-Wl,--no-undefined",
            ]),
            "libs": " ".join([f"-L{ANDROID_SDL.parent}", *(f"-l{lib}" for lib in LIBRARIES)]),
        },
    )
    n.rule(name="android32_copy", command="cp $in $out", description="ANDROID32 STAGE $out")
    n.build(outputs=staged_sdl, rule="android32_copy", inputs=ANDROID_SDL)
    # internet play's MQTT brokers, in the APK (android64_main.c)
    staged_brokers = build_dir / "assets" / "brokers.txt"
    n.build(outputs=staged_brokers, rule="android32_copy", inputs=Path("port/assets/network/brokers.txt"))
    n.build(outputs="android32", rule="phony", inputs=[libmain, staged_sdl, staged_brokers])

    # the app (port/android/app with -Pnative32, into its build-native32)
    apk = PORT_DIR / "app" / "build-native32" / "outputs" / "apk" / "debug" / "app-debug.apk"
    n.rule(
        name="android32_gradle",
        command=(f"cd {PORT_DIR} && ./gradlew --console=plain -q -Pnative32 assembleDebug && "
                 "touch app/build-native32/outputs/apk/debug/app-debug.apk"),
        description="ANDROID32 GRADLE $out",
        pool="console",
    )
    n.build(outputs=apk, rule="android32_gradle", inputs=[libmain, staged_sdl, staged_brokers])
    n.build(outputs="android32_apk", rule="phony", inputs=apk)
    n.newline()
