# Port rendering enhancements

The PC renderer keeps the N64 display-list and game-state authority intact,
then adds opt-in presentation features at the port boundary.

## Render modes

`[Video] RenderMode` is an integer setting:

| Value | Mode | Behavior |
| ---: | --- | --- |
| 0 | `ORIGINAL` | Original bitmap UI and N64 lighting path. |
| 1 | `ENHANCED` | Vector port UI plus transient world lighting. |
| 2 | `REMASTER` | Same current enhanced path, reserved for additional remaster presentation work. |

The F10 overlay exposes the same values as `ORIGINAL`, `ENHANCED`, and
`REMASTER`. `Video.DynamicLighting` remains as a compatibility switch for the
old camera helper light; enhanced and remaster modes enable the shared world
lighting registry automatically.

## World lighting

`port/src/world_lighting.c` is a read-only adapter from existing game-owned
explosion and muzzle-flash state to the renderer. It converts world positions
through the active camera matrix and submits transient point lights through
`gfx_world_lighting_api.h`.

The renderer owns one fixed-capacity `GeDynamicLightRegistry`. Its centralized
policy handles point/spot falloff, color, validation, and deterministic
priority replacement when the 16-light frame budget is full. Effect profiles
live in `world_lighting_policy.h`, so the adapter, renderer, and tests do not
carry separate radius/intensity rules. The current implementation is
CPU-side vertex lighting in the existing software RSP path; it is not global
illumination, shadow mapping, or per-pixel lighting yet.

This separation intentionally uses an Adapter boundary and a small Registry
policy: the game remains the source of effect state, while the renderer remains
the sole owner of transient lights. No game logic or N64 layout is changed.

## Vector UI font

Enhanced and remaster modes render the port F10 overlay through the optional
FreeType-backed vector path. The font source is kept as outlines and the
coverage atlas is regenerated at the larger mapped output-pixel scale whenever
the window scale changes; it is not a single 13-pixel bitmap stretched
forever. Glyph bounds are snapped to output pixels and the atlas uses nearest
sampling so FreeType's coverage values are not blurred by a second
magnification pass.
The lookup order is:

1. `GE_UI_FONT`, when set;
2. `data/fonts/LibreFranklin-Regular.ttf`;
3. `data/fonts/LibreFranklin-SemiBold.ttf`;
4. the platform's Liberation Sans or Arial fallback.

Libre Franklin is not bundled with the repository. Install a licensed font
file at one of the documented paths, or point `GE_UI_FONT` at an installed
font. If FreeType, the font, or the OpenGL shader setup is unavailable, the
original bitmap font is used automatically.

The logical-to-window coordinate conversion is shared by the vector renderer
and its tests. Commands are queued while the port overlay builds its display
list and drawn after the software RSP flushes, so OpenGL state does not enter
the game-owned display-list path.

## Final presentation pass

The port also carries the compatible part of Ascension's graphics-final work:
an optional edge-adaptive sharpening pass runs on the composited back buffer
immediately before SDL presents it. It samples a small neighbourhood, reduces
gain at hard contrast boundaries, and clamps the result to limit halos. The
pass is fail-open and restores the shared Fast3D GL state through one RAII
guard, including the implicit VBO/VAO bindings relied on by the software RSP.

`Video.PostFX=0` or `GE_POSTFX=0` disables it; `Video.Sharpen` and
`GE_SHARPEN=0..100` control strength. The default is a conservative 35%.
The source remains presentation-only: it does not alter display lists, game
state, depth data, or timing.

## Verification

The focused `ge007_dynamic_lighting` test covers colored point lights, spot
cones, registry overflow priority, malformed effect inputs, shared muzzle and
explosion profiles, vector UI coordinate mapping, output-scale selection, and
PostFX strength policy. The full CTest suite also runs the PC BDD smoke
scenarios; an enhanced OpenGL smoke run validates vector-font and PostFX shader
initialization through 300 rendered frames.
