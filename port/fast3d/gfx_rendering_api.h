#ifndef GFX_RENDERING_API_H
#define GFX_RENDERING_API_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct ShaderProgram;

/* Post-world water geometry captured by the software RSP. Coordinates are
 * already in the same homogeneous clip space that the backend consumes for
 * ordinary Fast3D triangles. The additional streams keep the water material
 * from having to reconstruct all of its inputs from screen position alone. */
struct GfxWaterCompositeVertex {
    float x, y, z, w;
    float water_u, water_v;
    float view_x, view_y, view_z;
    float world_x, world_y, world_z;
    float surface_plane[4];
    float view_plane[4];
    float tangent[3];
    float bitangent[3];
    float surface_id;
};

/* A material-space ripple event. Game-side callers can submit boat wakes,
 * dam impacts, or other local disturbances without knowing the backend's
 * shader/uniform layout. */
struct GfxWaterDisturbance {
    float water_u, water_v;
    float radius, strength;
};

#define GFX_WATER_MAX_DISTURBANCES 8
#define GFX_WATER_MAX_SURFACES 8

/* Bounded per-viewport wake/impact input. Effects submit events through the
 * front-end API; the backend receives one immutable batch for the composite,
 * avoiding the old last-write-wins ripple slot. */
struct GfxWaterDisturbanceBatch {
    uint32_t count;
    struct GfxWaterDisturbance disturbances[GFX_WATER_MAX_DISTURBANCES];
};

/* Immutable view of the renderer-independent wave field. The frontend owns
 * the storage for the duration of draw_water_composite; the backend may upload
 * it to a GPU texture but must not retain these pointers after the call. */
struct GfxWaterHeightfieldView {
    uint32_t width;
    uint32_t height;
    const float *heights;
    const float *normal_x;
    const float *normal_z;
    float max_height;
};

struct GfxWaterSurface {
    uint32_t surface_id;
    /* The material plane is retained in the authored surface frame. The
     * view-space copy is used only by the reflected-camera pass, where the
     * clip decision must be made in the same space as the captured camera. */
    float plane[4];
    float view_plane[4];
};

/* Projection data captured with the water fan. The matrix is laid out for
 * glUniformMatrix4fv (column-major) and is the inverse of the active RSP
 * projection, so depth reconstruction is tied to the same camera that made
 * the vertices. */
struct GfxWaterCompositeCamera {
    float inverse_projection[16];
    float world_to_clip[16];
    float world_to_view[16];
    float forward_projection[16];
    float normal_matrix[9];
    uint32_t surface_count;
    struct GfxWaterSurface surfaces[GFX_WATER_MAX_SURFACES];
    uint32_t reflection_surface_mask;
    uint32_t reflection_atlas_columns;
    uint32_t reflection_atlas_rows;
    float near_plane, far_plane;
    int32_t depth_convention;
    int32_t invert_y;
    int32_t viewport_x, viewport_y;
    uint32_t viewport_width, viewport_height;
};

struct GfxClipParameters {
    bool z_is_from_0_to_1;
    bool invert_y;
};

enum FilteringMode { FILTER_NONE, FILTER_LINEAR, FILTER_THREE_POINT };
enum MipmapFilteringMode { MIPMAP_DISABLED, MIPMAP_NEAREST, MIPMAP_LINEAR };

struct GfxRenderingAPI {
    const char* (*get_name)(void);
    int (*get_max_texture_size)(void);
    struct GfxClipParameters (*get_clip_parameters)(void);
    void (*unload_shader)(struct ShaderProgram* old_prg);
    void (*load_shader)(struct ShaderProgram* new_prg);
    struct ShaderProgram* (*create_and_load_new_shader)(uint64_t shader_id0, uint32_t shader_id1);
    struct ShaderProgram* (*lookup_shader)(uint64_t shader_id0, uint32_t shader_id1);
    void (*shader_get_info)(struct ShaderProgram* prg, uint8_t* num_inputs, bool used_textures[2]);
    void (*clear_shaders)(void);
    uint32_t (*new_texture)(void);
    void (*select_texture)(int tile, uint32_t texture_id, bool linear_filter);
    void (*upload_texture)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps);
    void (*upload_bump_texture)(const uint8_t* rgba32_buf, uint32_t width, uint32_t height, bool gen_mipmaps);
    /* Bind the optional ROM-derived normal map on the auxiliary texture unit
     * and tell the active shader whether it should sample it. */
    void (*set_bump_texture)(uint32_t texture_id, bool enabled);
    void (*set_sampler_parameters)(int sampler, bool linear_filter, uint32_t cms, uint32_t cmt, bool mipmaps);
    void (*set_depth_mode)(bool depth_test, bool depth_update, bool depth_compare, bool depth_source_prim, uint16_t zmode);
    void (*set_depth_range)(float znear, float zfar);
    void (*set_viewport)(int x, int y, int width, int height);
    void (*set_scissor)(int x, int y, int width, int height);
    void (*set_use_alpha)(bool use_alpha, bool modulate);
    void (*draw_triangles)(float buf_vbo[], size_t buf_vbo_len, size_t buf_vbo_num_tris);
    void (*init)(void);
    void (*on_resize)(void);
    void (*start_frame)(void);
    void (*end_frame)(void);
    void (*finish_render)(void);
    int (*create_framebuffer)();
    void (*update_framebuffer_parameters)(int fb_id, uint32_t width, uint32_t height, uint32_t msaa_level,
                                          bool opengl_invert_y, bool render_target, bool has_depth_buffer,
                                          bool can_extract_depth);
    bool (*start_draw_to_framebuffer)(int fb_id, float noise_scale);
    void (*copy_framebuffer)(int fb_dst, int fb_src, int left, int top, bool flip_y, bool use_back);
    void (*clear_framebuffer)(bool clear_color, bool clear_depth);
    void (*resolve_msaa_color_buffer)(int fb_id_target, int fb_id_source);
    void* (*get_framebuffer_texture_id)(int fb_id);
    void (*select_texture_fb)(int fb_id);
    void (*delete_texture)(uint32_t texID);
    void (*set_texture_filter)(enum FilteringMode mode);
    enum FilteringMode (*get_texture_filter)(void);
    void (*set_mipmap_filter)(enum MipmapFilteringMode mode);
    void (*set_anisotropy_level)(int);
	int (*get_max_anisotropy_level)(void);
    bool (*draw_water_composite)(const struct GfxWaterCompositeVertex *vertices,
                                 size_t vertex_count, int render_tier,
                                 const struct GfxWaterDisturbanceBatch *disturbances,
                                 const struct GfxWaterHeightfieldView *heightfield,
                                 const struct GfxWaterCompositeCamera *camera,
                                 float elapsed_time);
    /* Render the already-submitted scene segment through a reflected camera
     * into the private planar target. The front end owns display-list replay;
     * the backend owns target setup and restoration of graphics state. Each
     * surface gets a bounded atlas tile so non-coplanar water cannot sample
     * the first surface's reflection camera. */
    bool (*begin_water_reflection)(const struct GfxWaterCompositeCamera *camera,
                                   const float view_plane[4],
                                   uint32_t surface_index,
                                   uint32_t surface_count);
    bool (*end_water_reflection)(void);
    bool (*capture_postfx_depth)(void);
    bool (*get_postfx_depth_texture)(uintptr_t *texture,
                                     uint32_t *width,
                                     uint32_t *height);
    void (*destroy)(void);
};

#endif
