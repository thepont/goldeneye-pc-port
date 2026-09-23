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

## Water surface material

The original water fan remains the source of geometry in every mode. Original
mode keeps its CI8 colour path. Enhanced and remaster modes wrap the fan in a
display-list-scoped renderer material, capture its projected triangles, and
discard the first-pass fragments so the later compositor can read an opaque
scene behind the water. The compositor reconstructs a procedural normal from
a bounded wave height field, then adds projective refraction, view-dependent
Fresnel reflection, alpha compositing, and a restrained water tint. Remaster
increases those responses while keeping the same texture and command path.
Original mode does not select the enhanced water shader or composite variant.

The reconstructed normal combines the procedural wave gradient with four
independently scrolled samples of the normal texture and is transformed in the
captured view frame. The highlight is a water-material term, separate from the
transient world-light registry.

### Bump-mapping boundary

The Enhanced and Remaster composite shaders evaluate the same bounded scalar
wave height at left/right and down/up base UV neighbours, form finite
differences, and normalize the tangent-space vector
`(height_dx * strength, height_dy * strength, 1)`. The renderer remaps that
normal into the water surface frame before reflection/refraction. The pure
contract lives in `port/fast3d/bump_mapping_policy.h` and is exercised by
`tests/bump_mapping_policy_test.cpp`, including flat surfaces, gradient
direction, normalization, tier strength, non-finite inputs, negative strength,
and overflow. The composite trace identifies this route as
`bump=height-gradient`; Original mode remains on the legacy path.

[GPU Gems 2 Chapter 8](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-8-pixel-displacement-mapping-distance-functions)
describes distance mapping—a displacement technique that requires a distance
map and tangent-space ray marching. This change uses the chapter's
tangent-frame/normal-lighting boundary where it fits the existing renderer,
but deliberately does not claim to implement distance-map displacement,
silhouette change, or occlusion between height features.

The water fan still renders before the opaque world, preserving the original
sky/world ordering. In Enhanced and Remaster, its first-pass fragments are
discarded after capture, so the private scene target contains opaque colour and
depth rather than a copy of the old water surface. The backend captures the
projected fan at the `bgLevelRender` → water-composite boundary, displaces it in
world space from the shared wave field, copies the active player viewport's
scene colour/depth into a private target, and renders the fan again after ships,
dam faces, and other opaque geometry. That second pass provides:

* projective screen-space refraction from the current scene;
* a reflected-camera planar target rendered by replaying the bounded
  pre-composite display-list segment, with depth testing and a water-plane clip
  step; if that pass cannot start, the screen-space reflection ray march and a
  vertically flipped target remain the bounded fallback;
* animated DuDv-like distortion built from one shared bounded wave field and
  four independently scaled/translated normal-map layers; malformed wave
  directions, scalar parameters, and elapsed time fail closed,
  with a bounded, decaying material-space `gfx_water_ripple` wake/impact batch.
  The runtime also advances a deterministic 64×64 CPU heightfield at a fixed
  60 Hz step, uploads it as a separate dynamic map, and samples its
  height/finite-difference gradients in the tessellated composite. The public
  world-space `gfx_water_boat_wake_world` adapter maps the production
  speedboat's position and heading into the same surface coordinates used by
  the shader, then emits a bow plus two diverging stern impulses only above a
  speed threshold. Stationary boats and dam walls receive waves but do not
  continuously generate wakes. The CPU field wraps at the same 256 m period as
  the shader's `fract` mapping, so a boat crossing the world seam cannot create
  a discontinuity. Hardware without the required
  texture unit falls back to the existing analytic wave path while retaining
  the same tested event policy;
* depth-edge contact foam, using linearized depth reconstructed through the
  captured inverse projection, the surface-vs-scene gap, and the strongest
  four-neighbour depth discontinuity. This is the part that lets a ship hull
  or dam wall produce a different edge response from open water.

The composite enables `GL_LEQUAL` with depth writes disabled and blends its
bounded water opacity over the captured scene, so shallow water can reveal the
refracted background while a strong grazing-angle reflection cannot paint over
a ship or dam wall. The reflected
camera pass clips replayed triangles against the captured view-space water
plane, while the composite's interpolated per-surface plane still controls
material-space normals, refraction, and contact response. Each reflection atlas
tile is marked valid only when replayed geometry actually writes it; a partial
or empty replay therefore selects the bounded fallback instead of sampling a
cleared tile. On profiles that can copy scene colour but not sample scene
depth, the backend explicitly clears its private depth to the far plane and
disables depth-derived effects while retaining live destination-depth
occlusion. It fails closed when the scene colour target, camera projection, or
projected water geometry is unavailable, and it is disabled in Original mode.
The first pass keeps the
original CI8 source, texture-coordinate safety policy, and legacy fallback
intact. The backend tessellates each captured fan triangle into a bounded
barycentric water mesh before this composite, so the shared height field has
enough spatial samples for crests and contact edges without changing the
original display-list ordering.

This reflected-camera replay is deliberately bounded to the current player's
pre-composite display-list segment; unsupported or incomplete targets retain
the documented screen-space fallback.

### Authored water material assets

The OpenGL fallback remains deterministic, but it first accepts optional 64×64
BMP assets through `GE_WATER_BASE`, `GE_WATER_NORMAL`, and `GE_WATER_HEIGHT`.
Missing or invalid maps select the procedural fallback. On desktop profiles
with vertex texture units, the height map contributes a bounded signed vertex
displacement along the captured surface normal; GLES and profiles without
vertex texture support retain the procedural height field. Valid base colour
data is uploaded as `GL_SRGB8_ALPHA8` with generated mipmaps, while normal and
height data remain linear. This keeps development and CI reproducible without
making an unavailable asset a rendering failure.

The GLES 3 path uses the game's RGB target format for the private water colour
targets and the GLES-safe `glDrawBuffers` state boundary. Its packed
depth-stencil-to-sampled-depth blit is not portable, so it resolves scene
colour and clears the sampled depth to the far plane; the final water draw
still uses the live destination depth buffer for ship/dam occlusion. The
desktop path retains the full colour/depth copy.

The design follows the height-field normal and local-foam guidance in
[GPU Gems 2, Chapter 18](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-18-using-vertex-texture-displacement),
the opaque-background/projective-distortion/Fresnel sequence in
[GPU Gems 2, Chapter 19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation),
and the sum-of-sines normal model in
[GPU Gems, Chapter 1](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).
The `reflect`/`refract` reference equations are the normalized-vector
contracts in the
[OpenGL Shading Language specification](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html).
The reflected-camera/refracted-background sequence also follows the conventional
framebuffer/clipping/Fresnel/normal-distortion sequence described in the
[OpenGL water guide](https://medium.com/@vincehnguyen/simplest-way-to-render-pretty-water-in-opengl-7bce40cbefbe).
The corresponding sample implementations are available in the
[GPU Gems Book source repository](https://github.com/QianMo/GPU-Gems-Book-Source-Code);
the port uses the algorithms, not the legacy sample's engine-specific code.

### GPU Gems coverage and deliberate boundary

The current pass is intentionally a safe vertical slice around the game's
legacy water fan. The math contracts in `tests/water_render_policy_test.cpp`
cover the implemented equations and their malformed-input behavior: normalized
reflection, perspective ray projection, Snell refraction, equal-index passthrough, total internal
reflection, non-unit normals, vector overflow/non-finite inputs, 100-point
Fresnel monotonicity, Schlick endpoints, perspective division and UV margins,
two-component wave bounds, smooth shoreline/contact response, four-neighbour
depth edges, triangle validity/area, mode tiers, and the pre-world → world →
composite → HUD ordering. The PC BDD water scenarios then compile the actual
GLSL path and prove the opaque scene capture plus `LEQUAL` depth-tested
composite in Enhanced and Remaster while
proving Original stays on the legacy path.

`ge007_bump_mapping_policy` separately covers the finite-difference normal
contract, gradient direction, tier strength, zero/negative strength, and
non-finite/overflow fail-closed behavior. The BDD trace assertion checks that
the generated composite shader is the path that reports the bump mapping.

### Water test matrix

Water verification is deliberately split by contract level:

* `ge007_water_render_policy` exercises the backend-neutral tier, vector,
  projection, depth, Fresnel, refraction, foam, geometry, and phase policies.
* `ge007_water_render_reference_math` compares those policies against
  independent reference equations and deterministic property samples.
* `ge007_water_heightfield_policy` covers fixed-step propagation, damping,
  bounded drops, finite-difference normals, malformed input, reset/cleanup,
  and the stationary-versus-moving boat-wake policy without SDL or OpenGL.
  `ge007_water_heightfield_integration_contract` then protects the frontend
  lifecycle, separate GPU upload resource, shader finite differences, and
  unsupported-texture-unit fallback at the source boundary.
* `ge007_water_cpu_render_harness` renders a fixed 96×64 scene entirely on
  the CPU without loading the ROM, starting the game, creating SDL, or
  requiring OpenGL. It reuses the production wave/reflection/refraction,
  Fresnel, depth-occlusion, and visibility-floor policies, advances through
  `GeWaterClockState` with an injected fixed step, checks byte-identical
  repeatability plus spatial/reflection/occluder/temporal metrics, and can
  write two PPMs with `ge007_water_cpu_render_harness_test --dump-dir DIR`.
* `ge007_water_render_quality_contract` covers the GPU-Gems water boundary.
  It checks the renderer boundary for world-space displacement, coherent wave
  derivatives, reflected-camera clipping, per-viewport/per-surface state,
  failure cleanup, and complete GL-state restoration.
* `ge007_water_render_bdd_contract` protects the live BDD harness. The normal
  `ge007_pc_bdd` run checks mode selection, shader diagnostics, geometry traces,
  composite traces, and crash handling. Set `GE_BDD_PIXEL_ASSERT=1` to add
  `GE_PCDUMP` framebuffer readback assertions when a display-backed GL run is
  available; llvmpipe/WSLg environments may skip that pixel boundary. The
  dependency-free `ge007_water_render_pixel_oracle` self-test covers the same
  variance, asymmetry, edge, viewport, and temporal metrics without a display.
* `ge007_water_render_missed_contract` now protects the formerly-red fixture
  boundaries: a Dam depth-edge occluder probe, a genuinely non-coplanar second
  surface, bounded boat-wake accumulation, resize invalidation, and the
  profile-matrix entry points. The display-backed fixture cases are opt-in
  through `GE_BDD_GLSL_PROFILES=1` for machines that expose all three SDL
  context families (GLSL120, GLSL130, and GLES); `GE_BDD_RESIZE=1` is used by
  the resize scenario itself. The split-screen scenario sets
  `GE_WATER_TEST_SPLIT=1` because Frigate's multiplayer environment record has
  no authored water fan; that adapter projects a two-triangle surface through
  the real per-viewport camera/reflection/composite path. Authored material assets, complete GL-state
  restoration, shader numeric safety, teardown, and MSAA/HDR capture remain
  source/contract coverage until a real display backend is available.

Set `GE_WATER_DEBUG_REFLECTION=1` during a display-backed capture to replace
the normal material with the raw reflection-atlas sample. This diagnostic path
distinguishes an empty or incorrectly addressed reflection target from a valid
target whose normal Fresnel contribution is intentionally subtle.

The implementation keeps the functional core separate from the renderer shell:
vector normalization, reflection/refraction, Fresnel, wave bounds and
gradients, depth/foam thresholds, clipping/projection, atlas ownership, light
falloff, normal transforms, and frame/resize state transitions are exercised
without starting the game or creating an SDL window. The CPU harness adds a
small deterministic scene/raster boundary around those policies, so ship/dam
occlusion and reflected asymmetry can be checked without pretending that a
CPU image proves GLSL state. Those tests use fixed inputs and bounded
deterministic samples; they do not read wall-clock time, random state, or GPU
output. Display-backed BDD is reserved for the remaining adapter contracts:
context/profile shader compilation, framebuffer attachment and readback, pass
ordering, and the final viewport/resource lifecycle.

The BDD trace is diagnostic evidence, not a substitute for framebuffer
readback. The reflected-camera pass still needs display-backed asymmetric
reflection, foreground ship/dam occlusion, split-viewport isolation, and
multi-surface pixel captures before those visual claims can be promoted to
required green gates. The fixture contracts and pixel-oracle tool name those
boundaries without accepting non-clear pixels as visual proof.

The GPU Gems items below separate the implemented bounded path from the
remaining scene-level explicit follow-up work:

| GPU Gems concern | Current status | Required next boundary |
| --- | --- | --- |
| Height-map vertex displacement and multi-octave wave summation | Implemented as bounded world-space procedural vertex displacement plus an optional authored 64×64 height map on desktop vertex-texture profiles, with barycentric tessellation of captured fan triangles; it still does not provide radial LOD or height-feature occlusion. | Add a world-space/radial water mesh and test displacement, derivatives, normals, and octave amplitude budgets before replacing the legacy fan. |
| Height-derived normals and normal/DuDv assets | Implemented from the shared procedural wave gradient plus four animated tangent-frame normal-texture layers; authored 64×64 BMP maps are validated before deterministic fallback, and malformed shader inputs use finite fallbacks. | Add authored tangent-space normal-map variants and a backend contract for broader asset formats and missing/invalid maps. |
| Render-to-texture/reflection/refraction | Implemented as private active-viewport scene colour/depth copies, a reflected-camera display-list replay with per-surface view-space water-plane clipping, per-tile replay-validity tracking, a depth-tested screen-space ray-march fallback, projective refraction, Fresnel blend, and bounded distortion. Colour-only profiles use an explicit far-depth fallback. | Add multi-surface reflected-target pixel oracles and hardware coverage for the remaining GLSL/context matrix; retain the fail-closed fallback for unsupported profiles. |
| Shoreline and vertical-geometry response | Implemented as active-projection linear depth, scene-depth occlusion, four-neighbour depth discontinuity, contact foam, and a bounded material-space ripple adapter. Ships and dam faces therefore have a distinct screen-space edge response. | Add object-aware disturbance/trail injection and depth-based opacity/foam tuning once those world positions are available. |
| Radial-grid LOD and displacement-driven foam | Not implemented; the legacy `IsWater` fan still has no radial mesh or per-object wake list. | Introduce bounded mesh/height-field ownership and test LOD seams, ripple injection/decay, restart/reset, and foam thresholds before enabling them. |

This keeps the current renderer honest: it delivers the GPU Gems depth and
screen-space principles that fit the existing display-list boundary, while the
remaining geometry/height-field work has named acceptance tests instead of a
second hidden water implementation.

## Layered explosions and smoke

Enhanced and remaster modes retain the original fire/smoke bitmap and its
depth-tested translucent material, then add two deterministic oblique cards
through the shared `explosionRenderPortLayeredQuad` adapter. The extra cards
are smaller and less opaque, with independent yaw/pitch offsets, so the effect
has volume from more viewing angles instead of reading as one camera-facing
sheet. Original mode emits only the original billboard.

The current material still compares against depth but does not update the
depth buffer, matching the N64 cloud render mode. This prevents the effect
from permanently occluding nearby geometry, but it is not yet a soft-particle
depth fade or a true volumetric effect. Bump/normal mapping is implemented for
the water material only; explosions and ordinary legacy world materials still
use their existing bitmap/CPU-side vertex-lighting paths because they do not
yet expose a safe tangent-space texture/material contract.

## Vector UI font

Enhanced and remaster modes render the port F10 overlay through the optional
FreeType-backed vector path. The font source is kept as outlines and the
coverage atlas is regenerated at the larger mapped output-pixel scale whenever
the window scale changes; it is not a single 13-pixel bitmap stretched
forever. Glyph bounds are snapped to output pixels and the atlas uses nearest
sampling so FreeType's coverage values are not blurred by a second
magnification pass.
For the Bank Gothic role used by the watch and both ammo readouts, Science
Gothic is the preferred outline-compatible replacement. It is a libre font
with an OFL license and is based on the same geometric display-font tradition.
The lookup order is:

1. `GE_UI_FONT`, when set;
2. `data/fonts/ScienceGothic[CTRS,slnt,wdth,wght].ttf`;
3. `data/fonts/ScienceGothic-Variable.ttf`;
4. `data/fonts/LibreFranklin-Regular.ttf`;
5. `data/fonts/LibreFranklin-SemiBold.ttf`;
6. the platform's Liberation Sans or Arial fallback.

The source package carries the OFL-licensed Science Gothic font under
`assets/fonts/`; release bundles copy it to `data/fonts/` next to the
executable. The runtime resolves that packaged path through the same
current-working-directory-or-executable-directory policy as the ROM, so a
Steam shortcut or other launch directory does not silently select the blurry
bitmap fallback. `GE_UI_FONT` remains available for an explicit override. If
FreeType, the font, or the OpenGL shader setup is unavailable, the original
bitmap font is used automatically.

The gameplay ammo, watch ammo, and bottom HUD message functions share one
routing seam. In enhanced mode that seam uses the same vector face and
preserves the original eight-way outline; it does not maintain a second
ammo-only font implementation.

The logical-to-window coordinate conversion is shared by the vector renderer
and its tests. Commands are queued while the port overlay builds its display
list and drawn after the software RSP flushes, so OpenGL state does not enter
the game-owned display-list path.

For lifecycle diagnostics, `GE_VECTOR_TEXT_TRACE=1` records the render frame,
the command batch consumed by that frame, and commands deferred to the next
frame. `GE_VECTOR_TEXT_TRACE=1` should be combined with a bounded run or the
PC BDD scenario; it is intentionally disabled during normal play.

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

Depth of field uses the same final presentation boundary. When enabled with
`Video.DepthOfField=1` or `GE_DOF=1`, the renderer preserves the scene depth
buffer before the normal framebuffer resolve, calculates a bounded normalized
CoC-like blur amount, and applies an eight-tap depth-aware blur. Samples are
clamped to the recorded player viewport, so two-player co-op cannot bleed the
top view into the bottom view. DoF is off by default and fails open when depth
capture or the required framebuffer path is unavailable. `GE_POSTFX=0` still
disables sharpening while leaving an explicitly enabled DoF pass active. The
camera-correctness and image-quality gaps are tracked in the
[test-first DoF plan](DEPTH-OF-FIELD.md).

## Verification

The focused `ge007_explosion_effect` test covers render-mode compatibility,
layer count, deterministic scale/orientation/alpha descriptors, bounds, and
invalid-index fail-closed behavior. The PC BDD suite also runs paired Original
and Remaster frame-boundary scenarios and asserts the selected explosion
policy. The focused `ge007_dynamic_lighting` test covers colored point lights,
spot cones, registry overflow priority, malformed effect inputs, shared muzzle
and explosion profiles, vector UI coordinate mapping, output-scale selection,
and PostFX strength policy. The full CTest suite also runs the remaining PC
BDD smoke scenarios.

`ge007_world_lighting_integration` exercises the real world-lighting adapter
against a fixed explosion buffer, including an active later slot after the
legacy screen-shake countdown has expired. `ge007_vector_text_frame` also
covers the atomic readiness boundary used when an output-scale atlas rebuild
fails. `ge007_muzzle_flash_contract` protects the restored legacy angle-zero
muzzle alignment basis and runs as a normal green unit regression.

`ge007_depth_of_field_policy` covers the focus plane, near/far CoC bounds,
malformed depth/config inputs, depth discontinuity rejection, viewport clamps,
and the split-viewport activation matrix. The PC BDD suite also enables DoF
against a live single-player frame and a two-viewport co-op frame, asserting
that depth was captured and both viewport layouts were observed.
