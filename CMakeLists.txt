# ============================================================================
# GoldenEye 007 — PC port build
#
# This is a PARALLEL build system to the N64 `Makefile`. It compiles the same
# game C sources (src/game, src) for a modern host (Windows/Linux/macOS)
# against the PC shims in `port/`. The N64 build is untouched.
#
# See docs/internals.md for the architecture and plan.
# Modelled on the Perfect Dark PC port (perfect-dark-pc-port/perfect_dark).
#
# Usage:
#   cmake -S . -B build-pc -DROMID=ntsc-final
#   cmake --build build-pc -j
# ============================================================================

cmake_minimum_required(VERSION 3.16)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# The decomp uses the `#define inherits struct` pattern (47x in
# src/bondtypes.h): a nested `struct Base;` inside a derived struct. GCC
# treats this as embedding the full base struct (correct layout, matches the
# N64/IDO behaviour the decomp relies on); Clang treats it as a bare forward
# declaration (no members) and rejects base-member access. So prefer GCC.
# (GCC does hard-error on the one struct that declares a member with the same
# name as an inherited one — CCTVRecord's `pad` — see docs/internals.md.)
# An explicit -DCMAKE_C_COMPILER=... always wins.
if(NOT DEFINED CMAKE_C_COMPILER)
  find_program(_GE007_GCC NAMES gcc)
  if(_GE007_GCC)
    set(CMAKE_C_COMPILER "${_GE007_GCC}" CACHE FILEPATH "C compiler" FORCE)
    get_filename_component(_GE007_GCC_DIR "${_GE007_GCC}" DIRECTORY)
    find_program(_GE007_GXX NAMES g++ PATHS "${_GE007_GCC_DIR}" NO_DEFAULT_PATH)
    if(_GE007_GXX)
      set(CMAKE_CXX_COMPILER "${_GE007_GXX}" CACHE FILEPATH "CXX compiler" FORCE)
    endif()
  endif()
endif()

if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
  else()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
  endif()
endif()

# ASM is needed for the generated absolute ROM asset symbols
# (port/src/romassets_<region>.s, see scripts/gen_romassets.py).
project(ge007 C CXX ASM)

include(CTest)

include(TargetArch)
include(FindPkgConfig)

set(CMAKE_C_STANDARD 11)
# GNU extensions ON: strict -std=c11/-std=c++20 define __STRICT_ANSI__, which
# disables glibc's _DEFAULT_SOURCE and hides POSIX symbols the port needs
# (clock_gettime/CLOCK_REALTIME in port/src/libultra.c, mmap flags, etc.).
# -std=gnu11/gnu++20 is GCC's own default and what MinGW was already using.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Opt-in ccache launcher. The game sources are frozen (1:1 with the decomp),
# so ccache makes branch-switch / worktree rebuilds nearly free. No-op when
# ccache is not installed.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
endif()

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# ----------------------------------------------------------------------------
# Options
# ----------------------------------------------------------------------------
# Which ROM the executable expects in the data/ dir.
set(ROMID "ntsc-final" CACHE STRING "ROM id: ntsc-final (US), pal-final (EU), jpn-final (JP)")

# Coverage is opt-in because instrumentation makes the full executable larger
# and slower. The unit target is deterministic; the full game target is also
# instrumented when explicitly requested, but its counters are only useful
# after a runtime or mocked game-loop harness drives them.
option(GE007_ENABLE_COVERAGE
  "Instrument unit tests and the full game target (GCC only)"
  OFF)
set(GE007_COVERAGE_MIN_LINES "100" CACHE STRING
  "Minimum line coverage for required policy files")
set(GE007_COVERAGE_MIN_BRANCHES "100" CACHE STRING
  "Minimum branch coverage for required policy files")

# ----------------------------------------------------------------------------
# Target arch / platform
# ----------------------------------------------------------------------------
target_architecture(TARGET_ARCH)
if(TARGET_ARCH MATCHES "64")
  set(TARGET_IS_64BIT TRUE)
else()
  set(TARGET_IS_64BIT FALSE)
endif()
message(STATUS "Target arch: ${TARGET_ARCH} (64bit=${TARGET_IS_64BIT})")

if(WIN32)
  set(TARGET_OS "windows")
elseif(APPLE)
  set(TARGET_OS "osx")
elseif(UNIX)
  set(TARGET_OS "linux")
else()
  message(FATAL_ERROR "Unknown target OS")
endif()
set(TARGET_PLATFORM "${TARGET_ARCH}-${TARGET_OS}")
message(STATUS "Target platform: ${TARGET_PLATFORM}")

# Region -> compile definitions.
#
# The game code #ifdefs a full per-region macro set (see the N64 Makefile,
# the VERSION blocks). e.g. VERSION_EU is used in 60+ files, LEFTOVERDEBUG in
# 26, BUGFIX_R1 in 21. We MUST emit the same set the N64 build uses for the
# matching region, or the PC build takes different conditional branches and
# silently diverges (and fails to link). These mirror Makefile LCDEFS exactly.
if(ROMID STREQUAL "ntsc-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION "")
  set(ASSET_REGION "u")
  set(REGION_DEFS
    -DVERSION_US -DLANG_US -DREFRESH_NTSC
    -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0 -DBYTEMATCH)
elseif(ROMID STREQUAL "pal-final")
  set(VERSION_IS_PAL 1)
  set(BIN_REGION ".pal")
  set(ASSET_REGION "e")
  set(REGION_DEFS
    -DVERSION_EU -DLANG_EU -DREFRESH_PAL
    -DBUGFIX_R1 -DBUGFIX_R2 -DBYTEMATCH)
elseif(ROMID STREQUAL "jpn-final")
  set(VERSION_IS_PAL 0)
  set(BIN_REGION ".jpn")
  set(ASSET_REGION "j")
  set(REGION_DEFS
    -DVERSION_JP -DLANG_JP -DREFRESH_NTSC
    -DBUGFIX_R1 -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBYTEMATCH)
else()
  message(FATAL_ERROR "Unknown ROMID: ${ROMID}")
endif()

# git hash for version info
execute_process(COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE VERSION_HASH OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT VERSION_HASH)
  set(VERSION_HASH "unknown")
endif()

# Provenance, printed by --version and at startup (port/src/main.c): a build
# keeps pointing at this repository no matter who compiles or renames it.
set(VERSION_CODENAME "larkspur")
set(ORIGIN_URL "https://github.com/jkdansereau/goldeneye-pc-port")

configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/versioninfo.h.in"
  "${CMAKE_BINARY_DIR}/port/include/versioninfo.h" @ONLY)

# Host <string.h> path for port/shim/string.h. The N64 include/string.h
# declares strlen/strchr with `const unsigned char *`, which conflicts with
# the host declarations that SDL (and other system headers) pull in, and it
# sits earlier on the include path than the toolchain's own string.h — so the
# shim cannot use #include_next (it would find the N64 header first). Derive
# the toolchain include dir from the compiler location (MinGW/MSYS2 layout:
# bin/../include) and generate an absolute-path include.
get_filename_component(_PORT_GCC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(PORT_HOST_STRING_H "${_PORT_GCC_DIR}/../include/string.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststring.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststring.h" @ONLY)

# Host <sched.h> path for port/shim/sched.h. The decomp's src/sched.h is on
# the include path and shadows the system header; libstdc++'s <thread>/gthr
# headers in the fast3d C++ TUs need the real <sched.h> (cpu_set_t,
# sched_yield). Both MinGW and Linux keep <sched.h> at bin/../include.
get_filename_component(PORT_HOST_SCHED_H "${_PORT_GCC_DIR}/../include/sched.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hostsched.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hostsched.h" @ONLY)

# Host <stdlib.h> path for port/shim/stdlib.h. The N64 include/stdlib.h
# typedefs lldiv_t/ldiv_t, which conflict with the host's when both land in
# one C++ TU (fast3d): MinGW's own sec_api/stdlib_s.h does `#include
# <stdlib.h>`, which resolves to the N64 header via -I. The shim routes C++
# TUs to the host header by absolute path (same trick as hoststring.h).
get_filename_component(PORT_HOST_STDLIB_H "${_PORT_GCC_DIR}/../include/stdlib.h" ABSOLUTE)
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststdlib.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststdlib.h" @ONLY)

# Host <limits.h> path for port/shim/limits.h. The N64 include/limits.h only
# defines LONG_MAX/ULONG_MAX for MIPS-specific macros, which is correct for
# game code but insufficient for host libraries such as FreeType.
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include/limits.h
  OUTPUT_VARIABLE _PORT_GCC_LIMITS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_PORT_GCC_LIMITS AND EXISTS "${_PORT_GCC_LIMITS}")
  get_filename_component(PORT_HOST_LIMITS_H "${_PORT_GCC_LIMITS}" ABSOLUTE)
else()
  get_filename_component(PORT_HOST_LIMITS_H "${_PORT_GCC_DIR}/../include/limits.h" ABSOLUTE)
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hostlimits.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hostlimits.h" @ONLY)

# Host <stddef.h> path for port/shim/stddef.h (same reason as stdlib: the N64
# stub shadows the host header in C++ TUs, hiding max_align_t from cstddef).
# stddef.h is compiler-internal, not in bin/../include: on a native Linux GCC
# it lives under lib/gcc/<triple>/<ver>/include. Ask the compiler where it is;
# fall back to the MinGW bin/../include layout.
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include/stddef.h
  OUTPUT_VARIABLE _PORT_GCC_STDDEF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_PORT_GCC_STDDEF AND EXISTS "${_PORT_GCC_STDDEF}")
  get_filename_component(PORT_HOST_STDDDEF_H "${_PORT_GCC_STDDEF}" ABSOLUTE)
else()
  get_filename_component(PORT_HOST_STDDDEF_H "${_PORT_GCC_DIR}/../include/stddef.h" ABSOLUTE)
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/hoststddef.h.in"
  "${CMAKE_BINARY_DIR}/port/include/hoststddef.h" @ONLY)

# ----------------------------------------------------------------------------
# Compiler definitions & flags
# ----------------------------------------------------------------------------
add_definitions(
  -DPORT=1
  -D_LANGUAGE_C=1
  -DAVOID_UB=1
  -DPAL=${VERSION_IS_PAL}
  ${REGION_DEFS}
)
if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT=1)
endif()

# The decomp is big-endian N64 code with unaligned accesses; these flags are
# required to compile it cleanly on a little-endian host (same as the PD port).
add_compile_options(
  -fno-strict-aliasing
  -funsigned-char
  -fwrapv
  -include "versioninfo.h"
)
# The decompiled game code has a class of latent fixed-size stack-buffer
# over/under-runs (e.g. stan.c sub_GAME_7F0B1DDC's tileStack, bondview2.c
# bondviewCalcIntroSwirlCamera's pointbuf[10] indexed [-3..11]) that write a
# few words into adjacent stack scratch. On the N64, and on the MinGW/Windows
# build (no default stack protector), this is harmless in practice — verified
# stable over thousands of frames. Only Ubuntu's gcc default
# (-fstack-protector-strong) turns these into a fatal __stack_chk_fail at
# stage load / first frames. Match the N64 and MinGW builds so the same code
# runs the same way; the genuinely understood cases are still fixed properly
# under #ifdef PORT (D189). A hardened Linux build would need the whole class
# swept first — tracked in docs/dev/findings.md.
add_compile_options(-fno-stack-protector)
# Keep frame pointers: dbghelp's StackWalk64 (crash handler + thread dumps)
# cannot read DWARF unwind info on MinGW, so the EBP chain is the only way to
# get a real backtrace of a hung/crashed game thread.
add_compile_options(-fno-omit-frame-pointer)

# Optimisation: the software RSP + software rasteriser in port/fast3d are the
# hot path and are very sensitive to -O level (the difference between ~1 fps
# and playable). Non-Debug builds keep the CMAKE_BUILD_TYPE optimisation
# (RelWithDebInfo => -O2); only an explicit Debug build drops to -Og.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Og)
endif()

add_compile_options(
  -Wall
  -Wno-address
  -Wno-misleading-indentation
  -Wno-missing-braces
  -Wno-multichar
  -Wno-tautological-compare
  -Wno-unused-but-set-variable
  -Wno-unused-value
  -Wno-unused-variable
)
# The decomp was written for IDO's leniency. These downgrade hard errors to
# warnings so the unmodified game code compiles; each class is a tracked
# decomp finding (see docs/internals.md):
#   int-conversion: raw 32-bit ROM address constants assigned to pointers
#     (WIP decomp; genuine 64-bit hazards to fix upstream as phases progress)
#   incompatible-pointer-types: Mtxf union vs float[4][4], Gfx array idioms
#   implicit-function-declaration: missing header declarations (e.g.
#     get_ptr_long_watch_text_for_item, defined in gunfire.c)
add_compile_options(
  -Wno-error=int-conversion
  -Wno-error=incompatible-pointer-types
  -Wno-error=implicit-function-declaration
)
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wno-pointer-sign>)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>)

# The decomp's `#define inherits struct` idiom (src/bondtypes.h) makes the base
# struct an unnamed tagged member and accesses its fields directly
# (obj->type where type lives in the embedded PropRecord). GCC targeting
# *-w64-mingw32 enables this via -fms-extensions implicitly; a native Linux/
# macOS GCC does not, so `'ObjectRecord' has no member named 'type'`. Enable
# the Plan 9 extensions explicitly there (superset of -fms-extensions: also the
# automatic base-pointer conversion the idiom relies on). C only — the decomp
# is C; fast3d C++ does not use `inherits`.
if(NOT MINGW)
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-fplan9-extensions>)
  # GCC 14 promoted several long-standing C leniencies to hard errors
  # (int<->pointer for a known prototype, incompatible-pointer arg types,
  # implicit int/function). The WIP decomp still trips these (e.g.
  # append_text_picked_up's vestigial u8* params in propobj.c). -fpermissive
  # downgrades them to warnings — the C counterpart of the -Wno-error lines
  # above, and the documented path for building older C against GCC 14+.
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-fpermissive>)
endif()

# ASLR must be OFF: dram_syms.s defines absolute symbols (cfb_16 @ 0x70000000,
# _bssSegmentEnd @ 0x70050000) that the game references through pointer-typed
# externs (src/bondgame.h: `extern u32 *_bssSegmentEnd`). On x86-64/PE, taking
# their address emits a .refptr slot holding the absolute value PLUS a base
# relocation; with ASLR the loader rebases it to runtime_base+0x70050000 =
# garbage. With DYNAMICBASE disabled the image loads at its preferred base
# (0x140000000) and the relocations are no-ops. main.c sanity-checks the load
# base at startup.
if(MINGW)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-dynamicbase")
elseif(UNIX AND NOT APPLE)
  # Linux equivalent of the ASLR-off requirement above: dram_syms.s defines
  # absolute symbols at 0x70000000 referenced through pointer-typed externs. A
  # PIE build (GCC's default on modern distros) emits GOT/relative relocations
  # for them — R_X86_64_32S link errors, or a runtime rebase to garbage. Build
  # and link no-PIE so the image keeps its preferred load address.
  add_compile_options(-fno-pie)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
  # Move the image base to 0x20000000. The default no-PIE base 0x400000 puts
  # .data/.bss symbols around 0x00500000-0x00600000, which COLLIDES with the
  # N64 address ranges fast3d's seg_addr() decodes: a game-built display list
  # that embeds `&symbol` (via the osVirtualToPhysical identity shim) hands
  # seg_addr a value < 0x800000, which it treats as a K0-physical DRAM offset
  # and remaps to 0x80xxxxxx (zeroed DRAM mirror) -> "unknown GBI opcode 0x00"
  # -> black screen. 0x20000000 sits in the gap between the segmented-address
  # space (< 0x10000000), the cart map (0x10000000), the D131 module range
  # ([0x40000000,0x70000000)) and DRAM (>= 0x70000000); seg_addr passes such
  # addresses through untouched. Still < 2GiB so -mcmodel=small / R_X86_64_32
  # relocations are fine (unlike a 0x140000000 base, which needs mcmodel=large).
  # This is the ELF analogue of the Windows build's fixed 0x140000000 base.
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-Ttext-segment=0x20000000")
endif()

# port/shim first: PC-only shadow headers (bondtypes.h, bondconstants.h,
# PR/gbi.h) that fix N64-compiler assumptions without touching src/ or
# include/. Inert in the N64 build (its Makefile never sees port/shim).
include_directories(
  "${CMAKE_SOURCE_DIR}/port/shim"
  "${CMAKE_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/include/PR"
  "${CMAKE_SOURCE_DIR}/src"
  "${CMAKE_SOURCE_DIR}/src/game"
  "${CMAKE_SOURCE_DIR}/src/libultra"
  "${CMAKE_SOURCE_DIR}/src/libultra/audio"
  "${CMAKE_SOURCE_DIR}/port/include"
  "${CMAKE_BINARY_DIR}/port/include"
)

# ----------------------------------------------------------------------------
# Dependencies: SDL2, zlib, OpenGL, optional FreeType vector UI
# ----------------------------------------------------------------------------
set(LIBS "")

find_package(SDL2 REQUIRED)
include_directories(SYSTEM "${SDL2_INCLUDE_DIR}")
list(APPEND LIBS "${SDL2_LIBRARY}")

# Science Gothic is packaged under data/fonts for the vector UI path. FreeType
# is optional so the port still builds on platforms that only ship the
# original N64 bitmap UI; those builds use that fallback.
find_package(Freetype QUIET)
if(FREETYPE_FOUND)
  include_directories(SYSTEM "${FREETYPE_INCLUDE_DIRS}")
  list(APPEND LIBS "${FREETYPE_LIBRARIES}")
  add_definitions(-DGE007_HAVE_FREETYPE=1)
else()
  message(STATUS "FreeType not found: vector UI will use the original bitmap font")
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_INCLUDE_DIR)
  include_directories(SYSTEM "${ZLIB_INCLUDE_DIR}")
endif()
list(APPEND LIBS "${ZLIB_LIBRARY}")

find_package(PNG REQUIRED)
list(APPEND LIBS PNG::PNG)

# Real <zlib.h> for port/src/rzdecomp.c (which backs decompressdata() with real
# zlib). The game's own src/game/zlib.h sits on the -I path and shadows a plain
# `#include <zlib.h>` in a port TU, so generate an absolute-path include (same
# trick as hoststring.h). Prefer find_package(ZLIB)'s dir; fall back to the
# toolchain include dir (MinGW layout: bin/../include), where
# mingw-w64-x86_64-zlib installs zlib.h.
set(PORT_REAL_ZLIB_H "${ZLIB_INCLUDE_DIR}/zlib.h")
if(NOT EXISTS "${PORT_REAL_ZLIB_H}")
  set(PORT_REAL_ZLIB_H "${_PORT_GCC_DIR}/../include/zlib.h")
endif()
if(NOT EXISTS "${PORT_REAL_ZLIB_H}")
  message(FATAL_ERROR "Cannot locate the host zlib.h needed by port/src/rzdecomp.c")
endif()
configure_file(
  "${CMAKE_SOURCE_DIR}/port/include/realzlib.h.in"
  "${CMAKE_BINARY_DIR}/port/include/realzlib.h" @ONLY)

if(NOT GL_LIBRARY)
  if(WIN32)
    set(GL_LIBRARY opengl32)
  elseif(APPLE)
    set(GL_LIBRARY "-framework OpenGL")
  else()
    set(GL_LIBRARY GL)
  endif()
endif()
list(APPEND LIBS "${GL_LIBRARY}")

if(NOT EXTRA_LIBRARIES)
  if(MINGW)
    # MinGW: pthreads for the real-OS-thread kernel (port/src/libultra.c).
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++ pthread)
  elseif(WIN32)
    set(EXTRA_LIBRARIES dbghelp -static-libstdc++)
  else()
    set(EXTRA_LIBRARIES stdc++ m dl pthread)
  endif()
endif()
list(APPEND LIBS "${EXTRA_LIBRARIES}")

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------
# Port layer (shims + software RSP).
file(GLOB_RECURSE SRC_PORT CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/port/src/*.c")
file(GLOB_RECURSE SRC_PORT_CXX CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/port/src/*.cpp")
file(GLOB_RECURSE SRC_FAST3D CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/port/fast3d/*.cpp"
  "${CMAKE_SOURCE_DIR}/port/fast3d/glad/*.c")

# Generated absolute ROM asset symbols for this region (see
# scripts/gen_romassets.py). Defines every obseg/ramrom/music label and the
# ge007.ld segment markers as 0x10000000 + rom_offset, so `&symbol` / romCopy()
# work once romdata.c maps the .z64 at 0x10000000.
set(SRC_ROMASSETS "${CMAKE_SOURCE_DIR}/port/src/romassets_${ASSET_REGION}.s")
if(NOT EXISTS ${SRC_ROMASSETS})
  message(FATAL_ERROR "Missing ${SRC_ROMASSETS} — run: python3 scripts/gen_romassets.py ${ASSET_REGION}")
endif()

# Absolute symbols inside the reserved N64-DRAM region (port/src/dram.c):
# cfb_16 (replaces src/cfb.c) and _bssSegmentEnd (mempool start marker).
set(SRC_DRAM_SYMS "${CMAKE_SOURCE_DIR}/port/src/dram_syms.s")

# Game code — compiled unmodified.
file(GLOB_RECURSE SRC_GAME "${CMAKE_SOURCE_DIR}/src/game/*.c")

# EXCLUDED from src/game (replaced by port/src/rzdecomp.c, which backs
# decompressdata()/rzipGetSomething() with real zlib):
#   decompress.c + zlib.c — GE's custom gzip-1.2.4 inflate builds its Huffman
#   tables contiguously into a caller-supplied fixed buffer of `struct huft`.
#   That struct is 8 bytes on MIPS (union holds a 32-bit pointer) but 16 bytes
#   on x86-64 (union holds a 64-bit pointer), so on a 64-bit host the tables
#   overflow load_resource's `u8 buffer[0x2100]` and clobber the caller's return
#   address -> SIGSEGV in langInit()'s first file load (finding D32). Real zlib
#   manages its own table memory, removing the fixed-buffer assumption. Mirrors
#   the PD port, which swaps its assembly rzip for a real-zlib-backed rzip_c.c.
#   The only externally-referenced symbols from these two files are
#   decompressdata() and rzipGetSomething(); both are provided by
#   port/src/rzdecomp.c (see the symbol sweep in docs/internals.md §F).
list(REMOVE_ITEM SRC_GAME
  "${CMAKE_SOURCE_DIR}/src/game/decompress.c"
  "${CMAKE_SOURCE_DIR}/src/game/zlib.c")

# libaudio (AL) — runs on the CPU, platform independent.
file(GLOB SRC_LIBAUDIO "${CMAKE_SOURCE_DIR}/src/libultra/audio/*.c")
# Rare's audio "New" driver (drvrNew/env/reverb) — synthesizer.c calls
# alSaveNew/alAuxBusNew/alMainBusNew/alLoadNew/alResampleNew/alEnvmixerNew/
# alEnvmixerParam/alFxParam, all defined here. Runs on the CPU; must be linked.
file(GLOB SRC_LIBULTRARE_AUDIO "${CMAKE_SOURCE_DIR}/src/libultrare/audio/*.c")
# gu matrix helpers.
file(GLOB SRC_GU "${CMAKE_SOURCE_DIR}/src/libultra/gu/*.c")

# VI mode table (DATA, not a driver): fr.c references osViModeTable ~15x.
# This is the only libultra/io file we compile — the rest are hardware
# drivers that get shimmed in port/src/libultra.c.
set(SRC_LIBULTRARE_DATA
  src/libultrare/io/vitbl.c
)
list(PREPEND SRC_LIBULTRARE_DATA "${CMAKE_SOURCE_DIR}")

# IDO printf engine: src/sprintf.c's sprintf() calls _Printf(prout, dst, fmt,
# va_list), the internal formatter from libultrare/libc (pure C, no hardware).
# xlitob/xldtob are its integer-conversion helpers (_Litob/_Ldtob). The
# _Printf prototype clash between xstdio.h and xprintf.c is resolved by the
# port/shim/libc/xstdio.h shadow header.
set(SRC_LIBULTRARE_LIBC
  src/libultrare/libc/xprintf.c
  src/libultra/libc/xlitob.c
  src/libultra/libc/xldtob.c
)
list(PREPEND SRC_LIBULTRARE_LIBC "${CMAKE_SOURCE_DIR}")

# ----------------------------------------------------------------------------
# Asset data — decompiled ROM arrays (pure data, no logic).
#
# Single-file assets compile unmodified. The per-model / per-level .c files
# under assets/obseg/{bg,brief,setup,stan} are NOT compiled: each one defines
# file-local globals with generic names (`header`, `room_data_table`,
# `intro`, ...) that the N64 build keeps apart by linking every file into its
# own ELF (see assets/obseg/Makefile.*). In a single host link they collide.
# The game loads those blobs from ROM at runtime anyway; their top-level
# symbols (C*Z, G*Z, P*Z, Tbg_*Z, U*Z, L*E/L*J, bg_*_all_p_seg, ...) are now
# absolute cart addresses in the generated romassets_<region>.s (see
# scripts/gen_romassets.py), backed by the ROM image that romdata.c maps at
# 0x10000000. The per-model data itself (chr/*/Model.c etc.) is gitignored
# ROM-derived and absent from this checkout. See docs/internals.md §11.
# D34 (docs/internals.md): animationtable_{data,entries}.c are NOT
# compiled on PC. Their arrays exist only so that &ANIM_DATA_x evaluates to
# the record's offset within the animation_data segment (N64 links it at VMA
# 0); on x86-64 assets/animationtable_data.h redefines them as lvalues at
# g_pc_animdata_base + offset instead. The data itself comes from ROM via
# romCopy, exactly like every other asset.
set(SRC_ASSETS
  assets/font_chardatae.c
  assets/font_chardataj.c
  assets/font_dl.c
  assets/oddtextures.c
  assets/rarewarelogo.c
)
list(PREPEND SRC_ASSETS "${CMAKE_SOURCE_DIR}")

# Engine files in src/ that are platform independent.
# NOTE: this list is a starting point and must be curated during Phase 1.
#
# INCLUDED here (platform-independent or CPU-side):
#   init.c    — provides mainproc() (the game's real main entry) + the thread
#               setup helpers. Its N64-only init() is compiled but NOT called;
#               the N64 symbols init() references are stubbed in
#               port/src/n64stubs.c (see A3 in docs/internals.md).
#   cfb.c     — defines cfb_16 (the framebuffer), referenced by fr.c and game
#               code (bondview2.c, front.c). Pure data.
#   stacks.c  — thread-stack arrays (sp_boot/sp_rmon/sp_idle/sp_shed/sp_main/
#               sp_audi + sp_debug under LEFTOVERDEBUG). Pure data; referenced
#               by init.c (thread setup) and audi.c (sp_audi). The SP_*_SZ
#               sizes in bondconstants.h match these exactly, so sizeof() is
#               correct. No duplicate-definition risk (all other refs are
#               extern decls in bondgame.h / audi.c).
#
# EXCLUDED (hardware drivers, replaced by port/ shims):
#   crash.c rmon.c usb.c tlb_*.c pi.c vi.c motor.c
#   (there is no src/spectrum.c in this decomp)
#   All of src/libultra/io/*.c and src/libultrare/io/*.c EXCEPT vitbl.c.
#   motor.c / pfsinit.c / pfsisplug.c are NOT compiled: joy.c calls
#   osMotorInit/Start/Stop and osPfsInit/osPfsIsPlug only for accessory
#   (Rumble Pak / Memory Pak) detection, so they are shimmed as no-ops in
#   port/src/libultra.c (see B2 — saves are EEPROM, not PFS).
#
# sched.c IS compiled (Phase 2): the game's real scheduler runs on the
# cooperative thread kernel; its hardware leaf calls (osSpTaskLoad/StartGo,
# osDpSetNextBuffer, osVi*) are shimmed in port/src/libultra.c, which runs
# the software RSP inline and posts the RSP_DONE/RDP_DONE messages.
set(SRC_ENGINE
  src/init.c
  src/sched.c
  src/audi.c
  src/snd.c
  src/music.c
  src/joy.c
  src/fr.c
  # src/cfb.c — cfb_16 now lives at 0x80000000 in the reserved DRAM region
  # (port/src/dram_syms.s) so OS_K0_TO_PHYSICAL(cfb_16[0]) is a small offset
  # that fast3d can map back; a host-BSS array would truncate to garbage.
  src/stacks.c
  src/boss.c
  src/mema.c
  src/memp.c
  src/deb.c
  src/debugmenu.c
  src/speed_graph.c
  src/token.c
  src/str.c
  src/sprintf.c
  src/ramrom.c
)
list(PREPEND SRC_ENGINE "${CMAKE_SOURCE_DIR}")

set(SRC
  ${SRC_GAME}
  ${SRC_PORT}
  ${SRC_ROMASSETS}
  ${SRC_DRAM_SYMS}
  ${SRC_PORT_CXX}
  ${SRC_FAST3D}
  ${SRC_LIBAUDIO}
  ${SRC_LIBULTRARE_AUDIO}
  ${SRC_LIBULTRARE_DATA}
  ${SRC_LIBULTRARE_LIBC}
  ${SRC_ASSETS}
  ${SRC_GU}
  ${SRC_ENGINE}
)

# ----------------------------------------------------------------------------
# Target
# ----------------------------------------------------------------------------
set(BIN_NAME "ge007${BIN_REGION}.${TARGET_ARCH}")
add_executable(ge007 ${SRC})
target_include_directories(ge007 PRIVATE
  "${CMAKE_SOURCE_DIR}/port/fast3d")
target_link_libraries(ge007 ${LIBS})
set_target_properties(ge007 PROPERTIES OUTPUT_NAME "${BIN_NAME}")
if(WIN32)
  set_target_properties(ge007 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(GE007_ENABLE_COVERAGE)
  if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR
     NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR
      "GE007_ENABLE_COVERAGE currently requires the GNU C/C++ toolchain")
  endif()

  find_program(GE007_GCOV_EXECUTABLE NAMES gcov)
  if(NOT GE007_GCOV_EXECUTABLE)
    message(FATAL_ERROR
      "GE007_ENABLE_COVERAGE requires gcov, but it was not found")
  endif()

  # This is deliberately applied to the complete executable target. In
  # particular, every src/game/*.c translation unit receives gcno counters;
  # the unit gate below remains separate because unit tests cannot exercise
  # the live game loop.
  target_compile_options(ge007 PRIVATE --coverage -O0 -g)
  target_link_options(ge007 PRIVATE --coverage)

  add_custom_target(ge007_game_coverage
    DEPENDS ge007
    COMMENT "Built instrumented ge007; run the runtime harness to collect game coverage")
endif()

if(BUILD_TESTING)
  add_executable(ge007_dynamic_lighting_test
    tests/dynamic_lighting_test.cpp)
  target_compile_features(ge007_dynamic_lighting_test PRIVATE cxx_std_20)
  add_test(NAME ge007_dynamic_lighting
           COMMAND ge007_dynamic_lighting_test)

  add_executable(ge007_advanced_lighting_contract_test
    tests/advanced_lighting_contract_test.cpp)
  target_compile_definitions(ge007_advanced_lighting_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_advanced_lighting_contract_test PRIVATE
    cxx_std_20)
  add_test(NAME ge007_advanced_lighting_contract
           COMMAND ge007_advanced_lighting_contract_test)
  set_tests_properties(ge007_advanced_lighting_contract
    PROPERTIES LABELS "ge007-red")

  add_executable(ge007_advanced_lighting_policy_test
    tests/advanced_lighting_policy_test.cpp)
  target_include_directories(ge007_advanced_lighting_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_advanced_lighting_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_advanced_lighting_policy
           COMMAND ge007_advanced_lighting_policy_test)

  add_executable(ge007_shadow_mapping_policy_test
    tests/shadow_mapping_policy_test.cpp)
  target_compile_features(ge007_shadow_mapping_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_shadow_mapping_policy
           COMMAND ge007_shadow_mapping_policy_test)

  add_executable(ge007_shadow_mapping_contract_test
    tests/shadow_mapping_contract_test.cpp)
  target_compile_features(ge007_shadow_mapping_contract_test PRIVATE cxx_std_20)
  target_compile_definitions(ge007_shadow_mapping_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ge007_shadow_mapping_contract
           COMMAND ge007_shadow_mapping_contract_test)

  add_executable(ge007_explosion_effect_test
    tests/explosion_effect_test.cpp)
  target_compile_features(ge007_explosion_effect_test PRIVATE cxx_std_20)
  add_test(NAME ge007_explosion_effect
           COMMAND ge007_explosion_effect_test)

  add_executable(ge007_vector_text_frame_test
    tests/vector_text_frame_test.cpp)
  target_compile_features(ge007_vector_text_frame_test PRIVATE cxx_std_20)
  add_test(NAME ge007_vector_text_frame
           COMMAND ge007_vector_text_frame_test)

  add_executable(ge007_vector_text_font_assets_test
    tests/vector_text_font_assets_test.cpp)
  target_compile_features(ge007_vector_text_font_assets_test PRIVATE cxx_std_20)
  target_compile_definitions(ge007_vector_text_font_assets_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(FREETYPE_FOUND)
    target_compile_definitions(ge007_vector_text_font_assets_test PRIVATE
      GE007_HAVE_FREETYPE_TEST=1)
    target_include_directories(ge007_vector_text_font_assets_test PRIVATE
      SYSTEM "${FREETYPE_INCLUDE_DIRS}")
    target_link_libraries(ge007_vector_text_font_assets_test PRIVATE
      "${FREETYPE_LIBRARIES}")
  endif()
  add_test(NAME ge007_vector_text_font_assets
           COMMAND ge007_vector_text_font_assets_test)

  add_executable(ge007_vector_text_route_test
    tests/vector_text_route_test.cpp)
  target_compile_features(ge007_vector_text_route_test PRIVATE cxx_std_20)
  add_test(NAME ge007_vector_text_route
           COMMAND ge007_vector_text_route_test)

  add_executable(ge007_bump_mapping_policy_test
    tests/bump_mapping_policy_test.cpp)
  target_compile_features(ge007_bump_mapping_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_bump_mapping_policy
           COMMAND ge007_bump_mapping_policy_test)

  add_executable(ge007_texture_wrap_policy_test
    tests/texture_wrap_policy_test.cpp)
  target_compile_features(ge007_texture_wrap_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_texture_wrap_policy
           COMMAND ge007_texture_wrap_policy_test)

  add_executable(ge007_textured_vertex_stream_policy_test
    tests/textured_vertex_stream_policy_test.cpp)
  target_compile_definitions(ge007_textured_vertex_stream_policy_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_textured_vertex_stream_policy_test PRIVATE
    cxx_std_20)
  add_test(NAME ge007_textured_vertex_stream_policy
           COMMAND ge007_textured_vertex_stream_policy_test)

  add_executable(ge007_fast3d_light_state_test
    tests/fast3d_light_state_test.cpp)
  target_include_directories(ge007_fast3d_light_state_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d"
    "${CMAKE_SOURCE_DIR}/include")
  target_compile_features(ge007_fast3d_light_state_test PRIVATE cxx_std_20)
  add_test(NAME ge007_fast3d_light_state
           COMMAND ge007_fast3d_light_state_test)

  add_executable(ge007_fast3d_normal_policy_test
    tests/fast3d_normal_policy_test.cpp)
  target_include_directories(ge007_fast3d_normal_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_fast3d_normal_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_fast3d_normal_policy
           COMMAND ge007_fast3d_normal_policy_test)

  add_executable(ge007_bumpmap_generator_policy_test
    tests/bumpmap_generator_policy_test.cpp)
  target_include_directories(ge007_bumpmap_generator_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/mktex/src")
  target_compile_features(ge007_bumpmap_generator_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_bumpmap_generator_policy
           COMMAND ge007_bumpmap_generator_policy_test)

  add_executable(ge007_bumpmap_decode_test
    tests/bumpmap_decode_test.cpp
    "${CMAKE_SOURCE_DIR}/tools/mktex/src/bumpmap_decode.c")
  target_include_directories(ge007_bumpmap_decode_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/mktex/src")
  target_compile_features(ge007_bumpmap_decode_test PRIVATE cxx_std_20)
  add_test(NAME ge007_bumpmap_decode
           COMMAND ge007_bumpmap_decode_test)

  add_executable(ge007_bumpmap_metadata_test
    tests/bumpmap_metadata_test.cpp
    "${CMAKE_SOURCE_DIR}/tools/mktex/src/bumpmap_metadata.c")
  target_include_directories(ge007_bumpmap_metadata_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/mktex/src")
  target_compile_features(ge007_bumpmap_metadata_test PRIVATE cxx_std_20)
  add_test(NAME ge007_bumpmap_metadata
           COMMAND ge007_bumpmap_metadata_test)

  add_executable(ge007_pdtex_stream_test
    tests/pdtex_stream_test.cpp
    "${CMAKE_SOURCE_DIR}/tools/mktex/src/libpdtex/pdtex.c")
  target_include_directories(ge007_pdtex_stream_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tools/mktex/src")
  target_compile_features(ge007_pdtex_stream_test PRIVATE cxx_std_20)
  add_test(NAME ge007_pdtex_stream
           COMMAND ge007_pdtex_stream_test)

  add_executable(ge007_bumpmap_runtime_test
    tests/bumpmap_runtime_test.cpp
    "${CMAKE_SOURCE_DIR}/port/fast3d/bumpmap_runtime.cpp")
  target_include_directories(ge007_bumpmap_runtime_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_definitions(ge007_bumpmap_runtime_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_bumpmap_runtime_test PRIVATE cxx_std_20)
  target_link_libraries(ge007_bumpmap_runtime_test PRIVATE PNG::PNG)
  add_test(NAME ge007_bumpmap_runtime
           COMMAND ge007_bumpmap_runtime_test)

  add_executable(ge007_depth_of_field_policy_test
    tests/depth_of_field_policy_test.cpp)
  target_include_directories(ge007_depth_of_field_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_depth_of_field_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_depth_of_field_policy
           COMMAND ge007_depth_of_field_policy_test)

  add_executable(ge007_gfx_wayland_surface_policy_test
    tests/gfx_wayland_surface_policy_test.cpp)
  target_include_directories(ge007_gfx_wayland_surface_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_gfx_wayland_surface_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_gfx_wayland_surface_policy
           COMMAND ge007_gfx_wayland_surface_policy_test)

  add_executable(ge007_ascension_postfx_shader_test
    tests/ascension_postfx_shader_test.cpp
    port/fast3d/glad/glad.c)
  target_compile_features(ge007_ascension_postfx_shader_test PRIVATE cxx_std_20)
  target_compile_definitions(ge007_ascension_postfx_shader_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(ge007_ascension_postfx_shader_test PRIVATE
    "${SDL2_LIBRARY}" "${GL_LIBRARY}")
  add_test(NAME ge007_ascension_postfx_shader_contract
           COMMAND ge007_ascension_postfx_shader_test)
  set_tests_properties(ge007_ascension_postfx_shader_contract
    PROPERTIES LABELS "ge007-unit;ge007-shader")

  find_program(GE007_GLSLANG_VALIDATOR NAMES glslangValidator)
  if(GE007_GLSLANG_VALIDATOR)
    add_test(NAME ge007_ascension_postfx_shader_compile
             COMMAND ge007_ascension_postfx_shader_test --compile
                     "${GE007_GLSLANG_VALIDATOR}")
    set_tests_properties(ge007_ascension_postfx_shader_compile
      PROPERTIES LABELS "ge007-unit;ge007-shader")
  else()
    message(STATUS
      "glslangValidator not found: PostFX shader compiler test will be omitted")
  endif()

  find_program(GE007_XVFB NAMES Xvfb)
  if(GE007_XVFB)
    add_test(NAME ge007_ascension_postfx_gpu
             COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_headless_gl.sh"
                     "${GE007_XVFB}"
                     $<TARGET_FILE:ge007_ascension_postfx_shader_test> --gpu)
    set_tests_properties(ge007_ascension_postfx_gpu
      PROPERTIES
        LABELS ge007-gpu
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=x11"
        SKIP_RETURN_CODE 77)
  else()
    message(STATUS
      "Xvfb not found: headless PostFX GPU test will be omitted")
  endif()

  add_executable(ge007_world_lighting_integration_test
    tests/world_lighting_integration_test.c
    port/src/world_lighting.c)
  add_test(NAME ge007_world_lighting_integration
           COMMAND ge007_world_lighting_integration_test)

  add_executable(ge007_gu_matrix_test
    tests/gu_matrix_test.cpp
    src/libultra/gu/rotate.c
    src/libultra/gu/scale.c
    src/libultra/gu/translate.c
    src/libultra/gu/ortho.c
    src/libultra/gu/perspective.c
    src/libultra/gu/lookat.c
    src/libultra/gu/sins.c
    src/libultra/gu/coss.c
    src/libultra/gu/mtxutil.c
    src/libultra/gu/normalize.c
    src/libultra/gu/align.c)
  target_compile_features(ge007_gu_matrix_test PRIVATE cxx_std_20)
  if(UNIX)
    target_link_libraries(ge007_gu_matrix_test PRIVATE m)
  endif()
  add_test(NAME ge007_gu_matrix
           COMMAND ge007_gu_matrix_test)

  add_executable(ge007_matrixmath_test
    tests/matrixmath_test.cpp
    src/game/matrixmath.c
    src/game/matrixmath_misc.c
    src/libultra/gu/normalize.c
    src/libultra/gu/align.c
    src/libultra/gu/mtxutil.c)
  target_compile_features(ge007_matrixmath_test PRIVATE cxx_std_20)
  add_test(NAME ge007_matrixmath
           COMMAND ge007_matrixmath_test)

  add_executable(ge007_game_math_test
    tests/game_math_test.cpp
    src/game/math_asinacos.c
    src/game/math_asinfacosf.c
    src/game/math_atan2f.c
    src/game/math_ceil.c
    src/game/math_floor.c
    src/game/math_unk_05A9E0.c
    src/game/quaternion.c)
  target_compile_features(ge007_game_math_test PRIVATE cxx_std_20)
  add_test(NAME ge007_game_math
           COMMAND ge007_game_math_test)

  add_executable(ge007_geometry_test
    tests/geometry_test.cpp
    src/game/stanintersection.c)
  target_compile_features(ge007_geometry_test PRIVATE cxx_std_20)
  add_test(NAME ge007_geometry
           COMMAND ge007_geometry_test)

  add_executable(ge007_rle_test
    tests/rle_test.cpp
    src/game/rle.c)
  target_compile_features(ge007_rle_test PRIVATE cxx_std_20)
  add_test(NAME ge007_rle
           COMMAND ge007_rle_test)

  add_executable(ge007_line_tri_intersect_test
    tests/line_tri_intersect_test.cpp
    src/game/line_tri_intersect.c)
  target_compile_features(ge007_line_tri_intersect_test PRIVATE cxx_std_20)
  add_test(NAME ge007_line_tri_intersect
           COMMAND ge007_line_tri_intersect_test)

  add_executable(ge007_blood_decrypt_test
    tests/blood_decrypt_test.cpp
    src/game/blood_decrypt.c)
  target_compile_features(ge007_blood_decrypt_test PRIVATE cxx_std_20)
  add_test(NAME ge007_blood_decrypt
           COMMAND ge007_blood_decrypt_test)

  add_executable(ge007_blood_animation_test
    tests/blood_animation_test.c
    src/game/blood_animation.c)
  target_compile_features(ge007_blood_animation_test PRIVATE c_std_11)
  target_compile_options(ge007_blood_animation_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_blood_animation_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_blood_animation
           COMMAND ge007_blood_animation_test)

  add_executable(ge007_rzdecomp_test
    tests/rzdecomp_test.cpp
    port/src/rzdecomp.c)
  target_compile_features(ge007_rzdecomp_test PRIVATE cxx_std_20)
  target_link_libraries(ge007_rzdecomp_test PRIVATE ZLIB::ZLIB)
  add_test(NAME ge007_rzdecomp
           COMMAND ge007_rzdecomp_test)

  add_executable(ge007_zlib_inflate_test
    tests/zlib_inflate_test.cpp
    src/game/zlib.c)
  target_compile_features(ge007_zlib_inflate_test PRIVATE cxx_std_20)
  target_compile_options(ge007_zlib_inflate_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_zlib_inflate_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_zlib_inflate_test PRIVATE ZLIB::ZLIB)
  add_test(NAME ge007_zlib_inflate
           COMMAND ge007_zlib_inflate_test)

  add_executable(ge007_crc_test
    tests/crc_test.cpp
    src/game/crc.c
    port/src/random.c)
  target_compile_features(ge007_crc_test PRIVATE cxx_std_20)
  add_test(NAME ge007_crc
           COMMAND ge007_crc_test)

  add_executable(ge007_objective_status2_test
    tests/objective_status2_test.cpp
    src/game/objective_status2.c)
  target_compile_features(ge007_objective_status2_test PRIVATE cxx_std_20)
  add_test(NAME ge007_objective_status2
           COMMAND ge007_objective_status2_test)

  add_executable(ge007_objective_test
    tests/objective_test.cpp
    src/game/objective.c
    src/game/objective_status.c)
  target_compile_features(ge007_objective_test PRIVATE cxx_std_20)
  target_compile_options(ge007_objective_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_objective_test PRIVATE -Wl,--gc-sections)
  add_test(NAME ge007_objective
           COMMAND ge007_objective_test)

  add_executable(ge007_loadobjectmodel_test
    tests/loadobjectmodel_test.cpp
    src/game/loadobjectmodel.c)
  target_compile_features(ge007_loadobjectmodel_test PRIVATE cxx_std_20)
  target_compile_options(ge007_loadobjectmodel_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_loadobjectmodel_test PRIVATE -Wl,--gc-sections)
  add_test(NAME ge007_loadobjectmodel
           COMMAND ge007_loadobjectmodel_test)

  add_executable(ge007_cctv_orientation_test
    tests/cctv_orientation_test.cpp
    src/game/cctv.c)
  target_compile_features(ge007_cctv_orientation_test PRIVATE cxx_std_20)
  add_test(NAME ge007_cctv_orientation
           COMMAND ge007_cctv_orientation_test)

  add_executable(ge007_bondview_r_coop_spawn_test
    tests/bondview_r_coop_spawn_test.c
    port/src/coop.c)
  target_compile_options(ge007_bondview_r_coop_spawn_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bondview_r_coop_spawn_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_bondview_r_coop_spawn
           COMMAND ge007_bondview_r_coop_spawn_test)

  add_executable(ge007_record_damage_kills_test
    tests/record_damage_kills_test.c
    src/game/bondview2.c)
  target_compile_options(ge007_record_damage_kills_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_record_damage_kills_test PRIVATE
    -Wl,--gc-sections -Wl,--wrap=bondviewKillCurrentPlayer)
  if(UNIX)
    target_link_libraries(ge007_record_damage_kills_test PRIVATE m)
  endif()
  add_test(NAME ge007_record_damage_kills
           COMMAND ge007_record_damage_kills_test)

  add_executable(ge007_coop_test
    tests/coop_test.cpp
    port/src/coop.c)
  target_include_directories(ge007_coop_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_coop
           COMMAND ge007_coop_test)

  add_executable(ge007_fake_controller_test
    tests/fake_controller_test.cpp
    port/src/input_harness.c)
  target_include_directories(ge007_fake_controller_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_fake_controller
           COMMAND ge007_fake_controller_test)

  add_executable(ge007_controller_mapping_test
    tests/controller_mapping_test.cpp
    port/src/controller_mapping.c)
  target_include_directories(ge007_controller_mapping_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_controller_mapping
           COMMAND ge007_controller_mapping_test)

  add_executable(ge007_input_mapping_test
    tests/input_mapping_test.cpp
    port/src/input_harness.c)
  target_include_directories(ge007_input_mapping_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_input_mapping
           COMMAND ge007_input_mapping_test)

  add_executable(ge007_sight_policy_test
    tests/sight_policy_test.cpp)
  target_include_directories(ge007_sight_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_sight_policy
           COMMAND ge007_sight_policy_test)

  add_executable(ge007_viewport_policy_test
    tests/viewport_policy_test.cpp)
  target_include_directories(ge007_viewport_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_viewport_policy
           COMMAND ge007_viewport_policy_test)

  add_executable(ge007_defect_contract_test
    tests/defect_contract_test.cpp)
  target_include_directories(ge007_defect_contract_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_defect_contract
           COMMAND ge007_defect_contract_test)

  add_executable(ge007_ambient_lighting_regression_test
    tests/ambient_lighting_regression_test.cpp)
  target_compile_definitions(ge007_ambient_lighting_regression_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_ambient_lighting_regression_test PRIVATE
    cxx_std_20)
  add_test(NAME ge007_ambient_lighting_regression
           COMMAND ge007_ambient_lighting_regression_test)

  add_executable(ge007_water_render_policy_test
    tests/water_render_policy_test.cpp)
  target_include_directories(ge007_water_render_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_water_render_policy
           COMMAND ge007_water_render_policy_test)

  add_executable(ge007_water_render_reference_math_test
    tests/water_render_reference_math_test.cpp)
  target_include_directories(ge007_water_render_reference_math_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  target_compile_features(ge007_water_render_reference_math_test PRIVATE
    cxx_std_20)
  add_test(NAME ge007_water_render_reference_math
           COMMAND ge007_water_render_reference_math_test)

  add_executable(ge007_water_time_policy_test
    tests/water_time_policy_test.cpp)
  target_include_directories(ge007_water_time_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  target_compile_features(ge007_water_time_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_water_time_policy
           COMMAND ge007_water_time_policy_test)

  add_executable(ge007_water_heightfield_policy_test
    tests/water_heightfield_policy_test.cpp)
  target_include_directories(ge007_water_heightfield_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  target_compile_features(ge007_water_heightfield_policy_test PRIVATE cxx_std_20)
  add_test(NAME ge007_water_heightfield_policy
           COMMAND ge007_water_heightfield_policy_test)

  add_executable(ge007_water_heightfield_integration_contract_test
    tests/water_heightfield_integration_contract_test.cpp)
  target_compile_definitions(ge007_water_heightfield_integration_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_water_heightfield_integration_contract_test PRIVATE
    cxx_std_20)
  target_include_directories(ge007_water_heightfield_integration_contract_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")
  add_test(NAME ge007_water_heightfield_integration_contract
           COMMAND ge007_water_heightfield_integration_contract_test)

  add_executable(ge007_water_live_shader_contract_test
    tests/water_live_shader_contract_test.cpp)
  target_compile_definitions(ge007_water_live_shader_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_water_live_shader_contract_test PRIVATE
    cxx_std_20)
  target_include_directories(ge007_water_live_shader_contract_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")
  add_test(NAME ge007_water_live_shader_contract
           COMMAND ge007_water_live_shader_contract_test)

  add_executable(ge007_water_cpu_render_harness_test
    tests/water_cpu_render_harness_test.cpp)
  target_include_directories(ge007_water_cpu_render_harness_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include"
    "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(ge007_water_cpu_render_harness_test PRIVATE cxx_std_20)
  add_test(NAME ge007_water_cpu_render_harness
           COMMAND ge007_water_cpu_render_harness_test --self-test)

  add_executable(ge007_water_render_pixel_oracle_test
    tests/water_render_pixel_oracle_test.cpp)
  target_include_directories(ge007_water_render_pixel_oracle_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(ge007_water_render_pixel_oracle_test PRIVATE
    cxx_std_17)
  add_test(NAME ge007_water_render_pixel_oracle
           COMMAND ge007_water_render_pixel_oracle_test --self-test)

  add_executable(ge007_model_render_pixel_oracle_test
    tests/model_render_pixel_oracle_test.cpp)
  target_include_directories(ge007_model_render_pixel_oracle_test PRIVATE
    "${CMAKE_SOURCE_DIR}/tests")
  target_compile_features(ge007_model_render_pixel_oracle_test PRIVATE cxx_std_20)
  add_test(NAME ge007_model_render_pixel_oracle
           COMMAND ge007_model_render_pixel_oracle_test --self-test)

  add_executable(ge007_water_render_quality_contract_test
    tests/water_render_quality_contract_test.cpp)
  target_include_directories(ge007_water_render_quality_contract_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_definitions(ge007_water_render_quality_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_features(ge007_water_render_quality_contract_test PRIVATE cxx_std_20)
  add_test(NAME ge007_water_render_quality_contract
           COMMAND ge007_water_render_quality_contract_test)

  add_executable(ge007_water_render_bdd_contract_test
    tests/water_render_bdd_contract_test.cpp)
  target_compile_features(ge007_water_render_bdd_contract_test PRIVATE
    cxx_std_20)
  target_compile_definitions(ge007_water_render_bdd_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ge007_water_render_bdd_contract
           COMMAND ge007_water_render_bdd_contract_test)

  add_executable(ge007_water_render_missed_contract_test
    tests/water_render_missed_contract_test.cpp)
  target_compile_features(ge007_water_render_missed_contract_test PRIVATE
    cxx_std_20)
  target_compile_definitions(ge007_water_render_missed_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ge007_water_render_missed_contract
           COMMAND ge007_water_render_missed_contract_test)

  add_executable(ge007_world_water_surface_contract_test
    tests/world_water_surface_contract_test.cpp)
  target_compile_features(ge007_world_water_surface_contract_test PRIVATE
    cxx_std_20)
  target_compile_definitions(ge007_world_water_surface_contract_test PRIVATE
    GE007_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME ge007_world_water_surface_contract
           COMMAND ge007_world_water_surface_contract_test)

  add_executable(ge007_muzzle_flash_contract_test
    tests/muzzle_flash_contract_test.cpp
    src/game/matrixmath.c
    src/game/matrixmath_misc.c
    src/libultra/gu/normalize.c
    src/libultra/gu/align.c
    src/libultra/gu/mtxutil.c)
  target_include_directories(ge007_muzzle_flash_contract_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/include")
  add_test(NAME ge007_muzzle_flash_contract
           COMMAND ge007_muzzle_flash_contract_test)

  add_executable(ge007_game_utility_edge_test
    tests/game_utility_edge_test.c
    src/game/cleanup_alarms.c
    src/game/cleanup_objectives.c
    src/game/cleanwindowpieces.c
    src/game/compiletime.c
    src/game/initnull_0009D0.c
    src/game/initnull_000BC0.c
    src/game/mp_weapon.c
    src/game/null_007970.c
    src/game/padhalllv.c
    src/game/setguscale.c)
  target_compile_features(ge007_game_utility_edge_test PRIVATE c_std_11)
  target_compile_options(ge007_game_utility_edge_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_game_utility_edge_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_game_utility_edge
           COMMAND ge007_game_utility_edge_test)

  add_executable(ge007_bondhead_test
    tests/bondhead_test.c
    src/game/bondhead.c)
  target_compile_features(ge007_bondhead_test PRIVATE c_std_11)
  target_compile_options(ge007_bondhead_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bondhead_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_bondhead
           COMMAND ge007_bondhead_test)

  add_executable(ge007_music_tracks_test
    tests/music_tracks_test.c
    src/game/music_0D2720.c)
  target_compile_features(ge007_music_tracks_test PRIVATE c_std_11)
  target_compile_options(ge007_music_tracks_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_music_tracks_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_music_tracks
           COMMAND ge007_music_tracks_test)

  add_executable(ge007_bgfog_test
    tests/bgfog_test.c
    src/game/bgfog.c)
  target_compile_features(ge007_bgfog_test PRIVATE c_std_11)
  target_compile_options(ge007_bgfog_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bgfog_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_bgfog
           COMMAND ge007_bgfog_test)

  add_executable(ge007_bg_apply_lut_test
    tests/bg_apply_lut_test.c
    src/game/bgapply.c)
  target_compile_features(ge007_bg_apply_lut_test PRIVATE c_std_11)
  target_compile_options(ge007_bg_apply_lut_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bg_apply_lut_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_bg_apply_lut
           COMMAND ge007_bg_apply_lut_test)

  add_executable(ge007_bg_room_trans_test
    tests/bg_room_trans_test.c
    src/game/bgroomtrans.c)
  target_compile_features(ge007_bg_room_trans_test PRIVATE c_std_11)
  target_compile_options(ge007_bg_room_trans_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bg_room_trans_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_bg_room_trans_test PRIVATE m)
  endif()
  add_test(NAME ge007_bg_room_trans
           COMMAND ge007_bg_room_trans_test)

  add_executable(ge007_file_state_test
    tests/file_state_test.c
    src/game/file.c)
  target_compile_features(ge007_file_state_test PRIVATE c_std_11)
  target_compile_options(ge007_file_state_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_file_state_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_file_state
           COMMAND ge007_file_state_test)

  add_executable(ge007_file2_handler_test
    tests/file2_handler_test.c
    src/game/file2.c)
  target_compile_features(ge007_file2_handler_test PRIVATE c_std_11)
  target_compile_options(ge007_file2_handler_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_file2_handler_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_file2_handler
           COMMAND ge007_file2_handler_test)

  add_executable(ge007_ob_resource_test
    tests/ob_resource_test.c
    src/game/ob.c
    ${SRC_ROMASSETS})
  target_compile_features(ge007_ob_resource_test PRIVATE c_std_11)
  target_compile_options(ge007_ob_resource_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_ob_resource_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_ob_resource
           COMMAND ge007_ob_resource_test)

  add_executable(ge007_objecthandler2_test
    tests/objecthandler2_test.c
    src/game/objecthandler_2.c)
  target_compile_features(ge007_objecthandler2_test PRIVATE c_std_11)
  target_compile_options(ge007_objecthandler2_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_objecthandler2_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_objecthandler2
           COMMAND ge007_objecthandler2_test)

  add_executable(ge007_model_pointer_promotion_test
    tests/model_pointer_promotion_test.c
    src/game/model.c)
  target_compile_features(ge007_model_pointer_promotion_test PRIVATE c_std_11)
  target_compile_options(ge007_model_pointer_promotion_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_model_pointer_promotion_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_model_pointer_promotion
           COMMAND ge007_model_pointer_promotion_test)

  add_executable(ge007_model_animation_test
    tests/model_animation_test.c
    src/game/model.c
    src/game/math_ceil.c)
  target_compile_features(ge007_model_animation_test PRIVATE c_std_11)
  target_compile_options(ge007_model_animation_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_model_animation_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_model_animation
           COMMAND ge007_model_animation_test)

  add_executable(ge007_tex_policy_test
    tests/tex_policy_test.c
    src/game/tex.c)
  target_compile_features(ge007_tex_policy_test PRIVATE c_std_11)
  target_compile_options(ge007_tex_policy_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_tex_policy_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_tex_policy
           COMMAND ge007_tex_policy_test)

  add_executable(ge007_image_texture_test
    tests/image_texture_test.c
    src/game/image.c
    src/game/image_bank.c)
  target_include_directories(ge007_image_texture_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_image_texture_test PRIVATE c_std_11)
  target_compile_options(ge007_image_texture_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_image_texture_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_image_texture
           COMMAND ge007_image_texture_test)

  add_executable(ge007_image_bank_test
    tests/image_bank_test.c
    src/game/image_bank.c)
  target_include_directories(ge007_image_bank_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_image_bank_test PRIVATE c_std_11)
  target_compile_options(ge007_image_bank_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_image_bank_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_image_bank
           COMMAND ge007_image_bank_test)

  add_executable(ge007_chr_b_test
    tests/chr_b_test.c
    src/game/chr_b.c)
  target_include_directories(ge007_chr_b_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_chr_b_test PRIVATE c_std_11)
  target_compile_options(ge007_chr_b_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_chr_b_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_chr_b
           COMMAND ge007_chr_b_test)

  add_executable(ge007_initanitable_test
    tests/initanitable_test.c
    src/game/initanitable.c)
  target_include_directories(ge007_initanitable_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_initanitable_test PRIVATE c_std_11)
  target_compile_options(ge007_initanitable_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_initanitable_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_initanitable
           COMMAND ge007_initanitable_test)

  add_executable(ge007_deb_loadallmodels_test
    tests/deb_loadallmodels_test.c
    src/game/deb_loadallmodels.c)
  target_include_directories(ge007_deb_loadallmodels_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_deb_loadallmodels_test PRIVATE c_std_11)
  target_compile_options(ge007_deb_loadallmodels_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_deb_loadallmodels_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_deb_loadallmodels
           COMMAND ge007_deb_loadallmodels_test)

  add_executable(ge007_init_bond_data_defaults_test
    tests/initBondDATAdefaults_test.c
    src/game/initBondDATAdefaults.c)
  target_include_directories(ge007_init_bond_data_defaults_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_init_bond_data_defaults_test PRIVATE c_std_11)
  target_compile_options(ge007_init_bond_data_defaults_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_bond_data_defaults_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_bond_data_defaults
           COMMAND ge007_init_bond_data_defaults_test)

  add_executable(ge007_init_actor_propstuff_test
    tests/initactorpropstuff_test.c
    src/game/initactorpropstuff.c)
  target_include_directories(ge007_init_actor_propstuff_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_init_actor_propstuff_test PRIVATE c_std_11)
  target_compile_options(ge007_init_actor_propstuff_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_actor_propstuff_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_actor_propstuff
           COMMAND ge007_init_actor_propstuff_test)

  add_executable(ge007_init_bond_data_test
    tests/initBondDATA_test.c
    src/game/initBondDATA.c)
  target_include_directories(ge007_init_bond_data_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_init_bond_data_test PRIVATE c_std_11)
  target_compile_options(ge007_init_bond_data_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_bond_data_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_bond_data
           COMMAND ge007_init_bond_data_test)

  add_executable(ge007_unk_092e50_test
    tests/unk_092E50_test.c
    src/game/unk_092E50.c)
  target_include_directories(ge007_unk_092e50_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_unk_092e50_test PRIVATE c_std_11)
  target_compile_options(ge007_unk_092e50_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_unk_092e50_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_unk_092e50_test PRIVATE m)
  add_test(NAME ge007_unk_092e50
           COMMAND ge007_unk_092e50_test)

  add_executable(ge007_glass_test
    tests/glass_test.c
    src/game/glass.c)
  target_include_directories(ge007_glass_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_glass_test PRIVATE c_std_11)
  target_compile_options(ge007_glass_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_glass_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_glass_test PRIVATE m)
  add_test(NAME ge007_glass
           COMMAND ge007_glass_test)

  add_executable(ge007_glass2_test
    tests/glass2_test.c
    src/game/glass2.c)
  target_include_directories(ge007_glass2_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_glass2_test PRIVATE c_std_11)
  target_compile_options(ge007_glass2_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_glass2_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_glass2_test PRIVATE m)
  add_test(NAME ge007_glass2
           COMMAND ge007_glass2_test)

  add_executable(ge007_debugmenu_navigation_test
    tests/debugmenu_navigation_test.c
    src/game/debugmenu_08FE00.c)
  target_compile_features(ge007_debugmenu_navigation_test PRIVATE c_std_11)
  target_compile_options(ge007_debugmenu_navigation_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_debugmenu_navigation_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_debugmenu_navigation
           COMMAND ge007_debugmenu_navigation_test)

  add_executable(ge007_bondinv_test
    tests/bondinv_test.c
    src/game/bondinv.c)
  target_include_directories(ge007_bondinv_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_bondinv_test PRIVATE c_std_11)
  target_compile_options(ge007_bondinv_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bondinv_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_bondinv_test PRIVATE m)
  add_test(NAME ge007_bondinv
           COMMAND ge007_bondinv_test)

  add_executable(ge007_textrelated_policy_test
    tests/textrelated_policy_test.c
    src/game/textrelated.c)
  target_include_directories(ge007_textrelated_policy_test PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_textrelated_policy_test PRIVATE c_std_11)
  target_compile_options(ge007_textrelated_policy_test PRIVATE
    -ffunction-sections -fdata-sections
    -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(ge007_textrelated_policy_test PRIVATE
    -Wl,--gc-sections
    -fsanitize=address)
  add_test(NAME ge007_textrelated_policy
           COMMAND ge007_textrelated_policy_test)
  set_tests_properties(ge007_textrelated_policy
    PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

  add_executable(ge007_sky_policy_test
    tests/sky_policy_test.c
    src/game/sky.c)
  target_compile_features(ge007_sky_policy_test PRIVATE c_std_11)
  target_compile_options(ge007_sky_policy_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_sky_policy_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_sky_policy_test PRIVATE m)
  endif()
  add_test(NAME ge007_sky_policy
           COMMAND ge007_sky_policy_test)

  add_executable(ge007_lightfixture_test
    tests/lightfixture_test.c
    src/game/lightfixture.c)
  target_compile_features(ge007_lightfixture_test PRIVATE c_std_11)
  target_compile_options(ge007_lightfixture_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_lightfixture_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_lightfixture_test PRIVATE m)
  endif()
  add_test(NAME ge007_lightfixture
           COMMAND ge007_lightfixture_test)

  add_executable(ge007_vtxstore_test
    tests/vtxstore_test.c
    src/game/vtxstore.c)
  target_compile_features(ge007_vtxstore_test PRIVATE c_std_11)
  target_compile_options(ge007_vtxstore_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_vtxstore_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_vtxstore
           COMMAND ge007_vtxstore_test)

  add_executable(ge007_debug_camera_test
    tests/debug_camera_test.c
    src/game/debug_camera.c)
  target_compile_features(ge007_debug_camera_test PRIVATE c_std_11)
  target_compile_definitions(ge007_debug_camera_test PRIVATE
    GE007_DEBUG_CAMERA_TEST)
  target_compile_options(ge007_debug_camera_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_debug_camera_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_debug_camera_test PRIVATE m)
  endif()
  add_test(NAME ge007_debug_camera
           COMMAND ge007_debug_camera_test)

  add_executable(ge007_title3_display_list_test
    tests/title3_display_list_test.c
    src/game/title3.c)
  target_compile_features(ge007_title3_display_list_test PRIVATE c_std_11)
  target_compile_options(ge007_title3_display_list_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_title3_display_list_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_title3_display_list_test PRIVATE m)
  endif()
  add_test(NAME ge007_title3_display_list
           COMMAND ge007_title3_display_list_test)

  add_executable(ge007_title2_display_list_test
    tests/title2_display_list_test.c
    src/game/title2.c)
  target_compile_features(ge007_title2_display_list_test PRIVATE c_std_11)
  target_compile_options(ge007_title2_display_list_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_title2_display_list_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_title2_display_list
           COMMAND ge007_title2_display_list_test)

  add_executable(ge007_indy_comms_test
    tests/indy_comms_test.c
    src/game/indy_comms.c)
  target_compile_features(ge007_indy_comms_test PRIVATE c_std_11)
  target_compile_options(ge007_indy_comms_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_indy_comms_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_indy_comms
           COMMAND ge007_indy_comms_test)

  add_executable(ge007_bondwalk2_display_test
    tests/bondwalk2_display_test.c
    src/game/bondwalk2.c)
  target_compile_features(ge007_bondwalk2_display_test PRIVATE c_std_11)
  target_compile_options(ge007_bondwalk2_display_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_bondwalk2_display_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_bondwalk2_display
           COMMAND ge007_bondwalk2_display_test)

  add_executable(ge007_speedgraphrenderer_test
    tests/speedgraphrenderer_test.c
    src/game/speedgraphrenderer.c)
  target_compile_features(ge007_speedgraphrenderer_test PRIVATE c_std_11)
  target_compile_options(ge007_speedgraphrenderer_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_speedgraphrenderer_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_speedgraphrenderer
           COMMAND ge007_speedgraphrenderer_test)

  add_executable(ge007_mp_music_test
    tests/mp_music_test.c
    src/game/mp_music.c)
  target_compile_features(ge007_mp_music_test PRIVATE c_std_11)
  target_compile_options(ge007_mp_music_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_mp_music_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_mp_music
           COMMAND ge007_mp_music_test)

  add_executable(ge007_dyn_allocator_test
    tests/dyn_allocator_test.c
    src/game/dyn.c)
  target_compile_features(ge007_dyn_allocator_test PRIVATE c_std_11)
  target_compile_options(ge007_dyn_allocator_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_dyn_allocator_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_dyn_allocator
           COMMAND ge007_dyn_allocator_test)

  add_executable(ge007_radar_test
    tests/radar_test.c
    src/game/radar.c)
  target_compile_features(ge007_radar_test PRIVATE c_std_11)
  target_compile_options(ge007_radar_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_radar_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_radar_test PRIVATE m)
  endif()
  add_test(NAME ge007_radar
           COMMAND ge007_radar_test)

  add_executable(ge007_player_state_test
    tests/player_state_test.c
    src/game/player.c)
  target_compile_features(ge007_player_state_test PRIVATE c_std_11)
  target_compile_options(ge007_player_state_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_player_state_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_player_state
           COMMAND ge007_player_state_test)

  add_executable(ge007_mp_menu_policy_test
    tests/mp_menu_policy_test.c
    src/game/mpmenu.c)
  target_compile_features(ge007_mp_menu_policy_test PRIVATE c_std_11)
  target_compile_options(ge007_mp_menu_policy_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_mp_menu_policy_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_mp_menu_policy_test PRIVATE m)
  endif()
  add_test(NAME ge007_mp_menu_policy
           COMMAND ge007_mp_menu_policy_test)

  add_executable(ge007_ramrom_resume_test
    tests/ramrom_resume_test.c
    src/game/ramromreplay.c)
  target_compile_features(ge007_ramrom_resume_test PRIVATE c_std_11)
  target_compile_options(ge007_ramrom_resume_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_ramrom_resume_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_ramrom_resume_test PRIVATE m)
  endif()
  add_test(NAME ge007_ramrom_resume
           COMMAND ge007_ramrom_resume_test)

  add_executable(ge007_options_state_test
    tests/options_state_test.c
    src/game/options.c)
  target_compile_features(ge007_options_state_test PRIVATE c_std_11)
  target_compile_options(ge007_options_state_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_options_state_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_options_state_test PRIVATE m)
  endif()
  add_test(NAME ge007_options_state
           COMMAND ge007_options_state_test)

  add_executable(ge007_gun_sight_render_test
    tests/gun_sight_render_test.c
    src/game/gunfire.c)
  target_compile_features(ge007_gun_sight_render_test PRIVATE c_std_11)
  target_compile_options(ge007_gun_sight_render_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_gun_sight_render_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_gun_sight_render_test PRIVATE m)
  endif()
  add_test(NAME ge007_gun_sight_render
           COMMAND ge007_gun_sight_render_test)

  add_executable(ge007_options_overlay_test
    tests/options_overlay_test.c
    port/src/optionsoverlay.c)
  target_compile_features(ge007_options_overlay_test PRIVATE c_std_11)
  target_compile_options(ge007_options_overlay_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_options_overlay_test PRIVATE
    -Wl,--gc-sections)
  target_link_libraries(ge007_options_overlay_test PRIVATE
    "${SDL2_LIBRARY}" m)
  add_test(NAME ge007_options_overlay
           COMMAND ge007_options_overlay_test)
  set_tests_properties(ge007_options_overlay
    PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

  add_executable(ge007_language_state_test
    tests/language_state_test.c
    src/game/language.c)
  target_compile_features(ge007_language_state_test PRIVATE c_std_11)
  target_compile_options(ge007_language_state_test PRIVATE
    -ffunction-sections -fdata-sections
    -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(ge007_language_state_test PRIVATE
    -Wl,--gc-sections
    -fsanitize=address)
  if(UNIX)
    target_link_libraries(ge007_language_state_test PRIVATE m)
  endif()
  add_test(NAME ge007_language_state
           COMMAND ge007_language_state_test)
  set_tests_properties(ge007_language_state PROPERTIES
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

  add_executable(ge007_rsp_task_test
    tests/rsp_task_test.c
    src/game/rsp.c)
  target_compile_features(ge007_rsp_task_test PRIVATE c_std_11)
  target_compile_options(ge007_rsp_task_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_rsp_task_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_rsp_task
           COMMAND ge007_rsp_task_test)

  add_executable(ge007_othermodemicrocode_test
    tests/othermodemicrocode_test.c
    src/game/othermodemicrocode.c)
  target_compile_features(ge007_othermodemicrocode_test PRIVATE c_std_11)
  target_compile_options(ge007_othermodemicrocode_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_othermodemicrocode_test PRIVATE
    -Wl,--gc-sections)
  if(UNIX)
    target_link_libraries(ge007_othermodemicrocode_test PRIVATE m)
  endif()
  add_test(NAME ge007_othermodemicrocode
           COMMAND ge007_othermodemicrocode_test)

  add_executable(ge007_model_hit_freelist_test
    tests/model_hit_freelist_test.c
    src/game/initunk_005450.c)
  target_compile_features(ge007_model_hit_freelist_test PRIVATE c_std_11)
  target_compile_options(ge007_model_hit_freelist_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_model_hit_freelist_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_model_hit_freelist
           COMMAND ge007_model_hit_freelist_test)

  add_executable(ge007_game_initialization_edge_test
    tests/game_initialization_edge_test.c
    src/game/initimages.c
    src/game/initunk_0009E0.c
    src/game/initunk_007290.c)
  target_compile_features(ge007_game_initialization_edge_test PRIVATE c_std_11)
  target_compile_options(ge007_game_initialization_edge_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_game_initialization_edge_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_game_initialization_edge
           COMMAND ge007_game_initialization_edge_test)

  add_executable(ge007_init_path_table_test
    tests/init_path_table_test.c
    src/game/initpathtablesomething.c)
  target_compile_features(ge007_init_path_table_test PRIVATE c_std_11)
  target_compile_options(ge007_init_path_table_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_path_table_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_path_table
           COMMAND ge007_init_path_table_test)

  add_executable(ge007_init_path_table_links_test
    tests/initpathtablelinks_test.c
    src/game/initpathtablelinks.c)
  target_compile_features(ge007_init_path_table_links_test PRIVATE c_std_11)
  target_compile_options(ge007_init_path_table_links_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_path_table_links_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_path_table_links
           COMMAND ge007_init_path_table_links_test)

  add_executable(ge007_init_weapon_animation_groups_test
    tests/init_weapon_animation_groups_test.c
    src/game/initweaponanigroups.c)
  target_compile_features(ge007_init_weapon_animation_groups_test PRIVATE c_std_11)
  target_compile_options(ge007_init_weapon_animation_groups_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_weapon_animation_groups_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_weapon_animation_groups
           COMMAND ge007_init_weapon_animation_groups_test)

  add_executable(ge007_init_item_slots_test
    tests/init_item_slots_test.c
    src/game/inititemslots.c)
  target_compile_features(ge007_init_item_slots_test PRIVATE c_std_11)
  target_compile_options(ge007_init_item_slots_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_init_item_slots_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_init_item_slots
           COMMAND ge007_init_item_slots_test)

  add_executable(ge007_sound_cleanup_test
    tests/sound_cleanup_test.c
    src/game/cleanup_object_sounds.c
    src/game/cleanupSFXRelated.c)
  target_compile_features(ge007_sound_cleanup_test PRIVATE c_std_11)
  target_compile_options(ge007_sound_cleanup_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_sound_cleanup_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_sound_cleanup
           COMMAND ge007_sound_cleanup_test)

  add_executable(ge007_explosion_cleanup_test
    tests/explosion_cleanup_test.c
    src/game/cleanexplosions.c)
  target_compile_features(ge007_explosion_cleanup_test PRIVATE c_std_11)
  target_compile_options(ge007_explosion_cleanup_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_explosion_cleanup_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_explosion_cleanup
           COMMAND ge007_explosion_cleanup_test)

  add_executable(ge007_guard_cleanup_test
    tests/guard_cleanup_test.c
    src/game/cleanup_guard_data.c)
  target_compile_features(ge007_guard_cleanup_test PRIVATE c_std_11)
  target_compile_options(ge007_guard_cleanup_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_guard_cleanup_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_guard_cleanup
           COMMAND ge007_guard_cleanup_test)

  add_executable(ge007_player_favorite_weapon_test
    tests/player_favorite_weapon_test.c
    src/game/playerstats_007770.c)
  target_compile_features(ge007_player_favorite_weapon_test PRIVATE c_std_11)
  target_compile_options(ge007_player_favorite_weapon_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_player_favorite_weapon_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_player_favorite_weapon
           COMMAND ge007_player_favorite_weapon_test)

  add_executable(ge007_explosion_buffer_init_test
    tests/explosion_buffer_init_test.c
    src/game/initexplosioncasing.c)
  target_compile_features(ge007_explosion_buffer_init_test PRIVATE c_std_11)
  target_compile_options(ge007_explosion_buffer_init_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_explosion_buffer_init_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_explosion_buffer_init
           COMMAND ge007_explosion_buffer_init_test)

  add_executable(ge007_window_piece_allocation_test
    tests/window_piece_allocation_test.c
    src/game/alloc_window_pieces.c)
  target_compile_features(ge007_window_piece_allocation_test PRIVATE c_std_11)
  target_compile_options(ge007_window_piece_allocation_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_window_piece_allocation_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_window_piece_allocation
           COMMAND ge007_window_piece_allocation_test)

  add_executable(ge007_player_sound_cleanup_test
    tests/player_sound_cleanup_test.c
    src/game/cleanplayersound.c)
  target_compile_features(ge007_player_sound_cleanup_test PRIVATE c_std_11)
  target_compile_options(ge007_player_sound_cleanup_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_player_sound_cleanup_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_player_sound_cleanup
           COMMAND ge007_player_sound_cleanup_test)

  add_executable(ge007_cheat_text_init_test
    tests/cheat_text_init_test.c
    src/game/initcheattext.c)
  target_compile_features(ge007_cheat_text_init_test PRIVATE c_std_11)
  target_compile_options(ge007_cheat_text_init_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_cheat_text_init_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_cheat_text_init
           COMMAND ge007_cheat_text_init_test)

  add_executable(ge007_object_cleanup_dispatch_test
    tests/object_cleanup_dispatch_test.c
    src/game/cleanup_objects.c)
  target_compile_features(ge007_object_cleanup_dispatch_test PRIVATE c_std_11)
  target_compile_options(ge007_object_cleanup_dispatch_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_object_cleanup_dispatch_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_object_cleanup_dispatch
           COMMAND ge007_object_cleanup_dispatch_test)

  add_executable(ge007_guard_initialization_test
    tests/guard_initialization_test.c
    src/game/initguards.c)
  target_compile_features(ge007_guard_initialization_test PRIVATE c_std_11)
  target_compile_options(ge007_guard_initialization_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_guard_initialization_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_guard_initialization
           COMMAND ge007_guard_initialization_test)

  add_executable(ge007_player_gait_object_init_test
    tests/player_gait_object_init_test.c
    src/game/initplayergaitobject.c)
  target_compile_features(ge007_player_gait_object_init_test PRIVATE c_std_11)
  target_compile_options(ge007_player_gait_object_init_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_player_gait_object_init_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_player_gait_object_init
           COMMAND ge007_player_gait_object_init_test)

  add_executable(ge007_model_slot_init_test
    tests/model_slot_init_test.c
    src/game/initunk_005520.c)
  target_compile_features(ge007_model_slot_init_test PRIVATE c_std_11)
  target_compile_options(ge007_model_slot_init_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_model_slot_init_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_model_slot_init
           COMMAND ge007_model_slot_init_test)

  add_executable(ge007_initialization_allocations_test
    tests/initialization_allocations_test.c
    src/game/initmttex.c
    src/game/initintromatrices.c)
  target_compile_features(ge007_initialization_allocations_test PRIVATE c_std_11)
  target_compile_options(ge007_initialization_allocations_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_initialization_allocations_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_initialization_allocations
           COMMAND ge007_initialization_allocations_test)

  add_executable(ge007_game_data_initialization_test
    tests/game_data_initialization_test.c
    src/game/initgamedata.c)
  target_compile_features(ge007_game_data_initialization_test PRIVATE c_std_11)
  target_compile_options(ge007_game_data_initialization_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_game_data_initialization_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_game_data_initialization
           COMMAND ge007_game_data_initialization_test)

  add_executable(ge007_ejected_cartridges_init_test
    tests/ejected_cartridges_init_test.c
    src/game/ejectedcartridges.c)
  target_compile_features(ge007_ejected_cartridges_init_test PRIVATE c_std_11)
  target_compile_options(ge007_ejected_cartridges_init_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_ejected_cartridges_init_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_ejected_cartridges_init
           COMMAND ge007_ejected_cartridges_init_test)

  add_executable(ge007_game_debug_test
    tests/game_debug_test.c
    src/game/game_debug.c)
  target_compile_features(ge007_game_debug_test PRIVATE c_std_11)
  target_compile_options(ge007_game_debug_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_game_debug_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_game_debug
           COMMAND ge007_game_debug_test)

  add_executable(ge007_frame_timing_test
    tests/frame_timing_test.c
    src/game/frametiming.c)
  target_compile_features(ge007_frame_timing_test PRIVATE c_std_11)
  target_compile_options(ge007_frame_timing_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_frame_timing_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_frame_timing
           COMMAND ge007_frame_timing_test)
  set_tests_properties(ge007_frame_timing
    PROPERTIES ENVIRONMENT "GE_D193=1")

  add_executable(ge007_decompress_adapter_test
    tests/decompress_adapter_test.c
    src/game/decompress.c)
  target_compile_features(ge007_decompress_adapter_test PRIVATE c_std_11)
  target_compile_options(ge007_decompress_adapter_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_decompress_adapter_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_decompress_adapter
           COMMAND ge007_decompress_adapter_test)

  add_executable(ge007_menu_initialization_test
    tests/menu_initialization_test.c
    src/game/initmenus.c)
  target_compile_features(ge007_menu_initialization_test PRIVATE c_std_11)
  target_compile_options(ge007_menu_initialization_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_menu_initialization_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_menu_initialization
           COMMAND ge007_menu_initialization_test)

  add_executable(ge007_viewport_depth_buffer_test
    tests/viewport_depth_buffer_test.c
    src/game/viewport.c)
  target_compile_features(ge007_viewport_depth_buffer_test PRIVATE c_std_11)
  target_compile_options(ge007_viewport_depth_buffer_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_viewport_depth_buffer_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_viewport_depth_buffer
           COMMAND ge007_viewport_depth_buffer_test)

  add_executable(ge007_object_initialization_test
    tests/object_initialization_test.c
    src/game/initobjects.c)
  target_compile_features(ge007_object_initialization_test PRIVATE c_std_11)
  target_compile_options(ge007_object_initialization_test PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(ge007_object_initialization_test PRIVATE
    -Wl,--gc-sections)
  add_test(NAME ge007_object_initialization
           COMMAND ge007_object_initialization_test)

  set_tests_properties(
    ge007_dynamic_lighting
    ge007_advanced_lighting_policy
    ge007_shadow_mapping_policy
    ge007_explosion_effect
    ge007_vector_text_frame
    ge007_vector_text_font_assets
    ge007_vector_text_route
    ge007_bump_mapping_policy
    ge007_textured_vertex_stream_policy
    ge007_fast3d_light_state
    ge007_fast3d_normal_policy
    ge007_bumpmap_generator_policy
    ge007_bumpmap_decode
    ge007_bumpmap_metadata
    ge007_bumpmap_runtime
    ge007_pdtex_stream
    ge007_depth_of_field_policy
    ge007_gfx_wayland_surface_policy
    ge007_world_lighting_integration
    ge007_gu_matrix
    ge007_matrixmath
    ge007_game_math
    ge007_geometry
    ge007_rle
    ge007_line_tri_intersect
    ge007_blood_decrypt
    ge007_blood_animation
    ge007_rzdecomp
    ge007_zlib_inflate
    ge007_crc
    ge007_objective_status2
    ge007_objective
    ge007_loadobjectmodel
    ge007_cctv_orientation
    ge007_bondview_r_coop_spawn
    ge007_record_damage_kills
    ge007_coop
    ge007_fake_controller
    ge007_controller_mapping
    ge007_input_mapping
    ge007_sight_policy
    ge007_viewport_policy
    ge007_ambient_lighting_regression
    ge007_water_time_policy
    ge007_water_heightfield_policy
    ge007_water_heightfield_integration_contract
    ge007_water_cpu_render_harness
    ge007_model_render_pixel_oracle
    PROPERTIES LABELS ge007-unit)
  set_tests_properties(
    ge007_defect_contract
    ge007_water_render_policy
    ge007_water_render_reference_math
    ge007_water_render_pixel_oracle
    ge007_water_render_bdd_contract
    ge007_world_water_surface_contract
    ge007_muzzle_flash_contract
    ge007_game_utility_edge
    ge007_bondhead
    ge007_music_tracks
    ge007_bgfog
    ge007_bg_apply_lut
    ge007_bg_room_trans
    ge007_file_state
    ge007_file2_handler
    ge007_ob_resource
    ge007_objecthandler2
    ge007_model_pointer_promotion
    ge007_model_animation
    ge007_tex_policy
    ge007_image_texture
    ge007_image_bank
    ge007_chr_b
    ge007_initanitable
    ge007_deb_loadallmodels
    ge007_init_bond_data_defaults
    ge007_init_actor_propstuff
    ge007_init_bond_data
    ge007_unk_092e50
    ge007_glass
    ge007_glass2
    ge007_debugmenu_navigation
    ge007_bondinv
    ge007_textrelated_policy
    ge007_sky_policy
    ge007_lightfixture
    ge007_vtxstore
    ge007_debug_camera
    ge007_title3_display_list
    ge007_title2_display_list
    ge007_indy_comms
    ge007_bondwalk2_display
    ge007_speedgraphrenderer
    ge007_mp_music
    ge007_dyn_allocator
    ge007_radar
    ge007_player_state
    ge007_mp_menu_policy
    ge007_ramrom_resume
    ge007_options_state
    ge007_gun_sight_render
    ge007_language_state
    ge007_rsp_task
    ge007_othermodemicrocode
    ge007_model_hit_freelist
    ge007_game_initialization_edge
    ge007_init_path_table
    ge007_init_path_table_links
    ge007_init_weapon_animation_groups
    ge007_init_item_slots
    ge007_sound_cleanup
    ge007_explosion_cleanup
    ge007_guard_cleanup
    ge007_player_favorite_weapon
    ge007_explosion_buffer_init
    ge007_window_piece_allocation
    ge007_player_sound_cleanup
    ge007_cheat_text_init
    ge007_object_cleanup_dispatch
    ge007_guard_initialization
    ge007_player_gait_object_init
    ge007_model_slot_init
    ge007_initialization_allocations
    ge007_game_data_initialization
    ge007_ejected_cartridges_init
    ge007_game_debug
    ge007_frame_timing
    ge007_decompress_adapter
    ge007_menu_initialization
    ge007_viewport_depth_buffer
    ge007_object_initialization
    ge007_options_overlay
    PROPERTIES LABELS ge007-unit)

  set_tests_properties(ge007_shadow_mapping_contract
    PROPERTIES LABELS ge007-contract)

  # These are deliberately red-first contracts for live water features that
  # still need renderer/fixture implementation. Keep them visible and
  # runnable, but do not let an intentional TDD red state poison the green
  # unit/coverage gate.
  set_tests_properties(
    ge007_water_live_shader_contract
    ge007_water_render_missed_contract
    ge007_water_render_quality_contract
    PROPERTIES LABELS ge007-red)

  if(GE007_ENABLE_COVERAGE)
    set(_GE007_UNIT_TEST_TARGETS
      ge007_dynamic_lighting_test
      ge007_advanced_lighting_policy_test
      ge007_shadow_mapping_policy_test
      ge007_explosion_effect_test
      ge007_vector_text_frame_test
      ge007_vector_text_font_assets_test
      ge007_vector_text_route_test
      ge007_bump_mapping_policy_test
      ge007_textured_vertex_stream_policy_test
      ge007_fast3d_light_state_test
      ge007_fast3d_normal_policy_test
      ge007_bumpmap_generator_policy_test
      ge007_bumpmap_decode_test
      ge007_bumpmap_metadata_test
      ge007_bumpmap_runtime_test
      ge007_pdtex_stream_test
      ge007_depth_of_field_policy_test
      ge007_gfx_wayland_surface_policy_test
      ge007_ascension_postfx_shader_test
      ge007_world_lighting_integration_test
      ge007_gu_matrix_test
      ge007_matrixmath_test
      ge007_game_math_test
      ge007_geometry_test
      ge007_rle_test
      ge007_line_tri_intersect_test
      ge007_blood_decrypt_test
      ge007_blood_animation_test
      ge007_rzdecomp_test
      ge007_zlib_inflate_test
      ge007_crc_test
      ge007_objective_status2_test
      ge007_objective_test
      ge007_loadobjectmodel_test
      ge007_cctv_orientation_test
      ge007_bondview_r_coop_spawn_test
      ge007_record_damage_kills_test
      ge007_coop_test
      ge007_fake_controller_test
      ge007_controller_mapping_test
      ge007_input_mapping_test
      ge007_sight_policy_test
      ge007_viewport_policy_test
      ge007_defect_contract_test
    ge007_ambient_lighting_regression_test
      ge007_water_heightfield_policy_test
      ge007_water_heightfield_integration_contract_test
    ge007_water_render_policy_test
    ge007_water_render_reference_math_test
      ge007_water_time_policy_test
      ge007_water_cpu_render_harness_test
      ge007_model_render_pixel_oracle_test
      ge007_water_render_pixel_oracle_test
      ge007_water_render_bdd_contract_test
      ge007_world_water_surface_contract_test
      ge007_muzzle_flash_contract_test
      ge007_game_utility_edge_test
      ge007_bondhead_test
      ge007_music_tracks_test
      ge007_bgfog_test
      ge007_bg_apply_lut_test
      ge007_bg_room_trans_test
      ge007_file_state_test
      ge007_file2_handler_test
      ge007_ob_resource_test
      ge007_objecthandler2_test
      ge007_model_pointer_promotion_test
      ge007_model_animation_test
      ge007_tex_policy_test
      ge007_image_texture_test
      ge007_image_bank_test
      ge007_chr_b_test
      ge007_initanitable_test
      ge007_deb_loadallmodels_test
      ge007_init_bond_data_defaults_test
      ge007_init_actor_propstuff_test
      ge007_init_bond_data_test
      ge007_unk_092e50_test
      ge007_glass_test
      ge007_glass2_test
      ge007_debugmenu_navigation_test
      ge007_bondinv_test
      ge007_textrelated_policy_test
      ge007_sky_policy_test
      ge007_lightfixture_test
      ge007_vtxstore_test
      ge007_debug_camera_test
      ge007_title3_display_list_test
      ge007_title2_display_list_test
      ge007_indy_comms_test
      ge007_bondwalk2_display_test
      ge007_speedgraphrenderer_test
      ge007_mp_music_test
      ge007_dyn_allocator_test
      ge007_radar_test
      ge007_player_state_test
      ge007_mp_menu_policy_test
      ge007_ramrom_resume_test
      ge007_options_state_test
      ge007_gun_sight_render_test
      ge007_options_overlay_test
      ge007_language_state_test
      ge007_rsp_task_test
      ge007_othermodemicrocode_test
      ge007_model_hit_freelist_test
      ge007_game_initialization_edge_test
      ge007_init_path_table_test
      ge007_init_path_table_links_test
      ge007_init_weapon_animation_groups_test
      ge007_init_item_slots_test
      ge007_sound_cleanup_test
      ge007_explosion_cleanup_test
      ge007_guard_cleanup_test
      ge007_player_favorite_weapon_test
      ge007_explosion_buffer_init_test
      ge007_window_piece_allocation_test
      ge007_player_sound_cleanup_test
      ge007_cheat_text_init_test
      ge007_object_cleanup_dispatch_test
      ge007_guard_initialization_test
      ge007_player_gait_object_init_test
      ge007_model_slot_init_test
      ge007_initialization_allocations_test
      ge007_game_data_initialization_test
      ge007_ejected_cartridges_init_test
      ge007_game_debug_test
      ge007_frame_timing_test
      ge007_decompress_adapter_test
      ge007_menu_initialization_test
      ge007_viewport_depth_buffer_test
      ge007_object_initialization_test)

    foreach(_GE007_TEST_TARGET IN LISTS _GE007_UNIT_TEST_TARGETS)
      target_compile_options(${_GE007_TEST_TARGET} PRIVATE --coverage -O0 -g)
      target_link_options(${_GE007_TEST_TARGET} PRIVATE --coverage)
    endforeach()

    file(GLOB _GE007_COVERAGE_REQUIRED_FILES CONFIGURE_DEPENDS
      "${CMAKE_SOURCE_DIR}/port/include/*_policy.h"
      "${CMAKE_SOURCE_DIR}/port/fast3d/*_policy.h")

    # dynamic_lighting.h predates the *_policy.h naming convention but is a
    # pure policy header with a dedicated exhaustive unit test.
    list(APPEND _GE007_COVERAGE_REQUIRED_FILES
      "${CMAKE_SOURCE_DIR}/port/fast3d/dynamic_lighting.h"
      "${CMAKE_SOURCE_DIR}/port/fast3d/fast3d_ambient_lighting.h"
      "${CMAKE_SOURCE_DIR}/port/fast3d/vector_text_layout.h"
      "${CMAKE_SOURCE_DIR}/tools/mktex/src/bumpmap_decode.c"
      "${CMAKE_SOURCE_DIR}/tools/mktex/src/bumpmap_metadata.c"
      "${CMAKE_SOURCE_DIR}/tools/mktex/src/bumpmap_policy.h"
      "${CMAKE_SOURCE_DIR}/src/game/objective_status.c")
    list(SORT _GE007_COVERAGE_REQUIRED_FILES)
    string(REPLACE ";" "|" _GE007_COVERAGE_REQUIRED_FILES_ARG
      "${_GE007_COVERAGE_REQUIRED_FILES}")
    set(_GE007_COVERAGE_GAME_UNIT_FILES
      "${CMAKE_SOURCE_DIR}/port/fast3d/bumpmap_runtime.cpp"
      "${CMAKE_SOURCE_DIR}/src/game/matrixmath.c"
      "${CMAKE_SOURCE_DIR}/src/game/matrixmath_misc.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_asinacos.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_asinfacosf.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_atan2f.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_ceil.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_floor.c"
      "${CMAKE_SOURCE_DIR}/src/game/math_unk_05A9E0.c"
      "${CMAKE_SOURCE_DIR}/src/game/quaternion.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/rotate.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/scale.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/translate.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/ortho.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/perspective.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/lookat.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/sins.c"
      "${CMAKE_SOURCE_DIR}/src/libultra/gu/coss.c"
      "${CMAKE_SOURCE_DIR}/src/game/stanintersection.c"
      "${CMAKE_SOURCE_DIR}/src/game/rle.c"
      "${CMAKE_SOURCE_DIR}/src/game/line_tri_intersect.c"
      "${CMAKE_SOURCE_DIR}/src/game/blood_decrypt.c"
      "${CMAKE_SOURCE_DIR}/src/game/blood_animation.c"
      "${CMAKE_SOURCE_DIR}/port/src/rzdecomp.c"
      "${CMAKE_SOURCE_DIR}/src/game/zlib.c"
      "${CMAKE_SOURCE_DIR}/src/game/crc.c"
      "${CMAKE_SOURCE_DIR}/src/game/objective_status2.c"
      "${CMAKE_SOURCE_DIR}/src/game/objective.c"
      "${CMAKE_SOURCE_DIR}/src/game/loadobjectmodel.c"
      "${CMAKE_SOURCE_DIR}/src/game/cctv.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanup_alarms.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanup_objectives.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanwindowpieces.c"
      "${CMAKE_SOURCE_DIR}/src/game/compiletime.c"
      "${CMAKE_SOURCE_DIR}/src/game/initnull_0009D0.c"
      "${CMAKE_SOURCE_DIR}/src/game/initnull_000BC0.c"
      "${CMAKE_SOURCE_DIR}/src/game/mp_weapon.c"
      "${CMAKE_SOURCE_DIR}/src/game/null_007970.c"
      "${CMAKE_SOURCE_DIR}/src/game/padhalllv.c"
      "${CMAKE_SOURCE_DIR}/src/game/bondhead.c"
      "${CMAKE_SOURCE_DIR}/src/game/music_0D2720.c"
      "${CMAKE_SOURCE_DIR}/src/game/bgfog.c"
      "${CMAKE_SOURCE_DIR}/src/game/sky.c"
      "${CMAKE_SOURCE_DIR}/src/game/tex.c"
      "${CMAKE_SOURCE_DIR}/src/game/image_bank.c"
      "${CMAKE_SOURCE_DIR}/src/game/chr_b.c"
      "${CMAKE_SOURCE_DIR}/src/game/initanitable.c"
      "${CMAKE_SOURCE_DIR}/src/game/deb_loadallmodels.c"
      "${CMAKE_SOURCE_DIR}/src/game/initBondDATAdefaults.c"
      "${CMAKE_SOURCE_DIR}/src/game/initactorpropstuff.c"
      "${CMAKE_SOURCE_DIR}/src/game/initBondDATA.c"
      "${CMAKE_SOURCE_DIR}/src/game/unk_092E50.c"
      "${CMAKE_SOURCE_DIR}/src/game/glass.c"
      "${CMAKE_SOURCE_DIR}/src/game/glass2.c"
      "${CMAKE_SOURCE_DIR}/src/game/debugmenu_08FE00.c"
      "${CMAKE_SOURCE_DIR}/src/game/bondinv.c"
      "${CMAKE_SOURCE_DIR}/src/game/othermodemicrocode.c"
      "${CMAKE_SOURCE_DIR}/src/game/bgapply.c"
      "${CMAKE_SOURCE_DIR}/src/game/bgroomtrans.c"
      "${CMAKE_SOURCE_DIR}/src/game/file.c"
      "${CMAKE_SOURCE_DIR}/src/game/file2.c"
      "${CMAKE_SOURCE_DIR}/src/game/ob.c"
      "${CMAKE_SOURCE_DIR}/src/game/objecthandler_2.c"
      "${CMAKE_SOURCE_DIR}/src/game/lightfixture.c"
      "${CMAKE_SOURCE_DIR}/src/game/vtxstore.c"
      "${CMAKE_SOURCE_DIR}/src/game/debug_camera.c"
      "${CMAKE_SOURCE_DIR}/src/game/title3.c"
      "${CMAKE_SOURCE_DIR}/src/game/title2.c"
      "${CMAKE_SOURCE_DIR}/src/game/indy_comms.c"
      "${CMAKE_SOURCE_DIR}/src/game/bondwalk2.c"
      "${CMAKE_SOURCE_DIR}/src/game/speedgraphrenderer.c"
      "${CMAKE_SOURCE_DIR}/src/game/mp_music.c"
      "${CMAKE_SOURCE_DIR}/src/game/dyn.c"
      "${CMAKE_SOURCE_DIR}/src/game/radar.c"
      "${CMAKE_SOURCE_DIR}/src/game/player.c"
      "${CMAKE_SOURCE_DIR}/src/game/language.c"
      "${CMAKE_SOURCE_DIR}/src/game/rsp.c"
      "${CMAKE_SOURCE_DIR}/src/game/setguscale.c"
      "${CMAKE_SOURCE_DIR}/src/game/initunk_005450.c"
      "${CMAKE_SOURCE_DIR}/src/game/initimages.c"
      "${CMAKE_SOURCE_DIR}/src/game/initunk_0009E0.c"
      "${CMAKE_SOURCE_DIR}/src/game/initunk_007290.c"
      "${CMAKE_SOURCE_DIR}/src/game/initpathtablesomething.c"
      "${CMAKE_SOURCE_DIR}/src/game/initpathtablelinks.c"
      "${CMAKE_SOURCE_DIR}/src/game/initweaponanigroups.c"
      "${CMAKE_SOURCE_DIR}/src/game/inititemslots.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanup_object_sounds.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanupSFXRelated.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanexplosions.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanup_guard_data.c"
      "${CMAKE_SOURCE_DIR}/src/game/playerstats_007770.c"
      "${CMAKE_SOURCE_DIR}/src/game/initexplosioncasing.c"
      "${CMAKE_SOURCE_DIR}/src/game/alloc_window_pieces.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanplayersound.c"
      "${CMAKE_SOURCE_DIR}/src/game/initcheattext.c"
      "${CMAKE_SOURCE_DIR}/src/game/cleanup_objects.c"
      "${CMAKE_SOURCE_DIR}/src/game/initguards.c"
      "${CMAKE_SOURCE_DIR}/src/game/initplayergaitobject.c"
      "${CMAKE_SOURCE_DIR}/src/game/initunk_005520.c"
      "${CMAKE_SOURCE_DIR}/src/game/initmttex.c"
      "${CMAKE_SOURCE_DIR}/src/game/initintromatrices.c"
      "${CMAKE_SOURCE_DIR}/src/game/initgamedata.c"
      "${CMAKE_SOURCE_DIR}/src/game/ejectedcartridges.c"
      "${CMAKE_SOURCE_DIR}/src/game/game_debug.c"
      "${CMAKE_SOURCE_DIR}/src/game/frametiming.c"
      "${CMAKE_SOURCE_DIR}/src/game/ramromreplay.c"
      "${CMAKE_SOURCE_DIR}/src/game/decompress.c"
      "${CMAKE_SOURCE_DIR}/src/game/initmenus.c"
      "${CMAKE_SOURCE_DIR}/src/game/viewport.c"
      "${CMAKE_SOURCE_DIR}/src/game/initobjects.c")
    string(REPLACE ";" "|" _GE007_COVERAGE_GAME_UNIT_FILES_ARG
      "${_GE007_COVERAGE_GAME_UNIT_FILES}")

    add_custom_target(ge007_coverage
      COMMAND "${CMAKE_COMMAND}"
        "-DGE007_COVERAGE_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DGE007_COVERAGE_CTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        "-DGE007_COVERAGE_GCOV=${GE007_GCOV_EXECUTABLE}"
        "-DGE007_COVERAGE_REQUIRED_FILES=${_GE007_COVERAGE_REQUIRED_FILES_ARG}"
        "-DGE007_COVERAGE_MIN_LINES=${GE007_COVERAGE_MIN_LINES}"
        "-DGE007_COVERAGE_MIN_BRANCHES=${GE007_COVERAGE_MIN_BRANCHES}"
        "-DGE007_COVERAGE_ADDITIONAL_FILES=${_GE007_COVERAGE_GAME_UNIT_FILES_ARG}"
        "-DGE007_COVERAGE_GAME_SOURCE_DIR=${CMAKE_SOURCE_DIR}/src/game"
        "-DGE007_COVERAGE_ADDITIONAL_MIN_LINES=80"
        "-DGE007_COVERAGE_ADDITIONAL_MIN_BRANCHES=80"
        -P "${CMAKE_SOURCE_DIR}/cmake/coverage_gate.cmake"
      DEPENDS ${_GE007_UNIT_TEST_TARGETS}
      USES_TERMINAL
      VERBATIM)
  endif()

  add_test(NAME ge007_pc_bdd
           COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_pc_bdd.sh")
  set_tests_properties(ge007_pc_bdd
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      SKIP_RETURN_CODE 77)

  add_test(NAME ge007_wayland_smoke
           COMMAND bash "${CMAKE_SOURCE_DIR}/tests/run_wayland_smoke.sh"
                   "$<TARGET_FILE:ge007>")
  set_tests_properties(ge007_wayland_smoke
    PROPERTIES
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      SKIP_RETURN_CODE 77)

  add_executable(ge007_wayland_sdl_swap_probe
    tests/wayland_sdl_swap_probe.cpp
    port/fast3d/glad/glad.c)
  target_include_directories(ge007_wayland_sdl_swap_probe PRIVATE
    "${CMAKE_SOURCE_DIR}/port/fast3d")
  target_compile_features(ge007_wayland_sdl_swap_probe PRIVATE cxx_std_17)
  target_link_libraries(ge007_wayland_sdl_swap_probe PRIVATE
    "${SDL2_LIBRARY}" "${GL_LIBRARY}")
  add_test(NAME ge007_wayland_sdl_swap_probe
           COMMAND ge007_wayland_sdl_swap_probe)
  set_tests_properties(ge007_wayland_sdl_swap_probe
    PROPERTIES
      LABELS "ge007-wayland;ge007-control"
      ENVIRONMENT "GE_WAYLAND_PROBE_RESIZE=1"
      SKIP_RETURN_CODE 77
      TIMEOUT 8)
endif()

message(STATUS "Output binary: ${BIN_NAME}")
