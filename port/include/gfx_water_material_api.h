#ifndef GE007_GFX_WATER_MATERIAL_API_H
#define GE007_GFX_WATER_MATERIAL_API_H

#include <PR/gbi.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Emit a port-only state command into the display list. The game-side C
 * caller does not need the full fast3d API, and the state follows command
 * execution rather than display-list construction. */
Gfx *gfx_emit_water_material(Gfx *gdl, int on);

/* Mark the first command in the current player viewport's scene segment.
 * The PC renderer can replay this bounded range through a reflected camera
 * immediately before the water composite without making game code own a
 * second scene display list. */
Gfx *gfx_emit_water_scene_begin(Gfx *gdl);

/* Submit the authored water plane after opaque level geometry. Enhanced
 * rendering consumes this physical world surface; Original mode keeps the
 * legacy sky fan below. */
Gfx *gfx_emit_world_water_surface(Gfx *gdl, int stage_id, float water_height);

/* Keep the legacy sky-water fan and the modern world-surface path mutually
 * exclusive. This is a renderer policy query, not game logic. */
int gfx_water_world_surface_enabled(void);

/* Mark the post-world boundary for the captured water surface. The command
 * is intentionally separate from the material toggle: the first pass keeps
 * the original ordering, while this pass runs after ships/dams and before
 * the HUD so scene colour/depth can be used for reflection, refraction, and
 * contact foam. */
Gfx *gfx_emit_water_composite(Gfx *gdl);

/* Submit one material-space ripple for the next water composite. The port
 * owns the bounded per-frame storage; game-side effects only provide the
 * disturbance location and scale. */
void gfx_water_ripple(float water_u, float water_v, float radius, float strength);

/* Submit a moving-boat wake through the same bounded disturbance adapter. A
 * stationary boat is deliberately a no-op; ambient waves and one-shot dam or
 * impact events use gfx_water_ripple instead. */
void gfx_water_boat_wake(float water_u, float water_v, float heading_u,
                         float heading_v, float speed, float beam);

/* Gameplay adapter for moving world-space boats. The PC renderer owns the
 * world-to-periodic-water mapping so game objects do not duplicate UV or
 * heading conventions. */
void gfx_water_boat_wake_world(float world_x, float world_z,
                               float heading_radians, float speed,
                               float beam);

#ifdef __cplusplus
}
#endif

#endif /* GE007_GFX_WATER_MATERIAL_API_H */
