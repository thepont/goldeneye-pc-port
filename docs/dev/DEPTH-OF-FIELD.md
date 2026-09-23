# Depth of Field: test-first implementation plan

Status: the current tree contains a bounded, presentation-pass DoF prototype.
This document separates what is already covered from the tests required before
calling the effect camera-correct and production-ready.

## Decision and difficulty

The best fit for this port is a reverse-mapped z-buffer (gather) effect: keep
the rendered scene colour and depth, calculate a blur radius per output pixel,
and gather neighbouring colours with depth-aware weights. It fits the existing
OpenGL final-presentation seam and does not require re-rendering every level
from several cameras.

The difficulty depends on the acceptance bar:

| Scope | Difficulty | Reason |
| --- | --- | --- |
| Bounded heuristic blur at the existing presentation seam | Medium | The scene copy, depth capture, shader, viewport records, fail-open path, and pure policy already exist. |
| Camera-parameterized CoC with stable near/far behaviour | Medium-high | Raw window depth must be converted using the active projection convention; the focus plane must describe a world/view distance, not a fixed screen-depth value. |
| Robust game-quality DoF | High | Foreground/background bleeding, transparent surfaces, HUD isolation, split-screen boundaries, resize/restart cleanup, shader portability, and bandwidth all need explicit contracts. |

This is not a good candidate for the chapter's ray-traced or
accumulation-buffer approaches. They would require either many scene renders or
substantial changes below the presentation layer. A gather filter is the
smallest coherent vertical slice, provided the tests reject its known image
artifacts instead of treating “the shader ran” as success.

## What the GPU Gems material contributes

[GPU Gems Chapter 23, “Depth of Field: A Survey of Techniques”](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-23-depth-field-survey-techniques)
organizes DoF into ray-traced, accumulation-buffer, layered, forward-mapped
z-buffer, and reverse-mapped z-buffer techniques. It explains the circle of
confusion (CoC) as the blur diameter caused by distance from the focus plane,
and calls out visibility errors at depth discontinuities as the difficult part
of image-based approaches.

[GPU Gems 3 Chapter 28, “Practical Post-Process Depth of Field”](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field)
is the closer engineering reference for this port. It uses a depth image,
foreground/background CoC treatment, downsampled blur levels, and a final
variable-width composite. It also makes the trade-off explicit: a fast
post-process can accept some colour bleeding, while better isolation requires
more buffers or a more intrusive render path.

## Reference and sample-code map

The supplied [GPU Gems book source-code collection](https://github.com/QianMo/GPU-Gems-Book-Source-Code)
is a useful companion lookup, but it is a community mirror rather than the
primary NVIDIA source. Its [GPU Gems 1 Chapter 23 directory](https://github.com/QianMo/GPU-Gems-Book-Source-Code/tree/master/GPU-Gems-1-CD-Content/Image_Processing/Depth_of_Field)
is the relevant sample-code location. The repository index identifies it under
GPU Gems 1's Image Processing section. Use it to inspect the original demo's
resource layout, shader entry points, and render-pass sequencing; do not copy
the old DirectX/Cg project structure into this port.

The online GPU Gems 3 chapter is the more useful staged sample reference. Map
its examples to this project as follows:

| Reference stage | Port equivalent or future seam |
| --- | --- |
| Depth/CoC initialization | `gfx_opengl.cpp` depth capture plus a future tested linear-depth/CoC policy. |
| Downsampled colour and CoC buffers | Not present in the current one-pass prototype; would require additional render targets. |
| Near-CoC smoothing and foreground treatment | Future render-contract slice for silhouette bleeding and foreground blur. |
| Final variable-width composite | Current `ascDofBlur()` is the deliberately smaller eight-neighbour approximation, not a drop-in equivalent. |

The NVIDIA [GPU Gems preface](https://developer.nvidia.com/gpugems/gpugems/preface)
also documents that the books' samples were distributed with the companion CD
and supplementary material. The source collection above is therefore useful
for archaeology, while the NVIDIA chapter pages remain the authoritative
algorithm references. The listings are HLSL/Cg-era examples; the project
should derive short, original GLSL/policy code from their stages and pin the
behaviour with tests rather than transplanting listings verbatim.

### Small reference algorithm

This is the implementation shape the tests should pin; it is intentionally
original pseudocode, not a copied shader listing:

```text
raw_depth = depth.sample(uv)
view_distance = linearize(raw_depth, projection, depth_convention)
coc = clamp(coc_from_distance(view_distance, focus_distance, lens), max_radius)

colour_sum = centre_colour
weight_sum = 1
for sample in neighbourhood(uv, coc.radius):
    weight = depth_edge_weight(centre_depth, sample.depth)
    colour_sum += sample.colour * weight
    weight_sum += weight

output = colour_sum / max(weight_sum, epsilon)
```

The important test seams are `linearize`, `coc_from_distance`, edge weighting,
normalization, and viewport-clamped neighbourhood selection. Keeping those
seams independent lets the CPU reference tests catch a projection or depth
convention bug without compiling a shader.

### Project history and previous implementations

A history search at the time of writing found no committed implementation
containing `GE_DOF` or `depth_of_field`. The current DoF policy, depth capture,
shader changes, and tests are working-tree additions. The nearest committed
rendering lineage is:

1. [`68d2f34d`](https://github.com/thepont/goldeneye-pc-port/commit/68d2f34d6cf82b677599071e9c6c03bcba21f7e8)
   added the original adaptive final PostFX pass and its fullscreen shader.
2. [`b62bdbe5`](https://github.com/thepont/goldeneye-pc-port/commit/b62bdbe5)
   tightened first-use GL state preservation for that pass.
3. [`9507669f`](https://github.com/thepont/goldeneye-pc-port/commit/9507669f)
   integrated the broader presentation stack, configuration, and rendering
   documentation.

Those commits are useful examples for resource ownership, fail-open shader
initialization, resize handling, and GL state restoration. They are not prior
DoF implementations and should not be cited as evidence that the DoF image
contract is already proven.

For a local source walk, start here:

- `port/fast3d/depth_of_field_policy.h`: pure policy and viewport boundary;
- `tests/depth_of_field_policy_test.cpp`: context-free unit baseline;
- `port/src/ascension_postfx.cpp:111-223`: current shader-side DoF amount,
  depth weighting, viewport clamp, and eight-tap blur;
- `port/fast3d/gfx_opengl.cpp:1398-1528`: depth texture allocation, FBO
  completeness check, and depth blit;
- `port/fast3d/gfx_pc.cpp:4941-4966` and
  `port/fast3d/gfx_sdl2.cpp:448-455`: capture and pre-swap presentation order;
- `tests/run_pc_bdd.sh:468-492`: current single-player and split-viewport
  activation scenarios.

## Current implementation boundary

The existing path is useful infrastructure, but it should not be described as
a complete implementation of the GPU Gems algorithm:

- [`depth_of_field_policy.h`](../../port/fast3d/depth_of_field_policy.h)
  validates ordered depth bands, evaluates near/far blur, bounds the radius,
  rejects discontinuous samples, and clamps samples to a player viewport.
- [`depth_of_field_policy_test.cpp`](../../tests/depth_of_field_policy_test.cpp)
  already covers focus, near/far monotonicity, malformed values, edge
  rejection, viewport clamping, and the activation matrix without a GL
  context.
- [`gfx_opengl.cpp`](../../port/fast3d/gfx_opengl.cpp) copies the current depth
  buffer into a texture and exposes it through the rendering API. The capture
  is fail-closed when the source framebuffer or required FBO operations are
  unavailable.
- [`ascension_postfx.cpp`](../../port/src/ascension_postfx.cpp) applies one
  eight-neighbour gather pass at pre-swap and clamps samples to the recorded
  viewport rectangles. `GE_DOF=1` or `Video.DepthOfField=1` enables it, and it
  is disabled by default.
- [`run_pc_bdd.sh`](../../tests/run_pc_bdd.sh) has single-view and two-viewport
  activation scenarios. Their log assertions prove that depth was captured and
  that both viewport layouts were observed; they do not yet prove pixel-level
  blur quality.

The current shader consumes the captured normalized depth value directly with
fixed normalized focus bands. That is a deliberate bounded heuristic, not yet
the camera-parameterized CoC calculation described by GPU Gems. A future
implementation must define the depth convention and linearization once, then
feed that authority to both the CPU reference policy and the shader.

There is also a UI boundary to settle: depth capture currently occurs after
the vector text draw in the frame assembly path. A test must decide and pin
whether HUD/document text stays sharp; if it must, the scene copy and UI
composite need to be separated or the UI must be explicitly masked.

## Test-first contract

Every implementation slice below starts with an automated test that is observed
failing. A live level is not a substitute for a unit or render-contract test.

### Unit and reference tests

These tests should be pure and run without SDL, OpenGL, a ROM, or a level:

1. **Depth convention and projection invariance (new red test).** Given the
   same view-space distance, two valid near/far projection pairs produce the
   same linear distance and CoC. Raw window-depth values must not be treated as
   linear distance.
2. **CoC boundaries (extend the existing test).** CoC is zero on the focus
   plane, has the expected sign on the near and far sides, grows monotonically
   within each blur band, and clamps to the configured maximum.
3. **Malformed configuration (green baseline plus adjacent cases).** NaN,
   infinity, reversed bands, negative distances, and an out-of-range radius
   fail closed without producing a non-finite result.
4. **Depth-edge weighting (extend the existing test).** Equal-depth samples
   retain their weight, a threshold edge contributes zero, and a large depth
   step cannot pull background colour through a foreground silhouette.
5. **Energy/normalization reference (new red test).** A flat colour remains
   unchanged after any legal blur radius; zero total weight returns the centre
   sample rather than NaN or black.
6. **Viewport isolation (extend the existing test).** Sampling clamps to the
   current player rectangle, never crosses a neighbouring rectangle, and an
   unsupported viewport count disables DoF rather than partially applying it.

### Render-contract tests

Use a deterministic small colour/depth fixture, for example two stacked
64x32 player rectangles containing flat but different colours and a narrow
near-depth foreground stripe over a far-depth background. Assert invariants,
not driver-specific exact pixels:

- the focused region is unchanged within a defined epsilon;
- out-of-focus variance decreases in the intended region;
- the foreground stripe does not acquire an unbounded background halo;
- the top fixture never samples the bottom fixture and vice versa;
- alpha is preserved;
- disabled DoF, missing depth, incomplete FBO, invalid configuration, and
  shader initialization failure return the original image unchanged;
- a resize, framebuffer recreation, level restart, and repeated toggle do not
  use a stale depth texture or stale viewport rectangle.

These should be a separate `ge007` test target (or an extension of the existing
render-contract tests), with a CPU reference implementation sharing only the
policy contract—not the shader's private code. Copying shader expressions into
the test would allow the same bug to pass twice.

The HUD contract must be explicit before implementation: the recommended
behaviour is that objectives, ammo, reticles, menus, and documents remain
sharp. Add a fixture with a sharp UI glyph over blurred scene depth and make
that test red before changing the capture/composite order.

### BDD and hardware-gated tests

Keep the existing scenarios, then add visual assertions only after the pure
contracts are green:

- `depth_of_field_single_view`: DoF activation, depth dimensions, and a stable
  one-player frame;
- `depth_of_field_split_view`: two independent depth/view rectangles with no
  cross-player colour or blur;
- disabled/fallback scenario: the original frame is preserved when depth
  capture or the shader path is unavailable;
- resize/restart scenario: toggling DoF and changing output size recreates
  resources without flicker or stale image data.

The current PC BDD runner requires X11/XWayland and should remain availability-
gated. A skipped display test is reported as skipped, never counted as proof
that the image contract passes.

### Shader-specific gates now in the tree

The presentation shader has three separate automated boundaries:

- `ge007_ascension_postfx_shader_contract` reads the production GLSL literals
  and checks that the DoF, depth-edge, viewport, and colour-output contracts
  are still present. It is also part of the deterministic `ge007-unit`
  coverage run.
- `ge007_ascension_postfx_shader_compile` runs `glslangValidator` against the
  exact production vertex and fragment bodies for GLSL 1.20, 1.30, 1.50 core,
  3.30 core, and 3.00 ES. This is a stage-compile portability check and is
  also included in the deterministic coverage run when the validator is
  installed; the validator's SPIR-V path is not used for the legacy profiles.
- `ge007_ascension_postfx_gpu` creates a small software OpenGL context under
  `xvfb-run`, links the 3.30 core program through the driver, renders a
  deterministic colour/depth fixture, and asserts that an in-focus ROI stays
  sharp while an out-of-focus ROI changes. It is labelled `ge007-gpu`, not
  `ge007-unit`, because it is availability-gated and cannot be collected by
  gcov as ordinary C/C++ line coverage.

The legacy profile test also caught and closed a real defect: GLSL 1.20 needs
`attribute`/`varying` and `texture2D`, while newer profiles use `in`/`out` and
`texture`. The production shader now selects those forms from `__VERSION__`.
These tests prove compilation, linkage, and a small pixel behaviour contract;
they do not prove every GPU vendor, driver, level, HUD composition, or shader
instruction branch.

## Implementation slices after the red baseline

1. **Make depth authoritative.** Define whether the source is standard NDC
   depth, reversed-Z, or another convention. Add one tested linearization
   function and pass the active projection parameters or precomputed constants
   to the shader.
2. **Make CoC camera-aware.** Replace fixed normalized bands with a validated
   focus distance and bounded CoC scale/bias. Preserve the existing safe
   fallback for invalid or unavailable depth.
3. **Choose the quality tier.** Start with a gather filter that passes the
   synthetic edge fixture. If foreground bleeding is still above the agreed
   threshold, split near and far CoC buffers or add a depth-aware blur pyramid;
   do not silently increase taps without a bandwidth check.
4. **Protect presentation layers.** Keep HUD/document text sharp and keep all
   samples inside each split-screen viewport. Verify GL state restoration after
   both success and failure paths.
5. **Prove lifecycle and cost.** Test resize/restart/toggle cleanup, shader
   profile fallbacks, maximum supported viewport count, and a bounded GPU
   cost at the target output sizes. Record the chosen quality tier and its
   maximum blur radius in the rendering documentation.

## Definition of done

DoF is ready to expose as a normal user-facing feature only when:

- the unit/reference tests above are green and the new tests were observed red
  before their implementation changes;
- the render-contract fixtures prove depth linearization, edge behaviour,
  normalization, viewport isolation, HUD policy, and fail-open behaviour;
- single-player and two-player BDD runs pass when a display is available;
- resize, restart, toggle, and shader/FBO failure paths are covered;
- the performance cost and known residual artefacts are recorded;
- the effect remains optional and the no-DoF output is unchanged.

Until then, the correct status is “bounded DoF prototype with activation
coverage,” not “GPU Gems-quality depth of field.”
