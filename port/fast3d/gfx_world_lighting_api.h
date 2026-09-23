#ifndef GE007_GFX_WORLD_LIGHTING_API_H
#define GE007_GFX_WORLD_LIGHTING_API_H

#ifdef __cplusplus
extern "C" {
#endif

void gfx_world_lighting_begin_frame(void);
int gfx_world_lighting_add_point_light(float x, float y, float z,
                                       float radius, float intensity,
                                       float r, float g, float b,
                                       int priority);
int gfx_world_lighting_add_spot_light(float x, float y, float z,
                                      float dx, float dy, float dz,
                                      float inner_cos, float outer_cos,
                                      float radius, float intensity,
                                      float r, float g, float b,
                                      int priority);

#ifdef __cplusplus
}
#endif

#endif /* GE007_GFX_WORLD_LIGHTING_API_H */
