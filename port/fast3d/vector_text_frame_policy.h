#ifndef GE007_VECTOR_TEXT_FRAME_POLICY_H
#define GE007_VECTOR_TEXT_FRAME_POLICY_H

#include <stdint.h>

/* The game thread records vector text while it builds a display list and the
 * scheduler thread consumes that text after the list has been rendered. The
 * two batches must therefore have an explicit hand-off: beginning a render
 * task takes the commands that were pending before it, while commands
 * recorded concurrently remain pending for the next task. */
typedef struct GeVectorTextFrameState {
    uint32_t pending_count;
    uint32_t render_count;
} GeVectorTextFrameState;

typedef struct GeVectorTextBuildState {
    uint32_t build_count;
    int building;
} GeVectorTextBuildState;

static inline int geVectorTextCanRender(int render_mode,
                                         int enhanced_mode,
                                         int vector_ready,
                                         int atlas_ready)
{
    return render_mode >= enhanced_mode && vector_ready != 0 &&
           atlas_ready != 0;
}

/* A display list can be submitted one scheduler tick before the game has
 * built the next list. Reusing one known-good batch across that empty handoff
 * prevents a steady HUD/menu from blinking out for a single frame. The
 * caller owns the one-frame budget and must clear it after this decision. */
static inline int geVectorTextFrameShouldReuseLast(uint32_t render_count,
                                                    uint32_t last_count,
                                                    int reuse_available)
{
    return reuse_available != 0 && render_count == 0 && last_count != 0;
}

static inline void geVectorTextBuildBegin(GeVectorTextBuildState *state)
{
    if (state == 0) {
        return;
    }
    state->build_count = 0;
    state->building = 1;
}

static inline int geVectorTextBuildQueue(GeVectorTextBuildState *state,
                                         uint32_t capacity)
{
    if (state == 0 || state->building == 0 || capacity == 0 ||
        state->build_count >= capacity) {
        return 0;
    }
    ++state->build_count;
    return 1;
}

static inline uint32_t geVectorTextBuildPublish(GeVectorTextBuildState *state)
{
    uint32_t published_count;

    if (state == 0 || state->building == 0) {
        return 0;
    }
    published_count = state->build_count;
    state->build_count = 0;
    state->building = 0;
    return published_count;
}

static inline int geVectorTextFrameQueue(GeVectorTextFrameState *state,
                                          uint32_t capacity)
{
    if (state == 0 || capacity == 0 || state->pending_count >= capacity) {
        return 0;
    }
    ++state->pending_count;
    return 1;
}

/* Port-owned overlays are emitted after the game display list has been
 * published but before the current frame is drawn. They must append to the
 * current render batch, not to the pending producer batch (which the next
 * game build is allowed to replace). */
static inline int geVectorTextFrameQueueCurrent(GeVectorTextFrameState *state,
                                                 uint32_t capacity)
{
    if (state == 0 || capacity == 0 || state->render_count >= capacity) {
        return 0;
    }
    ++state->render_count;
    return 1;
}

static inline void geVectorTextFrameBegin(GeVectorTextFrameState *state)
{
    if (state == 0) {
        return;
    }
    state->render_count = state->pending_count;
    state->pending_count = 0;
}

static inline void geVectorTextFrameConsume(GeVectorTextFrameState *state)
{
    if (state != 0) {
        state->render_count = 0;
    }
}

#endif /* GE007_VECTOR_TEXT_FRAME_POLICY_H */
