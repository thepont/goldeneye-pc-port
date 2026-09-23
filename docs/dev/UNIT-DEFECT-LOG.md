# Deterministic unit defect log

This is the handoff log for defects found by pure unit tests. Each entry records
the observable failure, the test that reproduces it, the affected production
symbol, and the eventual disposition. Entries marked **RED BASELINE** were
observed failing before the production fix; the normal test suite must finish
green after the fix.

The format is intentionally portable to the companion port repository:

| ID | Area | Status | Reproduction | Affected code |
| --- | --- | --- | --- | --- |
| GE-MATRIX-001 | Target-basis / look-at | FIXED | `tests/matrixmath_test.cpp`: coincident target and non-finite direction contracts | `src/game/matrixmath.c` target basis construction |
| GE-MATRIX-002 | Rotation normalization | FIXED | `tests/matrixmath_test.cpp`: uniform-scale normalization contract | `matrix_4x4_normalize_rotation` |
| GE-MATRIX-003 | Axis-angle rotation | FIXED | `tests/matrixmath_test.cpp`: X-axis quarter-turn and zero-axis contracts | `matrix_4x4_set_rotation_axis_angle` |
| GE-MATRIX-004 | Matrix inversion | FIXED | `tests/matrixmath_test.cpp`: singular rotation, affine, and general inverse contracts | `matrix_4x4_set_inverse_rotation_and_translation`, `matrix_4x4_invert_affine`, `matrix_4x4_invert` |
| GE-MATRIX-005 | Legacy muzzle-flash direction basis | FIXED | `tests/muzzle_flash_contract_test.cpp`: angle-zero muzzle direction contract | `src/game/gunfire.c`, `src/game/matrixmath.c` |
| GE-MATH-001 | Zero quaternion to matrix | FIXED | `tests/game_math_test.cpp`: `zero quaternion conversion does not create NaNs` | `src/game/quaternion.c:quaternion_to_matrix` |
| GE-MATH-002 | Degenerate matrix to quaternion | FIXED | `tests/game_math_test.cpp`: `zero matrix conversion falls back to identity` | `src/game/quaternion.c:quaternion_from_matrix` |
| GE-CRC-001 | Save CRC / PRNG drift | COVERED | `tests/crc_test.cpp`: N64 PRNG golden vector, persisted save checksum, and content sensitivity | `src/game/crc.c`, `port/src/random.c` |
| GE-OBJECTIVE-001 | Objective-status motion/matrix helper boundary behavior | COVERED | `tests/objective_status2_test.cpp`: deterministic random inputs, fixed-frame integration, and repeated matrix dispatch | `src/game/objective_status2.c` |
| GE-OBJECTIVE-002 | Objective status helpers accepted negative indexes and used an implicit null-pointer expression | FIXED | `tests/objective_test.cpp`: negative text/difficulty/status indexes and empty objective slots, observed red before the guard fix | `src/game/objective_status.c:get_text_for_objective`, `get_difficulty_for_objective`, `get_status_of_objective` |
| GE-LOADMODEL-001 | Object-reuse scan compared the low byte of the header word instead of the PC-native prop-definition type byte | FIXED | `tests/loadobjectmodel_test.cpp:test_object_reuse_reads_the_type_byte`, red before the production fix | `src/game/loadobjectmodel.c:setupFindObjForReuse` |
| GE-OBJECTHANDLER2-001 | Model-GDL compaction narrowed the host allocation pointer to `s32` before returning it to the memory pool | FIXED | `tests/objecthandler2_test.c:test_compaction_preserves_host_pointer`, red before the pointer-preserving port fix | `src/game/objecthandler_2.c:sub_GAME_7F0762E0` |
| GE-MODEL-002 | Model-node relocation narrowed the loaded sidecar allocation address to `u32`, producing invalid pointers above 4 GiB | FIXED | `tests/model_pointer_promotion_test.c:test_model_pointer_relocation_keeps_host_width`, red before the host-width relocation fix | `src/game/model.c:modelPromoteNodeOffsetsToPointers`, `sub_GAME_7F075A90` |
| GE-IMAGE-001 | Texture index-row alignment narrowed a host destination pointer to `u32`, moving subsequent rows into unmapped low memory | FIXED | `tests/image_texture_test.c:test_index_rows_align_without_narrowing_host_addresses`, segfault red baseline with a two-row high-address buffer | `src/game/image.c:texAlignIndices` |
| GE-IMAGE-002 | Odd-width I4 lookup expansion consumed the next source index for the unused low nibble | FIXED | `tests/image_texture_test.c:test_odd_width_ci4_rows_do_not_consume_the_next_index`, red before the bounds check | `src/game/image.c:texInflateLookupFromBuffer` |
| GE-IMAGE-003 | Texture bitstream setup narrowed the source pointer to `s32`, so high-address compressed data could not be read | FIXED | `tests/image_texture_test.c:test_bitstream_source_preserves_host_pointer_width`, segfault red baseline | `src/game/image_bank.c:texSetBitstring`, `src/game/image_bank.h` |
| GE-IMAGE-004 | Uncompressed texture output alignment narrowed the destination pointer to `u32`, moving decoded pixels into unmapped low memory | FIXED | `tests/image_texture_test.c:test_uncompressed_destination_alignment_preserves_host_pointer_width`, red after the bitstream fix exposed the next seam | `src/game/image.c:texReadUncompressed` |
| GE-RZ-001 | PC RZ decompression returned the consumed input pointer as `s32`, losing its upper host address bits before the next texture section | FIXED | `tests/rzdecomp_test.cpp:test_round_trip`, high-address stack source and full-width consumed-position assertion, red before the adapter fix | `port/src/rzdecomp.c:rzipGetSomething`, `src/game/decompress.h` |
| GE-TEXT-001 | Text wrapping copied words longer than 31 bytes into a fixed 32-byte stack buffer | FIXED | `tests/textrelated_policy_test.c:test_text_wrap_preserves_words_longer_than_legacy_scratch_buffer`, AddressSanitizer red baseline before the sized PC buffer | `src/game/textrelated.c:textWrap` |
| GE-LIGHT-001 | Segmented room-vertex resolution narrowed a host vertex-base pointer to `s32`, returning an invalid address above 4 GiB | FIXED | `tests/lightfixture_test.c:test_segmented_vertex_resolution_keeps_host_pointer_width`, red before the PC pointer-preserving fix | `src/game/lightfixture.c:lightFindVertexBaseForTri`, darkened-vertex index arithmetic |
| GE-LIGHT-002 | Light-fixture triangle decoding used N64 endian/32-bit word views on the little-endian 64-bit PC command layout, reversing `G_TRI1` vertices and losing all `G_TRI4` indices | FIXED | `tests/lightfixture_test.c:test_triangle_index_decoding_is_portable_and_bounded`, `test_tri4_index_decoding_uses_all_four_packed_triangles`, red before packed-word decoding | `src/game/lightfixture.c:extract_vertex_indices_from_triangle` |
| GE-LIGHT-003 | Unknown light-fixture triangle encodings left caller-provided vertex indexes untouched, allowing stale indexes to reach coordinate and darkening helpers | FIXED | `tests/lightfixture_test.c:test_triangle_index_decoding_is_portable_and_bounded`, red with sentinel indexes before the safe default initialization | `src/game/lightfixture.c:extract_vertex_indices_from_triangle` |
| GE-VTXSTORE-001 | Vertex-store allocation read pool-entry counts from the high halfword of a host little-endian `s32`, so the allocator could only split one block on PC | FIXED | `tests/vtxstore_test.c:test_allocate_preserves_pointer_width_and_splits_free_blocks`, second allocation red before the host-width count fix | `src/game/vtxstore.c:vtxstore_allocate` |
| GE-VTXSTORE-002 | Vertex-store allocation returned a host vertex pointer through `s32`, truncating allocations above 4 GiB | FIXED | `tests/vtxstore_test.c:test_allocate_preserves_pointer_width_and_splits_free_blocks`, mmap-backed high-address pool red before the PC return-type fix | `src/game/vtxstore.h`, `src/game/vtxstore.c`, `port/include/pc_protos.h` |
| GE-VTXSTORE-003 | `vtxstore_fix_refs` read an active object's model through the 32-bit `ChrRecord.chrflags` alias, truncating a valid host `Model*` before dereferencing its header | FIXED | `tests/vtxstore_test.c:test_fix_refs_walks_object_models_without_pointer_narrowing`, host-width model fixture and node-data replacement contract | `src/game/vtxstore.c:sub_GAME_7F09BAC4` |
| GE-COOP-001 | AI/environmental damage used the no-player `-1` sentinel as a multiplayer player index | FIXED | `tests/record_damage_kills_test.c`: lethal `-1`, out-of-range, killer, and suicide cases; red before the guard fix | `src/game/bondview2.c:record_damage_kills` |
| GE-CCTV-001 | CCTV orientation used the prop's placement pad instead of its separate look-at pad | FIXED at the unit seam; live visual confirmation pending | `tests/cctv_orientation_test.cpp`: all four Bunker CCTV records observed red before the field-selection fix | `src/game/cctv.c:cctvGetLookPad`, `src/game/prop.c:setupCctv` |
| GE-BLOOD-001 | 64-bit pointer narrowing in transpose | OPEN RISK | `tests/blood_decrypt_test.cpp` exercises the transpose successfully, but GCC warns on pointer-to-`u32` narrowing in the implementation | `src/game/blood_decrypt.c:bloodImgTranspose` |
| GE-WATER-001 | Water wave direction and scalar uniforms could propagate zero, non-finite, or oversized values into GLSL normalization and trigonometry | FIXED | `tests/water_render_policy_test.cpp` and `tests/water_render_missed_contract_test.cpp`: safe 2D direction, wave-parameter, and sanitized-time contracts; red before the shader guard | `port/include/gfx_water_composite_policy.h`, `port/fast3d/gfx_opengl.cpp` water shader |
| GE-WATER-002 | Generated water vertex/fragment shader source ignored `snprintf` truncation, allowing incomplete GLSL to reach compilation | FIXED | `tests/water_render_missed_contract_test.cpp:test_shader_source_capacity_contract`, red before the length checks | `port/fast3d/gfx_opengl.cpp:gfx_opengl_compile_water_composite_program` |
| GE-PATH-001 | Empty waypoint and waypoint-group pools reached a modulo-zero random selection | FIXED | `tests/game_utility_edge_test.c`: empty-list random-selection cases, red before the zero-length guards | `src/game/padhalllv.c:waypointFindRandomByDist`, `waygroupFindRandomByDist` |
| GE-INIT-001 | Model-hit free-list endpoints retained stale `prev`/`next` links after pool initialization | FIXED | `tests/model_hit_freelist_test.c`: prefilled pool endpoint assertions, red before endpoint clearing | `src/game/initunk_005450.c:initModelHitEntryFreeList` |
| GE-INIT-002 | Host queue pointer was stored in a 32-bit global and lost its upper address bits | FIXED | `tests/game_initialization_edge_test.c`: full-width `D_80036418` pointer identity, red before the type correction | `src/game/initunk_0009E0.c`, `src/game/objecthandler.h` |
| GE-INIT-003 | Multiplayer explosion initialization left a stale solo scorch-buffer pointer | FIXED | `tests/explosion_buffer_init_test.c`: solo-to-multiplayer reinitialization, red before explicit pointer reset | `src/game/initexplosioncasing.c` |
| GE-WINDOW-001 | Window-piece allocation divided by zero when the player-count provider returned zero | FIXED | `tests/window_piece_allocation_test.c`: zero-player boundary, red before the defensive count floor | `src/game/alloc_window_pieces.c` |
| GE-CHEAT-001 | Cheat-text initialization hard-coded 75 entries and left the terminal invalid slot stale | FIXED | `tests/cheat_text_init_test.c`: `CHEAT_INVALID` sentinel reset, red before the inclusive bound | `src/game/initcheattext.c` |
| GE-OBJECT-001 | Object-cleanup dispatch narrowed `propDefs` to `u32` on the 64-bit host | FIXED | `tests/object_cleanup_dispatch_test.c`: heap-backed prop-definition stream, red before the pointer-width fix | `src/game/cleanup_objects.c` |
| GE-GUARD-001 | Guard initialization accepted negative counts and body reset divided by zero for empty model pools | FIXED | `tests/guard_initialization_test.c`: negative allocation and empty body/head pools, red before both guards | `src/game/initguards.c` |
| GE-MODEL-001 | Model-slot initialization accepted negative regular or animated-slot counts and underflowed capacities | FIXED | `tests/model_slot_init_test.c`: negative count matrix, red before count clamping | `src/game/initunk_005520.c` |
| GE-TEX-001 | Negative or malformed `-mt` values wrapped into unsafe texture-pool allocation sizes and the option was queried twice | FIXED | `tests/initialization_allocations_test.c`: malformed-value and single-query cases, red before parsing validation | `src/game/initmttex.c` |
| GE-MENU-001 | PC menu video-buffer storage narrowed an allocated host pointer to `s32` before frame-buffer submission | FIXED | `tests/menu_initialization_test.c`: high-address allocation and 64-byte alignment, red at the conflicting pointer declaration/warning | `src/game/front.h`, `src/game/front.c`, `src/game/initmenus.c` |
| GE-VIEWPORT-001 | PC z-buffer allocation and binding narrowed host pointers to `s32` | FIXED | `tests/viewport_depth_buffer_test.c`: high-address allocation, all resolution/player-count modes, binding, and depth-clear command; red before pointer-safe storage | `src/game/viewport.c`, `src/game/viewport.h`, `src/game/front.c` |
| GE-OBJECT-002 | Safe-item level-load initialization wrote through the unrelated `ObjectRecord::prop` field instead of linking the `SafeObjectRecord::next` chain | FIXED | `tests/object_initialization_test.c`: safe-item head/link assertions, red after the test used a real safe-item record and before the typed-list fix | `src/game/initobjects.c`, `src/game/prop.c`, `src/game/propobj.h` |
| GE-BG-001 | Null-terminated display-list LUT scans checked a host memory byte instead of the `G_ENDDL` opcode byte in `w0` | FIXED | `tests/bg_apply_lut_test.c`: command after `G_ENDDL` was modified on little-endian hosts, red before the opcode extraction fix | `src/game/bgapply.c:bgApplyDynamicCCRMLUT` |
| GE-BG-ROOM-001 | Room-data initialization trusted player/room provider counts beyond the fixed pointer and matrix-table capacities | FIXED | `tests/bg_room_trans_test.c`: fifth-player and 301-room provider counts with sentinels, red before capacity clamping | `src/game/bgroomtrans.c:initializeRoomData` |
| GE-DEBUGCAM-001 | Debug-camera briefing-key rotation used the X delta twice and ignored the camera's Z delta in the rotated X coordinate | FIXED | `tests/debug_camera_test.c`: 90-degree yaw X/Z basis assertion, red before the corrected rotation term | `src/game/debug_camera.c:initializeDebugCameraPosition` |
| GE-TITLE-001 | Gunbarrel display-list construction emitted a vertex command and a triangle with negative indexes for fewer than three vertices | FIXED | `tests/title3_display_list_test.c`: negative, zero, and two-vertex counts, red before the early end-list guard | `src/game/title3.c:sub_GAME_7F01BFF8` |
| GE-TITLE-002 | Gunbarrel render-hole generation divided by zero for a zero segment count | FIXED | `tests/title3_display_list_test.c`: zero-segment sentinel record, red before the non-positive count guard | `src/game/title3.c:createGunbarrelRenderHole` |
| GE-RSP-001 | RSP framebuffer and output-buffer addresses were narrowed to 32 bits before scheduler submission on 64-bit hosts | FIXED | `tests/rsp_task_test.c`: high-address framebuffer submission and output-buffer allocation, red before `uintptr_t` storage | `src/game/rsp.h`, `src/game/rsp.c` |
| GE-TEX-MICRO-001 | Texture scale helpers divided by zero for non-positive widths; the undefined result was platform-dependent even when it appeared numerically as zero | FIXED | `tests/othermodemicrocode_test.c`: zero/negative widths and `FE_DIVBYZERO`, red before the input guards | `src/game/othermodemicrocode.c:ceil8000/ceil4000/ceil2000/ceil1000` |
| GE-MPMENU-001 | Multiplayer award ranking truncated fractional running maxima/minima to `s32`, allowing a lower later value to replace the true winner | FIXED | `tests/mp_menu_policy_test.c`: `2.9` vs `2.5`, `-0.9` vs `-0.5`, four-player boundaries, tie breaks, and menu-state contracts; red before the `f32` running-value fix | `src/game/mpmenu.c:mpFindMaxFloat`, `mpFindMinFloat` |
| GE-LANGUAGE-001 | Japanese glyph-cache scan read `cache[i + 1]` before proving that the final slot had a neighbor | FIXED | `tests/language_state_test.c:test_last_single_cache_slot_does_not_read_past_the_cache`, AddressSanitizer red baseline before the bounds-order fix | `src/game/language.c:langGetJpnCharPixels` |

## GE-LIGHT-001 — segmented light vertices lost the host pointer high half

**Observed:** `lightFindVertexBaseForTri()` resolves a segment-14 vertex
reference by adding its 24-bit offset to the room's vertex base. The original
PC path converted that host pointer to `s32`, so a normal allocation above the
4 GiB boundary produced a bogus low address.

**Red test:** `tests/lightfixture_test.c` builds a real `gSPVertex` command
with a segment-14 offset and a host-width room vertex array. The baseline
failed the full-pointer identity assertion before the PC address fix.

**Fix:** the PC path now uses `uintptr_t` for segmented-address detection,
offset addition, and persistent darkened-vertex index calculation. The N64
32-bit arithmetic remains unchanged.

## GE-LIGHT-002 — packed triangle indexes used the wrong host layout

**Observed:** the PC `Gfx` shim stores packed command words in host-width
`words.w0`/`words.w1` fields. The N64 `Gtri` member and 32-bit pointer casts in
`extract_vertex_indices_from_triangle()` therefore reversed ordinary triangle
vertices and read the wrong storage for the four compact triangles in
`G_TRI4`.

**Red test:** `tests/lightfixture_test.c` constructs real `gSP1Triangle` and
`gSP4Triangles` commands and asserts that every decoded triangle preserves the
source vertex order. The baseline failed all four `G_TRI4` cases and returned
`3,2,1` for an ordinary `1,2,3` triangle.

**Fix:** the PC path now extracts the packed nibbles/bytes from `words.w0` and
`words.w1`; the original N64 member access remains under the non-PC path.

## GE-LIGHT-003 — malformed triangle types propagated stale indexes

**Observed:** the triangle-index switch had no default initialization. A
caller passing an unsupported encoding received whatever values happened to be
in its output variables, which could become an out-of-range room-vertex read.

**Red test:** `tests/lightfixture_test.c` initializes all three outputs to
`-1`, passes an unsupported encoding, and observed the sentinels leaking out
of the helper.

**Fix:** all three outputs are initialized to zero before decoding. Valid
encodings still overwrite all three values, while malformed input resolves to
the safe vertex-zero triple.

## GE-VTXSTORE-001 — allocator capacity read from the wrong endian halfword

**Observed:** `vtxstore_allocate()` recovered the number of free-list entries
with `((s16 *)&count)[1]`. That is the low 16-bit count on the big-endian N64,
but zero on the little-endian PC host. The first request could consume the
initial block, while the next request incorrectly reported no split slot.

**Red test:** `tests/vtxstore_test.c` resets a single-player store, allocates
two four-vertex primary blocks, and requires the second pointer to be exactly
one block after the first. The second allocation returned null in the red
baseline.

**Fix:** the PC path uses the actual `s32` entry-count globals; the original
halfword extraction remains in the N64 path.

## GE-VTXSTORE-002 — vertex-store pointers lost the host address high half

**Observed:** the allocator returned `Vertex *` storage as `s32`, even though
the PC pool may live above the 32-bit address range. A real `mmap`-backed high
address therefore came back as a low, invalid pointer.

**Red test:** `tests/vtxstore_test.c` supplies the store from a high-address
mapped fake memory pool and compares the first primary and secondary results
with their complete allocation addresses.

**Fix:** the PC ABI exposes `uintptr_t` from `vtxstore.h`, `vtxstore.c`, and
the implicit-prototype compatibility header. The N64 ABI remains `s32`.

## GE-VTXSTORE-003 — reference repair narrowed the active object's model

**Observed:** the D255 crash path treated an active object's model pointer as
the 32-bit `ChrRecord.chrflags` field. On a 64-bit PC, the high half of a
valid `Model*` was discarded before `model->obj` was read.

**Regression test:** `tests/vtxstore_test.c` builds an active `PROP_TYPE_OBJ`
chain with a model above the 32-bit address range, an opcode-`0x18` root node,
and a deterministic `modelGetNodeRwData` fake. `sub_GAME_7F09BAC4()` must
replace the matching node value and must ignore a non-object prop.

**Fix:** the PC path reads the model through `ObjectRecord.model`, the
correctly typed host-width field. The N64 aliasing path remains unchanged.

## GE-COOP-001 — no-player damage sentinel reached multiplayer attribution

**Observed:** AI shots, autogun damage, and toxic gas intentionally call the
damage routine with `playerid == -1`, meaning that no player should receive
kill credit. The multiplayer death path treated that sentinel as an array
index, reached `set_cur_player(-1)`, and could then crash while updating the
kill display or kill-count table.

**Red test:** `tests/record_damage_kills_test.c` calls the real
`record_damage_kills()` with deterministic fake player/audio ports. Its matrix
covers `-1`, `player_count`, a valid killer, and a valid suicide. The original
baseline failed with `RED: record_damage_kills passed playerid -1 to
set_cur_player`.

**Fix:** `record_damage_kills()` now validates the attribution ID before both
the backside-stat array access and the lethal-death attribution block. Invalid
sentinels still apply damage and death bookkeeping, but cannot switch the
current player or update a player's kill table.

## GE-MATH-001 — zero quaternion creates NaN matrix entries

**Observed:** the all-zero quaternion produces NaNs in the 3x3 rotation
portion because `quaternion_to_matrix` divides by the quaternion norm squared.

**Expected:** an invalid zero-length rotation input must produce a finite
identity matrix, preserving the homogeneous row and column. A NaN transform
can contaminate every downstream model or camera transform that consumes it.

**Red test:** `test_quaternion_degenerate_inputs` in
`tests/game_math_test.cpp`, observed in the red baseline on 2026-09-22.

**Status:** FIXED after the red baseline. `quaternion_to_matrix` now returns
the finite identity matrix when the quaternion norm is not positive. The green
regression remains in the normal unit suite.

## GE-MATH-002 — zero matrix produces a non-unit quaternion

**Observed:** `quaternion_from_matrix` given an all-zero matrix returns
`{0.5, 0, 0, 0}`. This is finite but is not a valid unit rotation and does not
round-trip to the identity rotation.

**Expected:** a degenerate or non-rotation matrix must return the identity
quaternion `{1, 0, 0, 0}`. Downstream interpolation and transform code assumes
unit quaternions.

**Red test:** `test_quaternion_degenerate_inputs` in
`tests/game_math_test.cpp`, observed in the red baseline on 2026-09-22.

**Status:** FIXED after the red baseline. `quaternion_from_matrix` now returns
the identity quaternion when the source rotation basis has no energy. The
green regression remains in the normal unit suite.

## GE-BLOOD-001 — transpose narrows host pointers to `u32`

**Observed:** compiling `blood_decrypt.c` for the 64-bit PC target emits
pointer-size warnings at the `bloodImgTranspose` loop bounds: host pointers are
stored in `u32` and later reconstructed for comparisons.

**Current evidence:** the deterministic transpose test passes with ordinary
heap/stack allocations, because the compared pointers remain in the same low
32-bit region. The test does not yet force an allocation across a 32-bit
address boundary, so this remains a portability risk rather than a claimed
runtime regression.

**Status:** OPEN RISK. Do not mark this fixed without either a pointer-safe
implementation plus the existing behavioral test, or a deterministic failing
test that demonstrates the boundary failure.

## GE-OBJECTIVE-002 — objective status accepted invalid indexes

**Observed:** the text and difficulty helpers indexed the objective table for
negative inputs, and `get_status_of_objective()` relied on the address of a
member through a possibly NULL objective pointer to select the saved-status
fallback. The behavior was undefined at the negative boundary and obscured the
actual empty-slot contract.

**Red test:** `test_objective_text_and_index_boundaries` and
`test_objective_status_boundaries` in `tests/objective_test.cpp`, observed red
before the production guard fix on 2026-09-22.

**Status:** FIXED. All three helpers now require `0 <= index <
OBJECTIVES_MAX`; empty objective slots explicitly return their saved status.
The same test suite drives every objective criterion, status transition, pad
list, HUD message, and photograph viewport edge. `objective_status.c` is gated
at 100% lines and branches.

## GE-LOADMODEL-001 — object reuse read the wrong header byte

**Observed:** `setupFindObjForReuse()` walked the PC-converted prop-definition
stream through a `u32 *` and compared `(u8)cmd[0]` with the requested type. On
the PC layout, that byte is the low byte of `extrascale`; the type is the fourth
header byte. A valid reusable object was therefore skipped, and the real end
sentinel was not recognized unless unrelated adjacent memory happened to make
the low byte match.

**Red test:** `test_object_reuse_reads_the_type_byte` in
`tests/loadobjectmodel_test.cpp`, observed red before the production fix on
2026-09-22.

**Status:** FIXED. The scan now uses `PropDefHeaderRecord *` and reads
`cmd->type`, while retaining the existing converter-matched stride walk. The
module is covered at 83.69% lines / 90.10% branches and is protected by the
80% additional-source coverage gate.

## GE-MODEL-002 — model relocation narrowed host allocation addresses

**Observed:** PC model sidecars store relocatable `0x05xxxxxx` VMAs, but the
loaded sidecar image is a host allocation. `modelPromoteNodeOffsetsToPointers`
and its `sub_GAME_7F075A90` caller accepted that allocation address as `u32` and
performed the relocation delta in `s32`. On a 64-bit host, a stack or heap
allocation above 4 GiB became a low, invalid pointer before the node walk.

**Red test:** `tests/model_pointer_promotion_test.c` uses a high-address host
fixture and checks the root data pointer, header/group pointer, display-list
vertex/base pointers, following-node walk, and switch-array relocation. The
first run failed at `model relocation preserves the full host allocation
pointer` before the production change.

**Status:** FIXED. The PC-only API now carries the loaded address as
`uintptr_t`, uses host-width relocation arithmetic, and passes the full address
from `objecthandler_2.c`; N64 signatures and arithmetic remain unchanged.

The focused model tests are in the unit suite, but `model.c` is not yet an
80%-floor source: the current deterministic measurement is 1.09% because this
slice covers relocation and animation helpers only. It remains a separately
measured follow-up rather than being presented as whole-file coverage.

## Defect regression contract inventory

`tests/defect_contract_test.cpp` is an executable regression list for defects
described in `README.md` and `docs/dev/LEVEL-STATUS.md`. These contracts were
originally red, and are now green after the current branch's production
precondition fixes. They run as ordinary unit regressions and contribute to the
coverage gate.

| ID | Former defect contract | Regression assertion | Source/documentation route |
| --- | --- | --- | --- |
| GE-CONTRACT-001 | Gunshot sound deadline is strict instead of inclusive | FIXED — D240/D241 cadence edge in `test_d240_sound_deadline_is_inclusive` | `src/game/gunfire.c`, README gunshot SFX issue |
| GE-CONTRACT-002 | Water texture-coordinate shift changes at a one-unit boundary | FIXED — D245 seam edge in `test_d245_water_shift_is_continuous` | `src/game/sky.c`, README water issue |
| GE-CONTRACT-003 | Fresh All-Unlocked saves seed only audio fields | FIXED — D259/D281 blank-save defaults in `test_d281_fresh_all_unlocked_save_has_blank_settings` | `port/src/libultra.c`, README All unlocked warning |
| GE-CONTRACT-004 | Fast3D address guard checks only an address range, not a mapped allocation | FIXED — C2 invalid-address cases in `test_level_status_fast3d_pointer_and_gdl_alignment` | `port/fast3d`, LEVEL-STATUS C2 |
| GE-CONTRACT-005 | Model-GDL relocation masks offsets without proving the PC 16-byte stride | FIXED — C2/D80/D82/D83 misaligned offsets in the same test | `src/game/objecthandler_2.c`, LEVEL-STATUS C2 |
| GE-CONTRACT-006 | Text measurement accepts missing font banks when the text pointer is valid | FIXED — D129 NULL-font cases in `test_level_status_text_measure_preconditions` | `src/game/textrelated.c`, LEVEL-STATUS Cuba/D129 |
| GE-CONTRACT-007 | Tile walk result is used without requiring a non-NULL tile | FIXED — C4 cases in `test_level_status_tile_walk_preconditions` | LEVEL-STATUS Depot/C4 |
| GE-CONTRACT-008 | Portal geometry accepts NULL or fewer than three points | FIXED — C5 cases in `test_level_status_portal_preconditions` | LEVEL-STATUS Control/C5 |
| GE-CONTRACT-009 | Model load accepts out-of-range IDs or a NULL model header | FIXED — C6 cases in `test_level_status_model_and_sound_preconditions` | LEVEL-STATUS Surface2/C6 |
| GE-CONTRACT-010 | Sound setup accepts missing key-map or envelope data | FIXED — C7 cases in the same test | LEVEL-STATUS Surface1/C7 |
| GE-CONTRACT-011 | Muzzle-flash alignment helper returns identity for the legacy angle-zero call | FIXED — D303 direction-basis case in `tests/muzzle_flash_contract_test.cpp` | `src/game/gunfire.c`, `src/game/matrixmath.c` |

## Known runtime issues with incomplete automated seams

These rows still require a deterministic renderer fixture, converted level
data, or a bounded live capture before the remaining behavior can be fully
regression-tested.

| ID | Issue to file or cross-link in the companion repo | Current evidence | Missing automated seam |
| --- | --- | --- | --- |
| GE-RUNTIME-D307 | In-level security-camera props can face backwards, observed on Bunker | `tests/cctv_orientation_test.cpp` now pins the Bunker placement-pad/look-pad distinction; the red baseline identified the PC field-selection defect | A bounded Bunker visual capture or renderer fixture proving the corrected prop orientation in-level |
| GE-RUNTIME-D303 | Some muzzle flashes include an erroneous long upward flash | `tests/muzzle_flash_contract_test.cpp` now protects the restored legacy direction basis; display-list submission is still not isolated | A fake gunfire/effect sink that records every submitted flash primitive and checks count, origin, and direction |
| GE-RUNTIME-D308 | Occasional geometry z-fighting | README known issue; depends on depth/render ordering | A deterministic frame/depth submission fixture or captured draw stream with duplicate-surface detection |
| GE-RUNTIME-D311 | Bond's model can briefly render out of place after an animation transition | README known issue; no model-transform lifecycle fixture | A fake animation transition and render-transform sink that checks position continuity across the transition |

The absence of a red unit test for the remaining rows is itself a coverage gap,
not evidence that the runtime issue is resolved. D307 now has a red→green unit
regression at the data/setup seam, but its live visual symptom remains pending.

## GE-CCTV-001 — CCTV setup selected the placement pad instead of the look-at pad

**Red test:** `tests/cctv_orientation_test.cpp` was run against the pre-fix
implementation on 2026-09-22. All four Bunker records failed: the production
access returned placement pads `10018`, `10019`, `10020`, and `10021` instead of
look-at pads `88`, `95`, `80`, and `63`.

**Root cause:** a converted CCTV record has two pad IDs. The inherited
`ObjectRecord::pad` places the physical camera prop; the CCTV tail field stores
the pad that the head should face. `tools_pc/d88_propdefs.py` emits the latter
from N64 word 32 into the PC record tail. The PC portability rename from the
original duplicate `pad` member to `lookpad` was not propagated to
`setupCctv`, so the PC build read the placement field. This is a setup-data
field-selection defect, not a matrix-basis defect and not the player's view
camera.

**Fix:** `cctvGetLookPad` now reads the explicit PC `lookpad` field, and
`setupCctv` uses that value only for target-pad resolution. Placement continues
to use the inherited object pad. The same unit test passes after the change;
the matrix implementation is unchanged.

## Test-oracle corrections (not production defects)

The first execution also exposed three test-fixture problems, which were
corrected without changing production code:

1. Constant `atan2f` calls were being folded to the host libc implementation;
   game-compatible calls now go through a function pointer.
2. The N64 lookup-table trig wrappers approximate the clamped endpoints rather
   than returning mathematical `pi` exactly; boundary tests compare against
   the game function's clamped boundary.
3. `quaternion_7F05BC68` preserves the sign of a negative scalar part and does
   not normalize arbitrary input; its test now asserts that documented legacy
   behavior rather than inventing a unit-length contract.

4. Collision no-hit cases return the public normalized sentinel `1.0`, even
   when the internal endpoint-distance helper returns `FLT_MAX`; geometry
   tests were corrected to assert the public contract.

5. The host `ObjectRecord.runtime_bitflags` field is at `0x6c`, not the N64
   `0x64`, because the PC `bondtypes.h` layout widens embedded pointers. The
   objective/tag fixture now models that host boundary explicitly; the
   production tagged-object lookup was not changed.
