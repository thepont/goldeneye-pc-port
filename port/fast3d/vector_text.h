#ifndef GE007_VECTOR_TEXT_H
#define GE007_VECTOR_TEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Port-owned vector UI lifecycle. Commands are queued while the port overlay
 * builds its display list and rendered after the software RSP has flushed,
 * keeping OpenGL state out of the game display-list path. */
void gfx_vector_text_begin_frame(void);
int gfx_vector_text_enabled(void);
int gfx_vector_text_measure(const char *text);
int gfx_vector_text_queue(int x, int y, const char *text, uint32_t colour);
void gfx_vector_text_draw(void);

#ifdef __cplusplus
}
#endif

#endif /* GE007_VECTOR_TEXT_H */
