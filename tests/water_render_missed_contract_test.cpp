/*
 * Red contracts for water coverage that the first pass could not exercise
 * through the current single-scene BDD harness.
 *
 * These checks deliberately describe the missing fixture and integration
 * boundaries instead of making the weak "a framebuffer was non-empty" check
 * look like visual proof.  They become green when the renderer exposes real
 * scene fixtures, asset ownership, and measurable water-specific oracles.
 */
#include "water_render_contract_test_support.h"

#include <string_view>

using geWaterContractTest::TestState;
using geWaterContractTest::between;
using geWaterContractTest::contains;
using geWaterContractTest::contains_any;
using geWaterContractTest::count_occurrences;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_water_fixture_matrix(TestState &tests, std::string_view bdd)
{
    tests.expect(contains_any(bdd, {"water_material_split_view",
                                    "water_split_view",
                                    "water_multiplayer"}),
                 "water BDD must render a water surface in split-screen, not only in a single viewport");
    tests.expect(contains(bdd, "players=2 viewports=2") &&
                     contains_any(bdd, {"water_material_split_view",
                                        "water_split_view",
                                        "water_multiplayer"}),
                 "water split-screen fixture must exercise two independent player viewports");
    tests.expect(contains(bdd, "GE_WATER_TEST_SPLIT=1") &&
                     contains(bdd, "WATER_SPLIT_FIXTURE: enabled=1 triangles=2 surfaces=1"),
                 "split-screen water must use an explicit camera-projected fixture when the multiplayer environment has no authored water fan");
    tests.expect(contains_any(bdd, {"water_ship_occlusion",
                                    "water_dam_occlusion",
                                    "water_foreground_occluder"}),
                 "water BDD must include a foreground ship or dam occluder fixture");
    tests.expect(contains_any(bdd, {"WATER_OCCLUDER: type=ship",
                                    "WATER_OCCLUDER: type=dam",
                                    "WATER_OCCLUDER: depth-mask"}),
                 "water occluder fixture must identify the geometry that owns the depth edge");
    tests.expect(contains_any(bdd, {"water_multiple_surfaces",
                                    "water_non_coplanar_surfaces",
                                    "water_surface_batch"}),
                 "water BDD must render more than one non-coplanar water surface");
    tests.expect(contains(bdd, "WATER_SURFACE_BATCH:") &&
                     contains(bdd, "surface_count=") &&
                     contains(bdd, "fixture=non-coplanar"),
                 "multi-surface water fixture must prove surface ownership instead of one global line");
}

static void test_disturbance_fixture_matrix(TestState &tests,
                                            std::string_view bdd,
                                            std::string_view api_header,
                                            std::string_view pc_source)
{
    tests.expect(contains_any(bdd, {"water_boat_wake",
                                    "water_dam_impact",
                                    "water_disturbance_decay"}),
                 "water BDD must exercise a boat wake or dam-impact disturbance");
    tests.expect(contains_any(bdd, {"WATER_DISTURBANCE: event=",
                                    "WATER_WAKE:",
                                    "disturbance_count="}),
                 "water disturbance fixture must expose the injected event batch");
    tests.expect(contains_any(api_header, {"GfxWaterDisturbanceBatch",
                                           "disturbance_count",
                                           "disturbances["}),
                 "water API must expose a bounded disturbance batch to effects");

    const std::string_view ripple_function = between(
        pc_source, "extern \"C\" void gfx_water_ripple",
        "extern \"C\" void gfx_set_render_mode");
    tests.expect(contains_any(ripple_function, {"push_back", "emplace_back",
                                                "disturbance_count",
                                                "g_water_disturbances"}),
                 "water ripple submission must accumulate events instead of overwriting the last event");
    tests.expect(contains_any(ripple_function, {"timestamp", "lifetime",
                                                "age", "decay"}),
                 "water disturbances must have deterministic lifetime or decay semantics");
}

static void test_water_pixel_oracles(TestState &tests, std::string_view bdd)
{
    tests.expect(contains_any(bdd, {"assert_water_pixel_variance",
                                    "luma_variance",
                                    "water_pixel_variance"}),
                 "water pixel validation must reject a flat tint that has no spatial water response");
    tests.expect(contains_any(bdd, {"assert_water_reflection_asymmetry",
                                    "reflection_asymmetry",
                                    "water_reflection_difference"}),
                 "water pixel validation must distinguish asymmetric reflection from a vertical copy");
    tests.expect(contains_any(bdd, {"assert_water_surface_edge_signal",
                                    "surface_edge_signal"}),
                 "live water pixel validation must require visible surface structure without claiming an unmeasured occluder mask");
    tests.expect(contains_any(bdd, {"assert_water_temporal_change",
                                    "temporal_difference",
                                    "water_frame_difference"}),
                 "water pixel validation must prove animation changes across elapsed-time samples");
    tests.expect(contains_any(bdd, {"assert_water_viewport_isolation",
                                    "viewport_pixel_isolation",
                                    "split_viewport_difference"}),
                 "water pixel validation must compare the two split-screen water regions independently");
}

static void test_shader_profile_matrix(TestState &tests, std::string_view bdd,
                                       std::string_view opengl_source)
{
    tests.expect(contains_any(bdd, {"GE_BDD_GLSL_PROFILES",
                                    "glsl_profile_matrix",
                                    "water_shader_profiles"}),
                 "water BDD must run a shader-profile matrix rather than one driver profile");
    tests.expect(contains_any(bdd, {"GLSL120", "glsl120", "legacy_glsl"}) &&
                     contains_any(bdd, {"GLSL130", "glsl130", "modern_glsl"}),
                 "water shader profile matrix must cover legacy and modern desktop GLSL");
    tests.expect(contains_any(bdd, {"GLSL_ES", "glsl_es", "gles_profile"}),
                 "water shader profile matrix must cover the supported GLES path");
    tests.expect(contains(opengl_source, "glGetShaderInfoLog") &&
                     contains(opengl_source, "glGetProgramInfoLog") &&
                     contains_any(opengl_source, {"shader_source_profile",
                                                   "water_shader_profile",
                                                   "profile compile"}),
                 "water shader diagnostics must retain the profile that produced a compile or link failure");
}

static void test_authored_material_contract(TestState &tests,
                                            std::string_view opengl_source,
                                            std::string_view docs)
{
    const std::string_view material_setup = between(
        opengl_source, "static bool gfx_opengl_ensure_water_material_textures",
        "static bool gfx_opengl_ensure_water_scene");
    tests.expect(contains_any(material_setup, {"water_material_asset",
                                               "water_base_path",
                                               "GE_WATER_BASE",
                                               "load_water_material"}),
                 "water material must have an authored asset lookup before using its procedural fallback");
    tests.expect(contains_any(material_setup, {"water_material_fallback_texture",
                                               "fallback_texture",
                                               "fallback_base"}),
                 "water material loading must retain a deterministic fallback when assets are unavailable");
    tests.expect(contains_any(material_setup, {"glGenerateMipmap",
                                               "GL_LINEAR_MIPMAP_LINEAR"}),
                 "water material textures must build and select mip levels for moving detail");
    tests.expect(contains_any(material_setup, {"GL_SRGB8_ALPHA8",
                                               "GL_SRGB",
                                               "water_srgb_decode",
                                               "srgbToLinear"}),
                 "water base colour must declare or explicitly convert its colour space");
    tests.expect(contains_any(material_setup, {"GE_WATER_HEIGHT",
                                               "water_height_asset",
                                               "water_material_height_texture"}),
                 "water material must expose an authored height-map input for vertex displacement");
    tests.expect(contains_any(material_setup, {"height[i + 0] = 128",
                                               "neutral signed height",
                                               "fallback height"}),
                 "water height loading must retain a neutral fallback when assets are unavailable");
    tests.expect(contains_any(docs, {"authored water texture",
                                     "water material asset",
                                     "missing/invalid maps"}),
                 "water documentation must state the authored-asset validation and fallback contract");
}

static void test_camera_and_surface_identity_contract(TestState &tests,
                                                       std::string_view api_header,
                                                       std::string_view pc_source,
                                                       std::string_view composite_shader)
{
    tests.expect(contains_any(api_header, {"surface_id", "surface_index",
                                           "water_surface_id"}),
                 "captured water vertices must identify the owning surface");
    tests.expect(contains_any(api_header, {"surface_plane", "plane_normal",
                                           "water_plane"}),
                 "water camera data must carry a physical plane, not only clip-space vertices");
    tests.expect(contains_any(api_header, {"depth_convention", "reversed_z",
                                           "z_is_from_0_to_1"}) &&
                     contains_any(api_header, {"viewport_x", "viewport_rect",
                                               "source_rect"}),
                 "water camera data must carry depth convention and viewport origin/extent");
    tests.expect(contains_any(pc_source, {"water_surface_id", "surface_index",
                                          "g_water_surface"}),
                 "water capture must preserve surface identity across the composite boundary");
    tests.expect(contains_any(composite_shader, {"uWaterSurfaceId",
                                                 "vSurfaceId",
                                                 "surfaceIndex"}),
                 "water shader must select per-surface plane and disturbance state");
}

static void test_gl_state_guard_contract(TestState &tests,
                                         std::string_view opengl_source)
{
    const std::string_view state = between(
        opengl_source, "struct WaterCompositeGLState",
        "static WaterCompositeGLState gfx_opengl_save_water_state");
    const std::string_view restore = between(
        opengl_source, "static void gfx_opengl_restore_water_state",
        "static bool gfx_opengl_draw_water_composite");
    tests.expect(contains_any(state, {"color_mask", "color_write_mask", "colorMask",
                                      "GL_COLOR_WRITEMASK"}) &&
                     contains_any(restore, {"glColorMask", "color_mask", "color_write_mask",
                                            "colorMask"}),
                 "water state guard must restore the caller color-write mask");
    tests.expect(contains_any(state, {"blend_src_rgb", "blendSrcRgb",
                                      "GL_BLEND_SRC_RGB"}) &&
                     contains_any(state, {"blend_dst_rgb", "blendDstRgb",
                                          "GL_BLEND_DST_RGB"}) &&
                     contains_any(restore, {"glBlendFuncSeparate",
                                            "blend_src_rgb"}),
                 "water state guard must restore separate RGB/alpha blend factors");
    tests.expect(contains_any(state, {"blend_equation_rgb",
                                      "GL_BLEND_EQUATION_RGB"}) &&
                     contains_any(restore, {"glBlendEquationSeparate",
                                            "blend_equation_rgb"}),
                 "water state guard must restore separate RGB/alpha blend equations");
    tests.expect(contains_any(state, {"depth_range", "GL_DEPTH_RANGE"}) &&
                     contains_any(restore, {"glDepthRange", "gfx_opengl_set_depth_range",
                                            "depth_range"}),
                 "water state guard must restore the complete depth range");
    tests.expect(contains_any(state, {"stencil_func", "GL_STENCIL_FUNC"}) &&
                     contains_any(state, {"framebuffer_srgb", "GL_FRAMEBUFFER_SRGB"}) &&
                     contains_any(state, {"polygon_offset_factor",
                                          "GL_POLYGON_OFFSET_FACTOR"}),
                 "water state guard must preserve stencil, sRGB, and polygon-offset state");
    tests.expect(contains_any(state, {"vertex_attrib_pointer",
                                      "GL_VERTEX_ATTRIB_ARRAY_POINTER"}) &&
                     contains_any(state, {"vertex_attrib_type",
                                          "GL_VERTEX_ATTRIB_ARRAY_TYPE"}) &&
                     contains_any(restore, {"glVertexAttribPointer",
                                            "glVertexAttribFormat"}),
                 "water state guard must restore vertex attribute format and pointer state");
}

static void test_shader_numeric_safety_contract(TestState &tests,
                                                std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader, {"isfinite", "isnan",
                                                 "isinf", "safeNormalize"}),
                 "water shader must explicitly reject non-finite camera and wave inputs");
    tests.expect(contains_any(composite_shader, {"safeReflect", "reflectValid",
                                                 "reflectionValid"}) &&
                     contains_any(composite_shader, {"safeRefract", "refractValid",
                                                     "refractionValid"}),
                 "water shader must validate both reflected and refracted rays before projection");
    tests.expect(contains_any(composite_shader, {"totalInternalReflection",
                                                 "refractValid", "k < 0.0"}),
                 "water shader must handle total internal reflection without sampling invalid UVs");
    tests.expect(contains_any(composite_shader, {"safeProjectRay",
                                                 "projectRayValid",
                                                 "ray.z <"}),
                 "water shader must reject rays that point behind the camera");
}

static void test_wave_direction_safety_contract(TestState &tests,
                                                std::string_view composite_shader)
{
    tests.expect(contains(composite_shader, "vec2 safeNormalize2"),
                 "water shader must have a finite fallback for 2D wave directions");
    tests.expect(!contains(composite_shader, "normalize(uWaveDirection)"),
                 "water wave evaluation must not normalize an invalid direction directly");
    tests.expect(count_occurrences(composite_shader,
                                   "safeNormalize2(uWaveDirection)") >= 2,
                 "vertex and fragment wave fields must share safe direction normalization");
}

static void test_wave_parameter_safety_contract(TestState &tests,
                                                std::string_view composite_shader)
{
    tests.expect(contains(composite_shader, "safeWaterScalar"),
                 "water shader must fail closed for non-finite wave parameters");
    tests.expect(count_occurrences(composite_shader,
                                   "safeWaterScalar(uWaveLength") >= 2 &&
                     count_occurrences(composite_shader,
                                       "safeWaterScalar(uWaveAmplitude") >= 2 &&
                     count_occurrences(composite_shader,
                                       "safeWaterScalar(uWaveSpeed") >= 2,
                 "both wave fields must sanitize length, amplitude, and speed");
    tests.expect(count_occurrences(composite_shader,
                                   "safeWaterScalar(time") >= 2 &&
                     count_occurrences(composite_shader,
                                       "safeWaterScalar(scale") >= 2,
                 "both wave fields must sanitize elapsed time and scale");
    tests.expect(contains(composite_shader, "float materialTime =") &&
                     !contains(composite_shader, "vec2(uTime *"),
                 "normal-layer coordinates must use sanitized elapsed time");
}

static void test_pass_boundary_and_resource_lifecycle(TestState &tests,
                                                      std::string_view pc_source,
                                                      std::string_view opengl_source,
                                                      std::string_view bdd)
{
    tests.expect(contains_any(pc_source, {"WATER_CAPTURE: opaque-only",
                                           "water_capture_begin",
                                           "opaque_scene_capture"}) &&
                     contains_any(pc_source, {"WATER_COMPOSITE: phase=",
                                              "water_composite_begin",
                                              "post_world_before_hud"}),
                 "water integration must expose an ordered opaque-capture/composite pass boundary");
    tests.expect(contains_any(bdd, {"water_restart", "water_context_restart",
                                    "water_resize_recreate"}),
                 "water BDD must exercise restart or resize resource recreation");

    const std::string_view release = between(
        opengl_source, "static void gfx_opengl_release_water_resources",
        "static bool gfx_opengl_compile_water_composite_program");
    tests.expect(contains(opengl_source, "gfx_opengl_release_water_resources") &&
                     contains_any(release, {"glDeleteFramebuffers",
                                            "glDeleteTextures",
                                            "water_scene_fbo = 0"}),
                 "water resources must have an explicit context-loss/shutdown release path");
}

static void test_resize_fixture_is_connected(TestState &tests,
                                             std::string_view bdd,
                                             std::string_view video_source)
{
    tests.expect(contains(bdd, "GE_BDD_RESIZE=1") &&
                     contains(bdd, "WATER_RESIZE: resources=invalidated"),
                 "water resize BDD must request and observe a resource invalidation");
    tests.expect(contains(video_source, "GE_BDD_RESIZE") &&
                     contains(video_source, "videoRequestWindowSize"),
                 "the resize BDD switch must drive the existing window-request path");
    tests.expect(contains(video_source, "bdd_resize_requested") &&
                     contains(video_source, "bdd_resize_frame"),
                 "the resize fixture must be one-shot and delayed until an initialized frame exists");
    tests.expect(contains(video_source, "bdd_resize_resources_invalidated") &&
                     contains(video_source, "renderingAPI->on_resize"),
                 "the resize fixture must exercise the renderer invalidation callback even when a compositor keeps the drawable size fixed");
}

static void test_deterministic_bdd_exit(TestState &tests,
                                        std::string_view bdd,
                                        std::string_view libultra_source)
{
    tests.expect(contains(bdd, "GE_BDD_EXIT_AFTER_FRAMES"),
                 "live BDD probes must request an explicit rendered-frame completion boundary");
    tests.expect(contains(libultra_source, "GE_BDD_EXIT_AFTER_FRAMES") &&
                     contains(libultra_source, "g_framesRendered") &&
                     contains(libultra_source, "TEST_FRAME_EXIT"),
                 "the renderer test exit must be tied to the completed frame counter");
}

static void test_msaa_and_hdr_capture_contract(TestState &tests,
                                               std::string_view opengl_source)
{
    const std::string_view draw = between(
        opengl_source, "static bool gfx_opengl_draw_water_composite",
        "static int gfx_opengl_get_max_anisotropy_level");
    tests.expect(contains_any(draw, {"resolve_msaa", "msaa_level",
                                     "water_scene_samples"}),
                 "water capture must resolve or explicitly reject multisample scene inputs");
    tests.expect(contains_any(opengl_source, {"GL_RGBA16F", "GL_RGBA32F",
                                              "hdr_water", "linear_hdr"}),
                 "water scene/reflection targets must define an HDR or explicit linear-light contract");
}

static void test_homogeneous_clipping_contract(TestState &tests,
                                               std::string_view pc_source)
{
    const std::string_view capture = between(
        pc_source, "static void gfx_capture_water_triangle",
        "static void gfx_sp_tri1");
    tests.expect(!contains(capture,
                           "geWaterCompositeVertexIsValid(v1->x, v1->y, v1->z, v1->w)") ||
                     contains_any(capture, {"clip_triangle", "homogeneous_clip",
                                            "clip_interpolate", "clip_polygon"}),
                 "water capture must clip triangles crossing the homogeneous camera plane instead of dropping them");
    tests.expect(!contains(capture,
                           "geWaterCompositeTriangleArea(x0, y0, x1, y1, x2, y2) <= 0.000001f") ||
                     contains_any(capture, {"pixel_area", "viewport_area",
                                            "min_triangle_pixels"}),
                 "water capture must not discard valid distant triangles using a fixed clip-space area threshold");
}

static void test_matrix_inverse_validation_contract(TestState &tests,
                                                    std::string_view pc_source)
{
    const std::string_view inverse = between(
        pc_source, "static bool gfx_matrix_inverse_for_water",
        "static void gfx_sp_matrix");
    tests.expect(contains_any(inverse, {"std::isfinite(input",
                                        "isfinite(augmented",
                                        "geWaterFinite"}),
                 "water camera inversion must reject non-finite matrix input before pivot arithmetic");
    tests.expect(contains_any(inverse, {"std::isfinite(output",
                                        "isfinite(augmented[column]",
                                        "finite_inverse"}),
                 "water camera inversion must validate the output matrix before marking the camera usable");
}

static void test_shader_energy_contract(TestState &tests,
                                        std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader, {"reflectionWeight",
                                                 "transmissionWeight",
                                                 "energyWeight",
                                                 "fresnelWeight"}) &&
                     contains_any(composite_shader, {"1.0 - reflection",
                                                     "weightSum",
                                                     "<= 1.0",
                                                     "min(reflection"}),
                 "water reflection and transmission weights must have an explicit energy-conserving budget");
    tests.expect(contains_any(composite_shader, {"beerLambert",
                                                 "absorptionDepth",
                                                 "exp(-"}) &&
                     contains_any(composite_shader, {"clamp(transmission",
                                                     "transmissionWeight"}),
                 "water absorption must be bounded and included in the transmission weight");
}

static void test_visible_surface_and_reflection_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view composite_shader)
{
    const std::string_view draw = between(
        opengl_source, "static bool gfx_opengl_draw_water_composite",
        "static int gfx_opengl_get_max_anisotropy_level");
    const std::string_view begin = between(
        opengl_source, "static bool gfx_opengl_begin_reflected_camera",
        "static bool gfx_opengl_end_reflected_camera");
    const std::string_view end = between(
        opengl_source, "static bool gfx_opengl_end_reflected_camera",
        "/* The legacy sky water");

    tests.expect(contains(composite_shader, "float waterOpacity") &&
                     contains(composite_shader, "vec4(waterLinearToDisplay(water), waterOpacity)"),
                 "water must expose a bounded alpha/opacity term instead of always outputting an opaque pixel");
    tests.expect(contains(composite_shader, "fract(waveUv * 4.0)") ||
                     contains(composite_shader, "materialDetail"),
                 "water base detail must be sampled at a visible repeat scale instead of becoming a flat low-frequency tint");
    tests.expect(contains(draw, "glEnable(GL_BLEND)") &&
                     contains(draw, "glBlendFuncSeparate(GL_SRC_ALPHA") &&
                     !contains(draw, "glDisable(GL_BLEND)"),
                 "water composite must alpha blend over the captured scene so shallow/refraction colour remains visible");

    tests.expect(contains(composite_shader, "SAMPLE_TEX(uReflection, reflectUv)") &&
                     !contains(composite_shader,
                               "SAMPLE_TEX(uReflection, reflectionAtlasUv(reflectUv))"),
                 "reflection atlas coordinates must be transformed exactly once before sampling");

    const std::size_t foam = composite_shader.find("float foam = clamp");
    const std::size_t foam_end = composite_shader.find(";", foam);
    const std::string_view foam_expression =
        foam == std::string_view::npos || foam_end == std::string_view::npos
            ? std::string_view{}
            : composite_shader.substr(foam, foam_end - foam);
    tests.expect(contains(foam_expression, "edge") &&
                     contains(foam_expression, "proximity") &&
                     contains(foam_expression, "(1.0 - occluded)"),
                 "boat/dam contact foam must combine visible water edges with contact proximity");

    tests.expect(contains(begin, "water_reflection_draw_calls = 0") &&
                     contains(end, "water_reflection_populated = draw_calls > 0") &&
                     contains(end, "return water_reflection_populated"),
                 "reflection validity must require replayed geometry rather than only an allocated and cleared target");
    tests.expect(contains(opengl_source, "GE_WATER_DEBUG_REFLECTION") &&
                     contains(composite_shader, "reflectionDiagnostic"),
                 "water diagnostics must be able to show the raw reflection sample independently of Fresnel/refraction weighting");
}

static void test_shader_failure_recovery_contract(TestState &tests,
                                                  std::string_view opengl_source)
{
    const std::string_view compile = between(
        opengl_source, "const bool uniforms_ready",
        "return uniforms_ready;");
    tests.expect(contains(compile, "if (!uniforms_ready") &&
                     contains(compile, "glDeleteProgram(program)"),
                 "water shader uniform failure must delete the unusable program before returning false");
    tests.expect(contains(compile, "water_composite_program = 0") &&
                     contains_any(compile, {"water_composite_scene_location = -1",
                                            "reset_water_composite_uniforms"}),
                 "water shader uniform failure must leave the next frame able to retry compilation");

    const std::string_view shader_setup = between(
        opengl_source, "GLuint vertex_shader = glCreateShader",
        "GLuint program = glCreateProgram");
    tests.expect(contains(shader_setup, "vertex_shader == 0") &&
                     contains(shader_setup, "fragment_shader == 0"),
                 "water shader object allocation failure must fail closed before submitting source");
    tests.expect(contains(opengl_source, "program == 0") ||
                     contains(opengl_source, "if (program == 0"),
                 "water program allocation failure must be handled before attaching shaders");
}

static void test_shader_source_capacity_contract(TestState &tests,
                                                 std::string_view opengl_source)
{
    tests.expect(contains(opengl_source, "vertex_source_length") &&
                     contains(opengl_source, "fragment_source_length"),
                 "water shader source generation must retain snprintf lengths");
    tests.expect(contains(opengl_source, ">= sizeof(vertex_source)") &&
                     contains(opengl_source, ">= sizeof(fragment_source)"),
                 "water shader source truncation must fail closed before compilation");
}

static void test_capture_allocation_failure_contract(TestState &tests,
                                                     std::string_view opengl_source)
{
    const std::string_view ensure_scene = between(
        opengl_source, "static bool gfx_opengl_ensure_water_scene",
        "struct WaterCompositeGLState");
    tests.expect(contains(ensure_scene, "water_scene_fbo == 0") &&
                     contains_any(ensure_scene, {"water_scene_color == 0",
                                                 "water_scene_depth == 0",
                                                 "water_reflection_fbo == 0"}),
                 "water framebuffer allocation must detect partially failed GL object creation");
    tests.expect(contains_any(ensure_scene, {"glDeleteFramebuffers",
                                             "glDeleteTextures",
                                             "release_water_scene",
                                             "gfx_opengl_release_water_resources"}),
                 "water framebuffer allocation failure must release partially created objects");

    const std::string_view draw = between(
        opengl_source, "static bool gfx_opengl_draw_water_composite",
        "static int gfx_opengl_get_max_anisotropy_level");
    const std::string_view vbo_setup = between(
        draw, "if (water_composite_vbo == 0)",
        "glBindBuffer(GL_ARRAY_BUFFER");
    tests.expect(contains(draw, "glGenBuffers") &&
                     contains_any(vbo_setup, {"glGetError",
                                              "water_composite_vbo == 0) return false",
                                              "GL_OUT_OF_MEMORY"}),
                 "water VBO allocation failure must fail closed before glBufferData");
}

int main()
{
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const std::string bdd = read_text(root / "tests" / "run_pc_bdd.sh");
    const std::string api_header =
        read_text(root / "port" / "fast3d" / "gfx_rendering_api.h");
    const std::string pc_source =
        read_text(root / "port" / "fast3d" / "gfx_pc.cpp");
    const std::string opengl_source =
        read_text(root / "port" / "fast3d" / "gfx_opengl.cpp");
    const std::string video_source =
        read_text(root / "port" / "src" / "video.c");
    const std::string libultra_source =
        read_text(root / "port" / "src" / "libultra.c");
    const std::string docs = read_text(
        root / "docs" / "dev" / "RENDERING-ENHANCEMENTS.md");
    const std::string_view composite_shader = between(
        opengl_source,
        "static bool gfx_opengl_compile_water_composite_program(void)",
        "static bool gfx_opengl_ensure_water_scene");

    TestState tests;
    tests.expect(!bdd.empty(), "missed water contract could not read the BDD harness");
    tests.expect(!api_header.empty(), "missed water contract could not read the renderer API");
    tests.expect(!pc_source.empty(), "missed water contract could not read the PC renderer");
    tests.expect(!opengl_source.empty(), "missed water contract could not read the OpenGL renderer");

    test_water_fixture_matrix(tests, bdd);
    test_disturbance_fixture_matrix(tests, bdd, api_header, pc_source);
    test_water_pixel_oracles(tests, bdd);
    test_shader_profile_matrix(tests, bdd, opengl_source);
    test_authored_material_contract(tests, opengl_source, docs);
    test_camera_and_surface_identity_contract(
        tests, api_header, pc_source, composite_shader);
    test_gl_state_guard_contract(tests, opengl_source);
    test_shader_numeric_safety_contract(tests, composite_shader);
    test_wave_direction_safety_contract(tests, composite_shader);
    test_wave_parameter_safety_contract(tests, composite_shader);
    test_pass_boundary_and_resource_lifecycle(
        tests, pc_source, opengl_source, bdd);
    test_resize_fixture_is_connected(tests, bdd, video_source);
    test_deterministic_bdd_exit(tests, bdd, libultra_source);
    test_msaa_and_hdr_capture_contract(tests, opengl_source);
    test_homogeneous_clipping_contract(tests, pc_source);
    test_matrix_inverse_validation_contract(tests, pc_source);
    test_shader_energy_contract(tests, composite_shader);
    test_visible_surface_and_reflection_contract(
        tests, opengl_source, composite_shader);
    test_shader_failure_recovery_contract(tests, opengl_source);
    test_shader_source_capacity_contract(tests, opengl_source);
    test_capture_allocation_failure_contract(tests, opengl_source);

    if (tests.failures == 0) {
        std::cerr << "water-render missed-coverage contracts passed\n";
        return 0;
    }
    std::cerr << tests.failures
              << " water-render missed-coverage contract(s) are red\n";
    return 1;
}
