---
title: Building
description: Full build and asset-extraction guide for the GoldenEye 007 PC port, covering Windows (MSYS2) and Linux.
---

## Building the PC port

Stages:

1. **Extract assets from your ROM** (§2): a one-time step using the
   decompilation's own toolchain to pull levels, models, textures, fonts and
   music into `assets/`. *(Only needed to regenerate the committed data files;
   a plain `git clone` already has what the PC build compiles.)*
2. **Build the port** (§3): a CMake build compiling the game sources plus the
   `port/` layer into a native executable. Needs no ROM.
3. **Generate the PC asset sidecars** (§4): two pure-Python converters turn
   ROM model / stage data into the PC-layout `data/pcmodels-*` / `data/pccg-*`
   files the port loads at runtime. **Required to run.**

You need a GoldenEye 007 N64 ROM you legally own (`.z64`, big-endian). See the
[Requirements table in the README](https://github.com/jkdansereau/goldeneye-pc-port#requirements)
for accepted versions and hashes.

---

## 1. Dependencies

### Port build

> **The Windows (MSYS2 MINGW64) path is the primary one.** The Linux build is
> compiled by CI on every push and ships in the release bundle (also
> playtested on Steam Deck hardware); the macOS column is best-effort
> guidance and has never been built or run there. Expect to fix build breaks
> yourself on untested platforms.

| Need | Windows (MSYS2 MINGW64) | Debian/Ubuntu | macOS (Homebrew) |
|------|------------------------|---------------|------------------|
| toolchain | `mingw-w64-x86_64-toolchain` | `build-essential` | Xcode CLT / `gcc` |
| CMake | `mingw-w64-x86_64-cmake` | `cmake` | `cmake` |
| SDL2 | `mingw-w64-x86_64-SDL2` | `libsdl2-dev` | `sdl2` |
| zlib | `mingw-w64-x86_64-zlib` | `zlib1g-dev` | `zlib` |
| OpenGL | (in the toolchain) | `libgl1-mesa-dev` | (system) |
| FreeType 2 (optional vector UI) | `mingw-w64-x86_64-freetype` | `libfreetype6-dev` | `freetype` |
| Python 3 | `mingw-w64-x86_64-python` | `python3` | `python3` |

### Asset extraction (decompilation toolchain)

The extraction scripts need `binutils-mips-linux-gnu` (or an equivalent MIPS
binutils), `make`, `git`, and `python3`. They build a small host-compiled
`tools/extractor` and slice blobs straight out of the ROM; **no IDO / IRIX
toolchain is involved in extraction or in the PC build.** (The IDO toolchain is
only needed to build the N64 ROM itself, and its proprietary SGI binaries are
not distributed here; see [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) "Recompile IDO".)
On Windows this is easiest under WSL or a Linux VM. Full details and
alternatives (Docker) are in [`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md).

---

## 2. Extract assets

Put your **US** ROM at the repository root as `baserom.u.z64` (this name is
required by the extraction scripts; it is git-ignored and never committed),
then:

```sh
./scripts/extract_baserom.u.sh
```

For PAL or JP, additionally place `baserom.e.z64` / `baserom.j.z64` at the root
and run:

```sh
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.e.sh   # PAL
./scripts/extract_baserom.u.sh && ./scripts/extract_diff.j.sh   # JP
```

(US extraction is a prerequisite for the others.)

This populates `assets/` with the generated `.bin` blobs the build needs. See
[`SetupGuide.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/SetupGuide.md) for the in-depth build/asset pipeline.

---

## 3. Build

```sh
./build-pc.sh ntsc-final        # or: pal-final / jpn-final
```

which is equivalent to:

```sh
cmake -S . -B build-pc -DROMID=ntsc-final
cmake --build build-pc -j
```

### Unit coverage gate

The deterministic unit gate does not launch the game and does not need a ROM or
runtime sidecars. It instruments the deterministic unit executables with
GCC/gcov, requires 100% line and branch coverage for the policy headers and
the objective-status module, and enforces an 80% floor for the selected
stateful game slices. The former defect-contract suites now run as green
regressions:

```sh
cmake -S . -B build-coverage -DROMID=ntsc-final \
  -DBUILD_TESTING=ON \
  -DGE007_ENABLE_COVERAGE=ON \
  -DGE007_COVERAGE_MIN_LINES=100 \
  -DGE007_COVERAGE_MIN_BRANCHES=100
cmake --build build-coverage --target ge007_coverage -j
```

`GE007_ENABLE_COVERAGE` is GCC/gcov-only. The normal game target and the
runtime BDD test remain separate because they require an available display,
ROM, and generated sidecars. The gate includes a deterministic slice of the
real `src/game` sources with an 80% line/branch floor. The current slice reports
100% lines / branches for the vector-text frame hand-off policy and 94.12%
lines / 100% branches for the PC RZ/raw-deflate adapter. It also reports
at least 95% lines / 100% branches across the tested renderer-facing libultra
matrix constructors and N64 sine tables.
99.70% lines / 100% branches for `matrixmath.c`, 100% lines for
`matrixmath_misc.c`, 100% lines / branches for the fixed-point trig,
floor/ceil, and bend-scale helpers, and 97.09% lines / 100% branches for
`quaternion.c`. The two remaining uncovered quaternion branches are legacy
defensive paths that are unreachable with the function's normalized scalar
convention; they are documented in
[`docs/dev/UNIT-DEFECT-LOG.md`](dev/UNIT-DEFECT-LOG.md).
The collision geometry slice in `stanintersection.c` reports 96.25% lines /
100% branches. Its tests cover segment fractions, radius overlap, endpoint
fallbacks, parallel edges, and the no-direction sentinel.
The RLE image decoders in `rle.c` are covered at 100% lines / branches for
8-bit, RGBA5551, and RGBA32 run streams.
The ray/triangle collision helper in `line_tri_intersect.c` reports 98.28%
lines / 100% branches, including inside/outside hits, behind-ray rejection,
parallel planes, and all three barycentric projection fallbacks.
The blood-frame decoder and image filters in `blood_decrypt.c` report 100%
lines / branches, including packed runs, nibble expansion, neighbourhood
averaging, transpose, and both four-neighbour passes.
The save-slot CRC generator in `crc.c` reports 100% lines / branches, with a
golden save payload and the N64-compatible PRNG stream checked by
`ge007_crc_test`.
The objective-status motion helpers in `objective_status2.c` report 100% lines
/ branches using deterministic PRNG and matrix fakes at their external
boundaries.
The objective registration and tag-list slice in `objective.c` reports 100%
lines / branches, including title/non-title reset behavior and the host-layout
tagged-object active-bit boundary.
The objective-status module reports 100% lines / branches, including invalid
index rejection, every criterion status path, split pad-list paths, HUD status
messages, and photograph viewport boundary cases.
The prop-definition stream and setup lookup slice in `loadobjectmodel.c` reports
83.45% lines / 90.65% branches, including all converter strides, widened-record
walks, model reuse filters, room registration branches, and model-load reuse.
It is held to the same 80% additional-source coverage gate.
The ROM-derived bump-map sidecar runtime is also built and executed by this
target, with its manifest/path/index/PNG-load behavior held to the same 80%
additional-source floor. Its GLSL presentation work is checked separately by
the shader contract/compiler and headless GPU tests described in the DoF
development notes.
The CCTV setup-pad selector is also covered by a ROM-free fixture for all four
Bunker CCTV records; its red baseline caught the placement-pad/look-pad mix-up.
The multiplayer radar renderer is covered with deterministic viewport, player
count, suppression, team-color, dead-player, distance-clamp, and split-player
fixtures; its player-count guard uses the shared `MIN_MULTIPLAYER` and
`MAX_MULTIPLAYER` bounds.
The player-state slice in `player.c` reports 100% lines / branches, including
single-player and co-op initialization, reset paths, sparse player-slot lookup,
viewport state, held-item mapping, shuffle helpers, and held-weapon side effects.
The language-state slice in `language.c` reports 94.65% lines / 91.84%
branches, including every stage-to-bank mapping, startup and on-demand bank
loading, ROM-bank fixup guards, string-slot validation, Japanese glyph-cache
hits and misses, TTL expiry, and the final single-slot boundary under
AddressSanitizer. The multiplayer-menu policy test covers fractional award
ranking, random tie breaks, navigation boundaries, and unpause state; the
focused test is intentionally a regression seam and does not claim whole-file
coverage for the large rendering-heavy `mpmenu.c` translation unit.
`ge007_game_coverage` also builds the full executable with counters, but useful
whole-game percentages require a runtime or mocked game-loop harness to drive
those counters.

The coverage gate now prints an explicit translation-unit inventory. At this
revision, 71 of the 135 `src/game/*.c` files (52% by translation-unit count)
are in the deterministic unit slice. That is an inventory percentage, not a
claim that 51% of executable lines are covered: each selected file is held to
an 80% line/branch floor, while the remaining stateful translation units still
need fakes or integration fixtures. The full executable is instrumented by
`ge007_game_coverage`, but its counters are not treated as coverage evidence
until a deterministic game-loop harness drives them.

To only build that instrumented executable without launching it:

```sh
cmake --build build-coverage --target ge007_game_coverage -j
```

FreeType is optional. When it is available, `Video.RenderMode=1` or `2`
enables the port's vector UI path; without it the original bitmap UI remains
available. See [the rendering enhancement notes](dev/RENDERING-ENHANCEMENTS.md)
for the Libre Franklin lookup and lighting behavior.

For PAL/JP you must first generate that region's ROM-asset symbol file
(the US one is committed):

```sh
python3 scripts/gen_romassets.py e     # PAL   -> port/src/romassets_e.s
python3 scripts/gen_romassets.py j     # JP    -> port/src/romassets_j.s
```

The executable lands at `build-pc/ge007.x86_64` (`.exe` on Windows). PAL/JP
builds are named `ge007.pal-final.x86_64` / `ge007.jpn-final.x86_64`.

---

## Offline bump maps

To generate height and tangent-space normal maps from your legally owned ROM,
see the offline bump-map pipeline in docs/dev/BUMPMAP-PIPELINE.md. It uses the
existing ROM texture decoder and emits local PNG pairs plus a traceable
manifest; it is independent of the game launch and sidecar-generation steps.

---

## 4. Generate the PC asset sidecars (required to run)

The port does **not** read model geometry, stage bg/stan data, or per-level
setup data from the raw ROM at runtime; it reads them from PC-layout *sidecar*
files under `data/`, produced offline by three converters. **Without them the
game shows the intro logos and then crashes** in
`modelPromoteNodeOffsetsToPointers` (finding D179) or on the first level load
(missing stage setup).

Put your ROM in `data/` first (same file the game runs from):

```sh
mkdir -p data
cp /path/to/your/rom.z64 data/ge007.ntsc-final.z64     # or pal-final / jpn-final
```

Then run all three emit passes for that region, **in this order** (d88 appends
to d69's output):

```sh
python3 tools_pc/d43_emit.py ntsc-final          # -> data/pcmodels-ntsc-final/{pcmodels.bin,manifest.csv}  (~1.3 MB)
python3 tools_pc/d69_emit.py ntsc-final          # -> data/pccg-ntsc-final/{pccg.bin,manifest.csv}          (bg + stan)
python3 tools_pc/d88_emit.py ntsc-final --regen  #    appends the 21 per-level Usetup*Z stage-setup files -> ~3.6 MB
```

**PAL / JP note:** sidecar generation for these regions is currently broken at
the source-data level (finding D258); use an NTSC-U ROM until issue #85 lands.

These are **pure-stdlib Python 3** (no MIPS toolchain, independent of the
step-2 asset extraction) and read only the ROM plus files already committed to
the repo (`scripts/filelist.u.csv`, `assets/obseg/file_resource_table.inc.c`,
`assets/**/ModelFileHeader.inc.c`, the bg/stan `.inc.c`). Output is a
deterministic function of the ROM. Re-run after any change to `d43_emit.py` /
`d69_emit.py` / `d88_emit.py` or the model/bg converters (`d43_*`, `d69_*`,
`d88_propdefs.py`).

> The release bundle ships the same converter frozen as
> `prepare-assets/ge007-convert`; the game spawns it on first launch when the
> sidecars are missing, so end users never run it by hand. See the bundled
> `README.md`.

> `data/pcmodels-*/` and `data/pccg-*/` are gitignored ROM-derived game data;
> never commit or redistribute them.

---

## 5. Run

```sh
./build-pc/ge007.x86_64          # run from the repo root
```

The ROM in `data/` (from step 4) and the sidecars are both required at
runtime. `ge007.ini` is written under `data/` on first launch.

### Useful flags / env

| | |
|---|---|
| `-level_NN` | boot straight into a solo level (e.g. `-level_09` = Bunker 1) |
| `GE_PCDUMP="first-last:step"` | dump rendered frames as PPM (debugging) |

More diagnostic switches are cataloged in
[`dev/GE-ENV-PROBES.md`](https://github.com/jkdansereau/goldeneye-pc-port/blob/main/docs/dev/GE-ENV-PROBES.md).
