#ifndef GE007_PORT_WORLD_LIGHTING_H
#define GE007_PORT_WORLD_LIGHTING_H

#ifdef __cplusplus
extern "C" {
#endif

/* Samples game-owned transient effects and submits them to the port-owned
 * renderer registry. This is intentionally a read-only adapter: no game
 * state, display list, or N64 behavior is changed. */
void portWorldLightingSubmitEffects(void);

#ifdef __cplusplus
}
#endif

#endif /* GE007_PORT_WORLD_LIGHTING_H */
