/*
 * Red acceptance contracts for the water renderer's known visual gaps.
 *
 * These are intentionally source-level contracts for the first step of the
 * refactor: the current renderer builds its GLSL in gfx_opengl.cpp and does
 * not yet expose a backend-neutral water material/reflection interface that
 * an offscreen pixel test can drive. Once those ports exist, these checks
 * should move behind that interface and become framebuffer integration tests.
 */
#include "gfx_rendering_api.h"
#include "water_render_contract_test_support.h"

#include <string_view>

using geWaterContractTest::TestState;
using geWaterContractTest::between;
using geWaterContractTest::contains;
using geWaterContractTest::contains_any;
using geWaterContractTest::count_occurrences;
using geWaterContractTest::read_text;
namespace fs = geWaterContractTest::fs;

static void test_surface_material_inputs(TestState &tests,
                                         std::string_view composite_shader)
{
    tests.expect(contains(composite_shader, "uniform sampler2D uWaterBase;"),
                 "final water composite must consume a water base/albedo texture");
    tests.expect(contains(composite_shader,
                          "uniform sampler2D uWaterNormal;") ||
                     contains(composite_shader,
                              "uniform sampler2D uWaterDuDv;"),
                 "final water composite must consume animated normal or DuDv detail");
    tests.expect(contains(composite_shader, "uWaterBase") &&
                     contains_any(composite_shader,
                                  {"uWaterNormal", "uWaterDuDv"}),
                 "the final material must use its water surface inputs instead of discarding the legacy texture");
}

static void test_multi_octave_normal_layers(TestState &tests,
                                             std::string_view composite_shader)
{
    const size_t normal_samples = count_occurrences(
        composite_shader, "SAMPLE_TEX(uWaterNormal");
    tests.expect(normal_samples >= 4,
                 "water normal detail must combine four animated bump layers");
    tests.expect(count_occurrences(composite_shader, "normalLayer") >= 2 ||
                     count_occurrences(composite_shader, "detailUv") >= 3,
                 "water bump layers must use distinct scale/phase coordinates");
}

static void test_gpu_gems_height_octaves(TestState &tests,
                                          std::string_view vertex_shader,
                                          std::string_view fragment_shader)
{
    tests.expect(contains(vertex_shader, "sampleHeightBilinear") &&
                     contains(vertex_shader, "uWaterHeightTexel"),
                 "vertex displacement must explicitly filter height-map samples when vertex filtering is unavailable");
    tests.expect(count_occurrences(vertex_shader, "heightLayer") >= 2,
                 "vertex displacement must combine multiple low-frequency height-map octaves");
    tests.expect(contains(fragment_shader, "sampleHeightBilinear") &&
                     contains(fragment_shader, "uWaterHeightTexel"),
                 "fragment normals must use the same filtered height-map source as displacement");
    tests.expect(count_occurrences(fragment_shader, "heightLayer") >= 2,
                 "fragment normals must combine multiple height-map octaves instead of one flat sample");
}

static void test_displaced_geometry_and_surface_data(TestState &tests,
                                                       std::string_view vertex_shader,
                                                       std::string_view api_header)
{
    tests.expect(sizeof(GfxWaterCompositeVertex) > sizeof(float) * 4,
                 "captured water vertices must carry world/UV surface data, not only clip coordinates");
    tests.expect(contains_any(vertex_shader,
                              {"waterHeight(", "uWaterHeight", "uHeightMap",
                               "waterDisplacement"}),
                 "water vertex stage must evaluate a height/displacement source");
    tests.expect(contains_any(vertex_shader,
                              {"aWaterWorldPosition", "waterWorldPosition",
                               "uWaterWorldOrigin"}) ||
                     contains_any(api_header,
                                  {"world_x", "world_position", "water_u",
                                   "water_v"}),
                     "water pipeline must preserve world or material coordinates for a height field");
}

static void test_authored_height_source(TestState &tests,
                                        std::string_view opengl_source,
                                        std::string_view vertex_shader,
                                        std::string_view fragment_shader)
{
    tests.expect(contains_any(vertex_shader,
                              {"uWaterHeight", "WATER_VERTEX_HEIGHT",
                               "authoredHeight"}),
                 "water vertex displacement must accept an authored height source when the profile supports vertex textures");
    tests.expect(contains_any(opengl_source,
                              {"GE_WATER_HEIGHT", "water_material_height_texture",
                               "water_height_texture"}),
                 "water material loading must provide a deterministic height-map input or fallback");
    tests.expect(contains(fragment_shader, "uWaterHeight") &&
                     contains_any(fragment_shader,
                                  {"heightStep", "authoredHeight", "height_dx"}),
                 "water normals must include the same authored height field used for vertex displacement");
}

static void test_true_planar_reflection(TestState &tests,
                                        std::string_view opengl_source,
                                        std::string_view composite_shader,
                                        std::string_view api_header)
{
    tests.expect(contains_any(opengl_source,
                              {"water_reflection_fbo", "water_reflection_texture",
                               "reflection_fbo", "reflection_texture"}),
                 "water renderer must allocate a reflection render target");
    tests.expect(contains_any(composite_shader,
                              {"uniform sampler2D uReflection;",
                               "uniform sampler2D uWaterReflection;"}),
                 "water composite must sample a rendered reflection target");
    tests.expect(contains_any(opengl_source,
                              {"GL_CLIP_DISTANCE", "water_clip_plane",
                               "uWaterClipPlane", "reflectionClipPlane"}) ||
                     contains(api_header, "water_reflection"),
                 "reflection pass must clip the scene against the water plane");
}

static void test_view_space_ray_and_normal_frame(TestState &tests,
                                                  std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"uInverseProjection", "uProjectionInverse",
                               "inverseProjection"}),
                 "water ray must be reconstructed from the active camera projection");
    tests.expect(contains_any(composite_shader,
                              {"viewPosition", "viewPos", "waterViewPosition",
                               "viewRay"}),
                 "water reflect/refract math must use the fragment view position or ray");
    tests.expect(contains_any(composite_shader,
                              {"uWaterNormalMatrix", "uViewMatrix",
                               "waterNormalView"}),
                 "water normals must be transformed into the same frame as the view ray");
    tests.expect(contains(composite_shader, "gl_FragCoord.xy") &&
                     contains_any(composite_shader,
                                  {"uViewport", "uViewportSize",
                                   "uViewportOrigin"}),
                 "projective water UVs must be derived per fragment from the active viewport");
}

static void test_camera_capture_contract(TestState &tests,
                                          std::string_view opengl_source,
                                          std::string_view pc_source,
                                          std::string_view api_header)
{
    tests.expect(sizeof(GfxWaterCompositeCamera) >= sizeof(float) * 18,
                 "water composite camera must carry an inverse projection and depth range");
    tests.expect(contains(api_header, "inverse_projection") &&
                     contains(api_header, "near_plane") &&
                     contains(api_header, "far_plane"),
                 "water camera contract must expose projection and depth parameters");
    tests.expect(contains(opengl_source, "glUniformMatrix4fv") &&
                     contains(opengl_source, "camera->inverse_projection"),
                 "water renderer must upload the captured inverse projection");
    tests.expect(contains(pc_source, "gfx_matrix_inverse_for_water") &&
                     contains(opengl_source, "linearizeDepth"),
                 "water projection capture must feed linear depth reconstruction");
}

static void test_world_camera_and_physical_depth_contract(
    TestState &tests, std::string_view game_source,
    std::string_view pc_source, std::string_view api_header)
{
    const std::string_view capture_function = between(
        pc_source, "static void gfx_capture_water_triangle",
        "static void gfx_sp_tri1");

    tests.expect(!contains(capture_function,
                           "gfx_matrix_inverse_for_water(\n                rsp.P_matrix"),
                 "water camera capture must not invert the singular legacy sky projection");

    const bool carries_physical_surface_data =
        contains_any(api_header, {"surface_depth", "water_surface_depth",
                                  "water_plane", "surface_plane"});
    tests.expect(!contains(game_source, "projf.m[2][2] = 0.0f") ||
                     carries_physical_surface_data,
                 "water composite must carry physical surface depth/plane data when legacy clip depth is synthetic");
}

static void test_world_space_displacement_contract(TestState &tests,
                                                    std::string_view vertex_shader)
{
    tests.expect(!contains(vertex_shader,
                           "displacedPosition.y += waterHeight"),
                 "water displacement must not be a clip-space Y-only warp");
    tests.expect(contains_any(vertex_shader,
                              {"uWaterWorldToClip", "uWaterProjection",
                               "aWaterWorldPosition", "waterWorldPosition"}),
                 "water vertex displacement must operate on a world/surface position before projection");
}

static void test_coherent_wave_field(TestState &tests,
                                     std::string_view vertex_shader,
                                     std::string_view fragment_shader)
{
    tests.expect(contains(vertex_shader, "waterWave(") &&
                     contains(fragment_shader, "waterWave("),
                 "water vertex displacement and fragment normals must call the same wave-field function");
    tests.expect(!contains(vertex_shader, "float waterHeight("),
                 "water must not define a separate vertex-only height function");
}

static void test_linear_depth_and_thickness(TestState &tests,
                                            std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"linearizeDepth(", "uDepthParams",
                               "uNearPlane", "uFarPlane"}),
                 "water contact and refraction must reconstruct linear eye-space depth");
    tests.expect(!contains(composite_shader,
                           "float surfaceDepth = gl_FragCoord.z;"),
                 "water thickness must not compare raw nonlinear device depth");
    tests.expect(contains_any(composite_shader,
                              {"waterThickness", "sceneThickness", "absorption",
                               "uWaterAbsorption"}),
                 "water colour must depend on scene thickness for transmission/absorption");
    tests.expect(contains(composite_shader,
                          "float surfaceDepth = -vDisplacedViewPosition.z;") &&
                     !contains(composite_shader,
                               "max(-vDisplacedViewPosition.z, linearizeDepth"),
                 "water thickness must preserve the water surface depth instead of replacing it with farther scene depth");
}

static void test_surface_lighting_and_local_disturbance_contract(
    TestState &tests, std::string_view composite_shader,
    std::string_view game_source, std::string_view api_header)
{
    tests.expect(contains_any(composite_shader,
                              {"specular", "environment", "uEnvironment",
                               "sunColour", "uSun"}),
                 "water material must expose an explicit specular/environment lighting term");
    tests.expect(contains_any(game_source,
                              {"waterRipple", "water_ripple", "waterWake",
                               "water_wake", "waterDisturbance"}) ||
                     contains(game_source, "gfx_water_material_api.h"),
                 "water game path must expose a boat/dam/local disturbance adapter");
    tests.expect(contains_any(api_header,
                              {"gfx_water_ripple", "GfxWaterDisturbance",
                               "GfxWaterDisturbanceBatch"}),
                     "water pipeline must provide an input for boat/dam/local disturbances");
}

static void test_low_light_visibility_contract(TestState &tests,
                                                std::string_view opengl_source,
                                                std::string_view composite_shader)
{
    tests.expect(contains(opengl_source, "GE_WATER_AMBIENT_SHALLOW_R") &&
                     contains(opengl_source, "GE_WATER_AMBIENT_DEEP_B"),
                 "water visibility policy must be sourced from shared ambient constants");
    tests.expect(contains(composite_shader, "waterVisibilityFloor") &&
                     contains(composite_shader,
                              "max(water, waterVisibilityFloor(waterThickness))"),
                 "water composite must preserve a readable low-light floor without dimming bright reflections");
}

static void test_reflected_camera_and_clip_contract(TestState &tests,
                                                     std::string_view opengl_source,
                                                     std::string_view water_draw,
                                                     std::string_view composite_shader)
{
    tests.expect(contains_any(water_draw,
                              {"reflected_camera", "reflection_camera",
                               "uReflectionViewProjection",
                               "reflection_view_projection"}),
                 "water reflection must render from a reflected camera, not only copy the main color buffer");
    tests.expect(contains_any(water_draw,
                              {"GL_CLIP_DISTANCE", "gl_ClipDistance",
                               "reflection_clip_plane", "water_reflection_clip"}),
                 "reflected geometry must be clipped against the water plane during the reflection pass");
    tests.expect(!contains(water_draw,
                           "glUniform4f(water_composite_clip_plane_location, 0.0f, 0.0f, 1.0f, 100000.0f)"),
                 "water reflection clip policy must not be a constant no-op plane");
    tests.expect(contains(composite_shader, "clipPlaneValid") &&
                     !contains(composite_shader,
                               "dot(vec4(viewPosition, 1.0), uWaterClipPlane)"),
                 "fallback reflection must not reject every camera-facing fragment with a constant view-space z plane");

    const bool target_is_flipped = contains(water_draw, "water_reflection_fbo") &&
                                   contains(water_draw, "water_scene_height, (GLint)water_scene_width, 0");
    const bool shader_mirrors_again =
        contains(composite_shader, "2.0 * line - screenUv.y");
    tests.expect(!(target_is_flipped && shader_mirrors_again),
                 "water reflection must apply only one reflection transform");

    tests.expect(contains(opengl_source, "water_reflection_fbo"),
                 "water reflection integration must retain a dedicated target");
}

static void test_refraction_projection_and_mask_contract(
    TestState &tests, std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"uProjection", "uProjectionMatrix",
                               "uForwardProjection", "projectSurfaceToScreen"}),
                 "water refraction must use the active forward projection when converting rays to screen UVs");
    tests.expect(contains_any(composite_shader,
                              {"uRefractionMask", "refractionMask",
                               "refractedDepth", "sceneDepth(refractUv)"}),
                 "water refraction must reject leakage into foreground geometry with a mask or refracted-depth test");
}

static void test_viewport_framebuffer_mapping_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view composite_shader)
{
    tests.expect(contains_any(opengl_source,
                              {"uSceneViewport", "uFramebufferViewport",
                               "sceneViewport", "framebufferUv"}) &&
                     contains_any(composite_shader,
                                  {"uSceneViewport", "uFramebufferViewport",
                                   "sceneViewport", "framebufferUv"}),
                 "water scene samples must map the active player viewport into the full framebuffer texture");
    tests.expect(count_occurrences(composite_shader, "uSceneViewport") >= 2 &&
                     count_occurrences(composite_shader, "localViewportUv") >= 2,
                 "active scene and player viewport uniforms must remain live in the linked water shader");
}

static void test_tangent_frame_contract(TestState &tests,
                                        std::string_view opengl_source,
                                        std::string_view api_header)
{
    tests.expect(contains_any(api_header,
                              {"tangent", "binormal", "surface_basis",
                               "tangent_frame", "tbn"}),
                 "water explicit UVs must carry a tangent-space surface frame");
    tests.expect(!contains(opengl_source, "identity_normal_matrix"),
                 "water normals must not be transformed with a hard-coded identity matrix");
}

static void test_ripple_and_time_contract(TestState &tests,
                                          std::string_view composite_shader,
                                          std::string_view opengl_source,
                                          std::string_view game_source)
{
    const std::string_view ripple_function = between(
        composite_shader, "vec3 waterRipple", "vec2 projectRayToScreen");
    tests.expect(count_occurrences(ripple_function, "radius * radius") >= 2,
                 "water ripple gradient must include both phase and envelope derivatives");
    tests.expect(!contains(game_source,
                           "gfx_water_ripple(sp274[0].unk20 / 32.0f"),
                 "water ripples must come from object/wake events, not a permanent first-vertex seed");
    tests.expect(!contains(opengl_source,
                           "water_composite_time_location, (float)frame_count"),
                 "water animation time must be elapsed time rather than frame-count dependent");
}

static void test_depth_reconstruction_math_contract(
    TestState &tests, std::string_view composite_shader,
    std::string_view pc_source)
{
    tests.expect(contains(composite_shader, "viewPosition.xyz / viewPosition.w") ||
                     contains(composite_shader, "viewPosition.xyz / safeW") ||
                     contains(composite_shader, "viewPosition.xyz / max(viewPosition.w"),
                 "water depth reconstruction must preserve the signed homogeneous W");
    tests.expect(!contains(composite_shader,
                           "viewPosition.xyz / max(abs(viewPosition.w), 0.0001)"),
                 "water depth reconstruction must not erase the projection W sign");
    tests.expect(!contains(pc_source,
                           "g_water_camera.near_plane = 0.1f"),
                 "water near plane must come from the active camera instead of a hard-coded fallback");
    tests.expect(!contains(pc_source,
                           "g_water_camera.far_plane = 10000.0f"),
                 "water far plane must come from the active camera instead of a hard-coded fallback");
}

static void test_displaced_surface_depth_contract(TestState &tests,
                                                   std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"vDisplacedViewPosition", "vSurfacePosition",
                               "surfaceViewPosition", "waterSurfaceView"}),
                 "water thickness must use the displaced surface position, not the undisplaced fan position");
    tests.expect(!contains(composite_shader,
                           "max(-viewPosition.z, linearizeDepth(screenUv, gl_FragCoord.z))"),
                 "water thickness must not reconstruct surface depth from an unchanged view position");
}

static void test_per_surface_plane_contract(TestState &tests,
                                             std::string_view composite_shader,
                                             std::string_view water_draw)
{
    tests.expect(contains_any(composite_shader,
                              {"vWaterPlane", "vSurfacePlane", "vWaterPlaneNormal",
                               "vPerSurfaceWaterLine"}),
                 "water reflection and refraction must use a per-surface plane instead of one global screen line");
    tests.expect(!contains(water_draw, "float water_line = 0.0f") ||
                     contains_any(water_draw,
                                  {"per_triangle_water_line", "water_line[]",
                                   "surface_plane"}),
                 "water capture must not reduce multiple surfaces to one viewport-wide mirror line");
}

static void test_shared_water_constants_contract(TestState &tests,
                                                 std::string_view opengl_source,
                                                 std::string_view composite_shader)
{
    const bool centralized_contact_policy =
        contains_any(opengl_source,
                     {"uWaterContactStart", "uWaterContactBand",
                      "GE_WATER_CONTACT_START", "GE_WATER_CONTACT_BAND"});
    tests.expect(centralized_contact_policy &&
                     contains_any(composite_shader,
                                  {"uWaterContactStart", "uWaterContactBand",
                                   "waterContactStart", "waterContactBand"}),
                 "water foam thresholds must come from one shared policy instead of duplicated shader literals");
}

static void test_reflection_depth_target_contract(TestState &tests,
                                                  std::string_view opengl_source)
{
    tests.expect(contains_any(opengl_source,
                              {"water_reflection_depth", "reflection_depth",
                               "waterReflectionDepth"}),
                 "the reflected-camera pass must have a depth attachment for depth testing and clipped geometry");
    tests.expect(contains(opengl_source, "GL_DEPTH_ATTACHMENT") &&
                     contains_any(opengl_source,
                                  {"water_reflection_depth", "reflection_depth",
                                   "waterReflectionDepth"}),
                 "reflection depth storage must be attached to the reflection framebuffer");
}

static void test_water_material_filtering_contract(TestState &tests,
                                                   std::string_view opengl_source)
{
    const std::string_view material_setup = between(
        opengl_source, "static bool gfx_opengl_ensure_water_material_textures",
        "static bool gfx_opengl_ensure_water_scene");
    tests.expect(contains_any(material_setup,
                              {"glGenerateMipmap", "GL_LINEAR_MIPMAP_LINEAR",
                               "GL_TEXTURE_MAX_ANISOTROPY"}),
                 "water base and normal textures must provide mip/LOD filtering for moving detail");
}

static void test_wave_parameter_contract(TestState &tests,
                                         std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"uWaveLength", "uWaveAmplitude", "uWaveDirection",
                               "waterWavelength", "waterAmplitude", "waterDirection"}),
                 "water waves must expose wavelength, amplitude, speed, and direction parameters");
    tests.expect(!contains(composite_shader, ") * 19.0") ||
                     contains_any(composite_shader,
                                  {"uWaveLength", "waterWavelength"}),
                 "water wave frequency must not be an unexplained hard-coded literal");
}

static void test_medium_transition_contract(TestState &tests,
                                             std::string_view composite_shader)
{
    tests.expect(contains_any(composite_shader,
                              {"uWaterEta", "uWaterIOR", "uEta",
                               "airToWater", "waterToAir"}),
                 "water refraction must expose both medium indices instead of one fixed eta");
    tests.expect(!contains(composite_shader,
                           "refract(viewRay, waterNormalView, 0.75)"),
                 "water refraction must handle entering and exiting the surface");
    tests.expect(contains_any(composite_shader,
                              {"totalInternalReflection", "refractValid",
                               "eta = dot", "if (cosine > 0.0)"}),
                 "water refraction must explicitly handle backside rays and total internal reflection");
}

static void test_disturbance_capacity_contract(TestState &tests,
                                               std::string_view api_header,
                                               std::string_view pc_source)
{
    tests.expect(contains_any(api_header,
                              {"disturbance_count", "disturbances[",
                               "GfxWaterDisturbanceList", "water_disturbances"}),
                 "water material API must carry more than one disturbance event");
    tests.expect(contains_any(pc_source,
                              {"g_water_disturbances", "water_disturbance_count",
                               "disturbance_count"}),
                 "water backend must preserve multiple wake/impact events per viewport");
}

static void test_water_gl_state_restore_contract(TestState &tests,
                                                 std::string_view opengl_source)
{
    const std::string_view state = between(
        opengl_source, "struct WaterCompositeGLState",
        "static WaterCompositeGLState gfx_opengl_save_water_state");
    tests.expect(contains_any(state, {"color_mask", "colorMask"}) &&
                     contains_any(state, {"blend_src", "blendSrc",
                                          "blend_equation", "blendEquation"}),
                 "water composite state guard must preserve color-write and blend-function state");
    tests.expect(contains_any(state, {"depth_range", "depthRange"}),
                 "water composite state guard must preserve the active depth range");
}

static void test_viewport_copy_scope_contract(TestState &tests,
                                              std::string_view water_draw)
{
    const bool copies_full_frame = contains(
        water_draw,
        "glBlitFramebuffer(0, 0, source.width, source.height");
    tests.expect(!copies_full_frame ||
                     contains_any(water_draw,
                                  {"active_viewport", "viewport[0], viewport[1]",
                                   "viewport.width", "viewport.height"}),
                 "water scene/reflection copies must be scoped to the active player viewport");
}

static void test_camera_failure_contract(TestState &tests,
                                         std::string_view water_draw)
{
    tests.expect(contains(water_draw, "camera == nullptr") &&
                     contains(water_draw, "return false"),
                 "water composite must fail closed when no valid camera projection was captured");
}

static void test_shader_stage_interface_contract(TestState &tests,
                                                  std::string_view opengl_source,
                                                  std::string_view vertex_shader,
                                                  std::string_view fragment_shader)
{
    tests.expect(contains(vertex_shader, "INPUT vec4 aPosition;") &&
                     contains(vertex_shader, "INPUT vec2 aWaterUv;") &&
                     contains(vertex_shader, "INPUT vec3 aViewPosition;") &&
                     contains(vertex_shader, "INPUT vec3 aWorldPosition;"),
                 "water vertex shader must declare every captured vertex stream");
    tests.expect(contains(fragment_shader, "INPUT vec2 vWaterUv;") &&
                     contains(fragment_shader, "INPUT vec3 vViewPosition;") &&
                     contains(fragment_shader, "INPUT vec3 vWorldPosition;"),
                 "water fragment shader must consume every vertex-stage varying");
    tests.expect(contains(vertex_shader, "gl_Position") &&
                     contains(fragment_shader, "outColor"),
                 "water shader stages must have explicit position and color outputs");
    tests.expect(contains(opengl_source, "glGetShaderiv") &&
                     contains(opengl_source, "GL_COMPILE_STATUS") &&
                     contains(opengl_source, "glGetProgramiv") &&
                     contains(opengl_source, "GL_LINK_STATUS"),
                 "water shader compilation must validate both compile and link status");
    tests.expect(contains(opengl_source, "glDeleteShader") &&
                     contains(opengl_source, "glDeleteProgram"),
                 "water shader failures must release temporary and linked objects");
}

static void test_wave_derivative_and_sampling_contract(
    TestState &tests, std::string_view vertex_shader,
    std::string_view fragment_shader, std::string_view opengl_source)
{
    tests.expect(contains_any(fragment_shader,
                              {"dFdx", "dFdy", "analyticDerivative",
                               "waveDerivative", "height_dx"}) &&
                     contains_any(fragment_shader,
                                  {"height_dy", "waveDerivative", "dFdy"}),
                 "water normals must be derived from both surface directions");
    tests.expect(contains_any(vertex_shader,
                              {"uHeightMap", "textureLod", "texture2D",
                               "waterHeightField", "waterWaveField"}),
                 "water displacement must sample or evaluate an authoritative height field");
    tests.expect(contains_any(fragment_shader,
                              {"textureGrad", "textureLod", "fwidth",
                               "uWaterLod", "waterLod"}),
                 "water detail sampling must select a stable LOD instead of aliasing");
    tests.expect(contains_any(vertex_shader,
                              {"radialGrid", "tessellat", "waterMesh",
                               "heightMapVertex"}) ||
                     contains_any(opengl_source,
                                  {"gfx_water_tessellate_mesh", "waterMesh",
                                   "kWaterMeshSubdivisions"}),
                 "GPU-Gems displacement must have sufficiently sampled water geometry");
}

static void test_scene_capture_and_exclusion_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view pc_source, std::string_view water_draw)
{
    const bool captures_color_and_depth =
        contains(water_draw, "GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT") ||
        (contains(water_draw, "water_scene_blit_mask") &&
         contains(water_draw, "GL_COLOR_BUFFER_BIT") &&
         contains(water_draw, "GL_DEPTH_BUFFER_BIT") &&
         contains(water_draw, "scene_depth_available") &&
         contains(water_draw, "glClearDepthf"));
    tests.expect(captures_color_and_depth,
                 "water scene capture must preserve opaque color and depth together");
    tests.expect(contains(pc_source, "g_water_composite_vertices.clear()") &&
                     contains(pc_source, "g_water_camera_valid = false") &&
                     contains(pc_source, "g_water_disturbance_valid = false"),
                 "water capture state must be consumed and reset at the composite boundary");
    tests.expect(contains_any(water_draw,
                              {"exclude_water", "skip_water", "water_excluded",
                               "reflection_water_mask"}),
                 "scene and reflection passes must exclude water to prevent recursive sampling");
    tests.expect(contains(opengl_source, "GL_DEPTH_ATTACHMENT") &&
                     contains_any(water_draw,
                                  {"GL_DEPTH_BUFFER_BIT", "reflection_depth",
                                   "water_reflection_depth"}),
                 "reflection capture must have depth semantics for clipped geometry");
    tests.expect(contains_any(water_draw,
                              {"glCheckFramebufferStatus", "GL_FRAMEBUFFER_COMPLETE",
                               "water_scene_fbo"}),
                 "water capture must validate framebuffer completeness before drawing");
}

static void test_camera_validation_and_depth_convention_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view pc_source, std::string_view api_header,
    std::string_view water_draw)
{
    tests.expect(contains_any(api_header,
                              {"camera_valid", "projection_valid",
                               "depth_convention", "z_is_from_0_to_1",
                               "viewport_x", "viewport_width"}),
                 "water camera data must carry validity, depth convention, and viewport ownership");
    tests.expect(contains(water_draw, "camera == nullptr") &&
                     contains_any(water_draw,
                                  {"return false", "return 0", "goto cleanup"}),
                 "missing water camera data must fail closed before composite uploads");
    tests.expect(contains_any(water_draw,
                              {"isfinite", "geWaterFinite", "near_plane <= 0",
                               "far_plane <= near_plane"}),
                 "water composite must reject malformed near/far and matrix inputs");
    tests.expect(!contains(water_draw, "const GLfloat identity_projection") &&
                     !contains(water_draw, "const GLfloat identity_normal_matrix"),
                 "water camera failure must not silently fall back to identity transforms");
    tests.expect(contains(pc_source, "get_clip_parameters") &&
                     contains(opengl_source, "uDepthParams"),
                 "water depth reconstruction must preserve the backend depth convention");
}

static void test_surface_frame_and_plane_contract(TestState &tests,
                                                    std::string_view api_header,
                                                    std::string_view pc_source,
                                                    std::string_view composite_shader)
{
    tests.expect(contains_any(api_header,
                              {"tangent", "binormal", "surface_basis",
                               "tangent_frame", "surface_plane"}),
                 "water vertices must carry a surface frame and plane for explicit UVs");
    tests.expect(contains_any(pc_source,
                              {"modelview", "world_matrix", "world_position",
                               "surface_plane"}),
                 "water capture must preserve the transform that owns surface coordinates");
    tests.expect(contains_any(composite_shader,
                              {"uWaterPlane", "uSurfacePlane", "vWaterPlane",
                               "surfacePlane"}),
                 "water shader must evaluate reflection and contact against the active plane");
    tests.expect(!contains(composite_shader, "uWaterLine"),
                 "future water surfaces must not depend on one viewport-wide mirror line");
}

static void test_viewport_and_surface_isolation_contract(
    TestState &tests, std::string_view api_header,
    std::string_view pc_source, std::string_view water_draw,
    std::string_view composite_shader)
{
    tests.expect(contains_any(api_header,
                              {"source_rect", "viewport_rect", "framebuffer_rect",
                               "viewport_x", "viewport_y"}),
                 "water API must identify the source rectangle for each player viewport");
    tests.expect(contains_any(pc_source,
                              {"water_viewport", "viewport_index", "player_viewport",
                               "g_water_viewport"}),
                 "water capture state must be isolated per active player viewport");
    tests.expect(contains_any(water_draw,
                              {"source.left", "source.x", "viewport[0]",
                               "active_viewport", "source_rect"}),
                 "water framebuffer copies must use the active viewport rectangle");
    tests.expect(contains_any(composite_shader,
                              {"uSceneViewport", "uFramebufferViewport",
                               "framebufferUv", "sourceRect"}),
                 "water sampling must map local viewport coordinates into source textures");
    tests.expect(contains_any(api_header,
                              {"surfaces[", "surface_count", "water_surface_count"}),
                 "water API must represent more than one non-coplanar surface");
}

static void test_disturbance_and_time_lifecycle_contract(
    TestState &tests, std::string_view api_header,
    std::string_view pc_source, std::string_view opengl_source,
    std::string_view composite_shader)
{
    tests.expect(contains_any(api_header,
                              {"disturbance_count", "disturbances[",
                               "GfxWaterDisturbanceList", "water_disturbances"}),
                 "water API must support multiple bounded disturbance events");
    tests.expect(contains_any(pc_source,
                              {"disturbance_count", "g_water_disturbances",
                               "g_water_disturbance_count"}),
                 "water backend must preserve a disturbance batch instead of one event");
    tests.expect(contains_any(pc_source,
                              {"elapsed_time", "delta_time", "water_time",
                               "monotonic_time"}) &&
                     !contains(opengl_source,
                               "water_composite_time_location, (float)frame_count"),
                 "water animation must use deterministic elapsed time rather than frame count");
    tests.expect(contains_any(pc_source,
                              {"water_disturbance_capacity", "overflow", "drop_oldest",
                               "priority"}),
                 "water disturbance overflow must be bounded and deterministic");
    tests.expect(contains_any(composite_shader,
                              {"disturbance_count", "for (int disturbance",
                               "waterDisturbanceList"}),
                 "water shader must accumulate the active disturbance batch");
    tests.expect(contains_any(pc_source,
                              {"level_restart", "reset_water", "water_reset",
                               "context_destroy"}),
                 "water transient state must reset on restart and context teardown");
}

static void test_complete_state_guard_contract(TestState &tests,
                                                std::string_view opengl_source)
{
    const std::string_view state = between(
        opengl_source, "struct WaterCompositeGLState",
        "static WaterCompositeGLState gfx_opengl_save_water_state");
    const std::string_view restore = between(
        opengl_source, "static void gfx_opengl_restore_water_state",
        "static bool gfx_opengl_draw_water_composite");
    tests.expect(contains_any(state, {"color_mask", "colorMask"}) &&
                     contains_any(state, {"blend_src", "blendSrc"}) &&
                     contains_any(state, {"blend_equation", "blendEquation"}),
                 "water state guard must preserve color-write and blend equations");
    tests.expect(contains_any(state, {"depth_range", "depthRange"}) &&
                     contains_any(restore, {"glDepthRange", "depth_range"}),
                 "water state guard must preserve the active depth range");
    tests.expect(contains_any(state, {"stencil", "framebuffer_srgb",
                                      "polygon_offset", "color_mask"}),
                 "water state guard must preserve less-common fixed-function state");
    tests.expect(contains_any(state, {"vertex_attrib_pointer",
                                      "vertex_attrib_format"}) &&
                     contains_any(restore, {"glVertexAttribPointer",
                                             "vertex_attrib_format"}),
                 "water state guard must restore vertex attribute formats and bindings");
    tests.expect(contains(state, "texture5") && contains(restore, "GL_TEXTURE5"),
                 "water state guard must restore the height-map texture unit");
    tests.expect(contains(state, "texture6") && contains(restore, "GL_TEXTURE6"),
                 "water state guard must restore the dynamic heightfield texture unit");
    tests.expect(contains(restore, "if (glBlendFuncSeparate != nullptr)") &&
                     contains(restore, "if (glBlendEquationSeparate != nullptr)"),
                 "water state restoration must tolerate optional GLES blend entrypoints");
}

static void test_blit_capability_guard(TestState &tests,
                                       std::string_view water_draw)
{
    tests.expect(contains(water_draw, "if (glBlitFramebuffer == nullptr)"),
                 "water composite must fail closed before any capture when blit is unavailable");
    tests.expect(contains(water_draw, "reflection_color_filter") &&
                     contains(water_draw, "source.msaa_level") &&
                     contains(water_draw, "source.msaa_level > 1 ? GL_NEAREST : GL_LINEAR") &&
                     contains(water_draw, "GL_COLOR_BUFFER_BIT, reflection_color_filter"),
                 "MSAA reflection fallback must use a nearest filter when required by the source");
}

static void test_resource_resize_and_failure_atomicity_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view water_draw)
{
    const std::string_view ensure_scene = between(
        opengl_source, "static bool gfx_opengl_ensure_water_scene",
        "struct WaterCompositeGLState");
    tests.expect(contains(ensure_scene, "width == 0") &&
                     contains(ensure_scene, "height == 0"),
                 "zero-sized water targets must fail before allocation");
    tests.expect(contains(ensure_scene, "water_scene_width == width") &&
                     contains(ensure_scene, "water_scene_height == height"),
                 "water targets must be reused when their size is unchanged");
    tests.expect(contains(ensure_scene, "glCheckFramebufferStatus") &&
                     contains(ensure_scene, "GL_FRAMEBUFFER_COMPLETE"),
                 "water target allocation must reject incomplete framebuffers");
    tests.expect(contains(water_draw, "gfx_opengl_restore_water_state(saved)"),
                 "water allocation and shader failures must restore caller state");
    const std::string_view resize = between(
        opengl_source, "static void gfx_opengl_on_resize",
        "static void gfx_opengl_start_frame");
    tests.expect(contains_any(resize,
                              {"water_scene_width = 0", "water_scene_height = 0",
                               "glDeleteFramebuffers", "glDeleteTextures"}),
                 "water resources must be invalidated or released on resize and shutdown");
}

static void test_gles_framebuffer_portability_contract(
    TestState &tests, std::string_view opengl_source,
    std::string_view state_guard_source, std::string_view postfx_source)
{
    tests.expect(contains(opengl_source, "gl_es ? GL_RGB8 : GL_RGBA8") &&
                     contains(opengl_source, "gl_es ? GL_RGB : GL_RGBA"),
                 "GLES water targets must match the RGB scene attachment format used by the game framebuffer");
    tests.expect(contains(opengl_source, "gl_es ? GL_UNSIGNED_INT : GL_FLOAT") &&
                     contains(opengl_source, "GL_DEPTH_COMPONENT24"),
                 "GLES depth textures must use a valid integer upload type for DEPTH_COMPONENT24");
    tests.expect(contains(opengl_source, "water_scene_blit_mask") &&
                     contains(opengl_source, "scene_depth_available") &&
                     contains(opengl_source, "!scene_depth_available") &&
                     contains(opengl_source, "glClearDepthf"),
                 "GLES and color-only water captures must avoid incompatible depth blits and fail closed to far depth");
    tests.expect(contains(state_guard_source, "glDrawBuffers") &&
                     contains(state_guard_source, "GL_DRAW_BUFFER0") &&
                     contains(state_guard_source, "geGlSetDrawBuffer"),
                 "shared GL state guards must use the GLES draw-buffer API");
    tests.expect(contains(postfx_source, "geGlSetDrawBuffer") &&
                     contains(postfx_source, "glDrawBuffers"),
                 "PostFX must share the GLES-safe draw-buffer boundary instead of poisoning water state");
}

static void test_capture_overflow_and_geometry_contract(
    TestState &tests, std::string_view pc_source)
{
    const std::string_view capture_function = between(
        pc_source, "static void gfx_capture_water_triangle",
        "static void gfx_sp_tri1");
    tests.expect(contains(capture_function, "kMaxWaterCompositeVertices") &&
                     contains_any(capture_function,
                                  {"overflow", "GE_WATER_TRACE", "reported"}),
                 "water capture overflow must be bounded and observable");
    tests.expect(contains(capture_function, "triangle_count") ||
                     contains(capture_function, "surface_plane"),
                 "water capture must retain topology or per-surface ownership");
    tests.expect(contains(capture_function, "world_x") &&
                     contains(capture_function, "view_x"),
                 "captured water vertices must retain both world and view positions");
}

static void test_bdd_acceptance_contract(TestState &tests,
                                         std::string_view bdd_source)
{
    tests.expect(contains(bdd_source, "water_material_original") &&
                     contains(bdd_source, "water_material_enhanced") &&
                     contains(bdd_source, "water_material_remaster"),
                 "BDD must exercise original, enhanced, and remaster water modes");
    tests.expect(contains(bdd_source, "assert_absent") &&
                     contains(bdd_source, "WATER_COMPOSITE_SHADER"),
                 "BDD must fail when the water shader reports a compile or link error");
    tests.expect(contains(bdd_source, "WATER_COMPOSITE_GEOMETRY") &&
                     contains(bdd_source, "WATER_COMPOSITE:"),
                 "BDD must observe captured water geometry and the composite boundary");
    tests.expect(contains_any(bdd_source,
                              {"GE_PCDUMP", "glReadPixels", "readback",
                               "framebuffer pixel"}),
                 "water BDD must include a framebuffer readback assertion, not only trace text");
    tests.expect(contains(bdd_source, "assert_split_render_viewports"),
                 "BDD must cover split-screen viewport isolation around water");
}

static void test_capability_scope_contract(TestState &tests,
                                           std::string_view docs,
                                           std::string_view pc_source)
{
    tests.expect(contains(docs, "reflected-camera replay") &&
                     contains(docs, "water-plane clip") &&
                     contains(docs, "screen-space fallback"),
                 "water documentation must distinguish the reflected pass from its screen-space fallback");
    tests.expect(contains_any(pc_source,
                              {"reflection=reflected-camera",
                               "planar_reflection=1",
                               "clip=water-plane"}),
                 "water diagnostics must identify the reflected-camera capability tier");
    tests.expect(!contains(pc_source,
                           "reflection=screen-space planar_reflection=0"),
                 "water diagnostics must not claim planar reflection from a color-buffer flip");
}

int main()
{
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const std::string opengl_source =
        read_text(root / "port" / "fast3d" / "gfx_opengl.cpp");
    const std::string pc_source =
        read_text(root / "port" / "fast3d" / "gfx_pc.cpp");
    const std::string api_header =
        read_text(root / "port" / "fast3d" / "gfx_rendering_api.h");
    const std::string state_guard_source = read_text(
        root / "port" / "fast3d" / "gfx_gl_state_guard.h");
    const std::string postfx_source = read_text(
        root / "port" / "src" / "ascension_postfx.cpp");
    const std::string game_source =
        read_text(root / "src" / "game" / "sky.c") +
        read_text(root / "src" / "game" / "lv.c");
    const std::string bdd_source = read_text(root / "tests" / "run_pc_bdd.sh");
    const std::string rendering_docs = read_text(
        root / "docs" / "dev" / "RENDERING-ENHANCEMENTS.md");
    const std::string_view composite_shader = between(
        opengl_source,
        "static bool gfx_opengl_compile_water_composite_program(void)",
        "static bool gfx_opengl_ensure_water_scene");
    const std::string_view vertex_shader = between(
        composite_shader,
        "snprintf(vertex_source, sizeof(vertex_source),",
        "snprintf(fragment_source, sizeof(fragment_source),");
    const std::string_view fragment_shader = between(
        composite_shader,
        "snprintf(fragment_source, sizeof(fragment_source),",
        "GLuint vertex_shader");
    const std::string_view water_draw = between(
        opengl_source,
        "static bool gfx_opengl_draw_water_composite",
        "static int gfx_opengl_get_max_anisotropy_level");

    TestState tests;
    tests.expect(!opengl_source.empty(),
                 "water quality contract could not read the OpenGL renderer source");
    tests.expect(!composite_shader.empty(),
                 "water quality contract could not locate the composite shader builder");
    test_surface_material_inputs(tests, composite_shader);
    test_multi_octave_normal_layers(tests, composite_shader);
    test_gpu_gems_height_octaves(tests, vertex_shader, fragment_shader);
    test_displaced_geometry_and_surface_data(tests, vertex_shader, api_header);
    test_authored_height_source(
        tests, opengl_source, vertex_shader, fragment_shader);
    test_true_planar_reflection(tests, opengl_source, composite_shader, api_header);
    test_view_space_ray_and_normal_frame(tests, composite_shader);
    test_camera_capture_contract(tests, opengl_source, pc_source, api_header);
    test_world_camera_and_physical_depth_contract(
        tests, game_source, pc_source, api_header);
    test_world_space_displacement_contract(tests, vertex_shader);
    test_coherent_wave_field(tests, vertex_shader, fragment_shader);
    test_linear_depth_and_thickness(tests, composite_shader);
    test_surface_lighting_and_local_disturbance_contract(
        tests, composite_shader, game_source, api_header);
    test_low_light_visibility_contract(tests, opengl_source, composite_shader);
    test_reflected_camera_and_clip_contract(
        tests, opengl_source, water_draw, composite_shader);
    test_refraction_projection_and_mask_contract(tests, composite_shader);
    test_viewport_framebuffer_mapping_contract(
        tests, opengl_source, composite_shader);
    test_tangent_frame_contract(tests, opengl_source, api_header);
    test_ripple_and_time_contract(
        tests, composite_shader, opengl_source, game_source);
    test_depth_reconstruction_math_contract(
        tests, composite_shader, pc_source);
    test_displaced_surface_depth_contract(tests, composite_shader);
    test_per_surface_plane_contract(tests, composite_shader, water_draw);
    test_shared_water_constants_contract(
        tests, opengl_source, composite_shader);
    test_reflection_depth_target_contract(tests, opengl_source);
    test_water_material_filtering_contract(tests, opengl_source);
    test_wave_parameter_contract(tests, composite_shader);
    test_medium_transition_contract(tests, composite_shader);
    test_disturbance_capacity_contract(tests, api_header, pc_source);
    test_water_gl_state_restore_contract(tests, opengl_source);
    test_viewport_copy_scope_contract(tests, water_draw);
    test_camera_failure_contract(tests, water_draw);
    test_shader_stage_interface_contract(
        tests, opengl_source, vertex_shader, fragment_shader);
    test_wave_derivative_and_sampling_contract(
        tests, vertex_shader, fragment_shader, opengl_source);
    test_scene_capture_and_exclusion_contract(
        tests, opengl_source, pc_source, water_draw);
    test_camera_validation_and_depth_convention_contract(
        tests, opengl_source, pc_source, api_header, water_draw);
    test_surface_frame_and_plane_contract(
        tests, api_header, pc_source, composite_shader);
    test_viewport_and_surface_isolation_contract(
        tests, api_header, pc_source, water_draw, composite_shader);
    test_disturbance_and_time_lifecycle_contract(
        tests, api_header, pc_source, opengl_source, composite_shader);
    test_complete_state_guard_contract(tests, opengl_source);
    test_blit_capability_guard(tests, water_draw);
    test_resource_resize_and_failure_atomicity_contract(
        tests, opengl_source, water_draw);
    test_gles_framebuffer_portability_contract(
        tests, opengl_source, state_guard_source, postfx_source);
    test_capture_overflow_and_geometry_contract(tests, pc_source);
    test_bdd_acceptance_contract(tests, bdd_source);
    test_capability_scope_contract(tests, rendering_docs, pc_source);

    if (tests.failures == 0) {
        std::cerr << "water-render quality contracts passed\n";
        return 0;
    }
    std::cerr << tests.failures
              << " water-render quality contract(s) are red\n";
    return 1;
}
