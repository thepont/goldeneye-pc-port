# Original Dam water fidelity review — 2026-09-29

## Scope

This review answers whether the old, pre-modern Dam water texture was modified.
It compares the same camera and room in the current renderer with the immediate
parent of `7324a9ce` (`feat(port): modernize graphics stack and water
rendering`). The comparison uses the PC `Original` graphics preset; it does not
claim to be a console-reference capture.

The deterministic camera is stored in
`tests/dam_original_water_camera.gdb`. It places the view at
`(4280.09, 428.009, -10700.2)`, angles it at `(180.0, 351.47)`, and selects
room 35 after 40 calls to `lvlViewMoveTick`.

## Result

![Pre-modern and current Original water at the same camera](../../../artifacts/screenshots/original-water-fidelity/pre-modern-vs-current-original.png)

The visible water did change. At the same camera, the pre-modern build at
`9507669f` renders dark teal water, while current `Original` at `1d345f92`
renders bright lavender/checkered water. The current appearance persists over
a complete sampled animation cycle:

![Current Original water animation samples](../../../artifacts/screenshots/original-water-fidelity/current-original-full-cycle.png)

This is a presentation change, not a replacement of the authored texture
asset. The transition is associated with the renderer and display-list state
handling introduced by `7324a9ce`.

## Findings

1. Dam's room water uses the two-layer CI8 setup
   `MipMap2C_Something2_Setup` in `src/game/unk_092E50.c`. It combines
   `TEXEL0` and `TEXEL1` using `PRIM_LOD_FRAC`.
2. Before `7324a9ce`, the PC build updated that fraction through
   `((u32 *)setup)[8]`. A PC `Gfx` is 16 bytes, so this N64-word index did not
   address the prim-color command. The fraction therefore remained at its
   initial value instead of animating as authored.
3. `7324a9ce` correctly redirected the write to `setup[4].words.w0` on PC and
   added `ge007_unk_092e50_test` coverage. Reverting that correction restores
   the old-looking output only by restoring a real 64-bit display-list ABI
   bug; it is not an acceptable fidelity fix.
4. The current renderer also derived tile extents with unsigned subtraction.
   An animated tile end below its start underflowed to a huge extent, and an
   authored `0..0` window could be mistaken for a literal one-texel sub-tile.
   A port-owned policy now rejects inverted rectangles and preserves full-image
   wrapping for the unset masked window. This was established with a failing
   link-level regression target before implementation.
5. Disabling `Video.WrapFix` does not restore the pre-modern colour. The
   matched current capture remains lavender, so the wrap defect is real but is
   not the primary colour regression.
6. Frigate/Surface sky-water and the D229 sky-water path are separate from
   this authored Dam room material.

## Evidence

- `artifacts/screenshots/original-water-fidelity/pre-modern-fixed-camera.png` —
  build `9507669f`, matched camera.
- `artifacts/screenshots/original-water-fidelity/current-original-fixed-camera.png`
  — build `1d345f92`, `Original`, `Video.WrapFix=1`.
- `artifacts/screenshots/original-water-fidelity/current-original-wrap-off-fixed-camera.png`
  — build `1d345f92`, `Original`, `Video.WrapFix=0`.
- `artifacts/screenshots/original-water-fidelity/current-original-full-cycle.png`
  — current frames 120 through 360 sampled every 40 frames.

The pre-modern build did not support the newer frame-exit/dump-directory
controls, so it was stopped after its deterministic frame capture. No supported
N64 emulator was available in this workspace, and no matched-camera console
capture is claimed.

## Decision

Keep the existing PC ABI correction and the new tile-wrap safety fix. Do not
add a legacy/static-LOD compatibility switch: that would duplicate incorrect
behavior and hide the unresolved renderer defect. The remaining work is to
trace the Fast3D two-cycle CI8 `TEXEL0`/`TEXEL1` interpolation with animated
`PRIM_LOD_FRAC` against an N64 reference, then correct that port-owned path.
