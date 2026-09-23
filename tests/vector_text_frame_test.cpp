#include "../port/fast3d/vector_text_frame_policy.h"

#include <cassert>
#include <cstdlib>

static void geTestRequire(bool condition)
{
    if (!condition) {
        std::abort();
    }
}

#undef assert
#define assert(condition) geTestRequire((condition))

int main()
{
    // Invalid producer/consumer state fails closed without dereferencing a
    // null state or treating a zero-capacity queue as writable.
    assert(geVectorTextFrameQueue(nullptr, 64) == 0);
    assert(geVectorTextFrameQueue(nullptr, 0) == 0);
    GeVectorTextFrameState invalid_state = {7, 9};
    assert(geVectorTextFrameQueue(&invalid_state, 0) == 0);
    geVectorTextFrameBegin(nullptr);
    geVectorTextFrameConsume(nullptr);
    geVectorTextBuildBegin(nullptr);
    assert(geVectorTextBuildPublish(nullptr) == 0);
    assert(invalid_state.pending_count == 7);
    assert(invalid_state.render_count == 9);

    GeVectorTextFrameState state = {};

    // The renderer may only publish vector commands when the enhanced path,
    // initialization, and the current atlas are all valid. This is the
    // failure boundary used by the OpenGL adapter when a resize rebuild fails.
    assert(geVectorTextCanRender(0, 1, 1, 1) == 0);
    assert(geVectorTextCanRender(1, 1, 0, 1) == 0);
    assert(geVectorTextCanRender(1, 1, 1, 0) == 0);
    assert(geVectorTextCanRender(1, 1, 1, 1) == 1);
    assert(geVectorTextCanRender(2, 1, 1, 1) == 1);

    // A scheduler handoff may expose one empty render batch even though the
    // same UI was just rendered. Reuse is bounded by the caller to one frame;
    // it is never a substitute for a fresh batch indefinitely.
    assert(geVectorTextFrameShouldReuseLast(0, 12, 1) == 1);
    assert(geVectorTextFrameShouldReuseLast(0, 0, 1) == 0);
    assert(geVectorTextFrameShouldReuseLast(4, 12, 1) == 0);
    assert(geVectorTextFrameShouldReuseLast(0, 12, 0) == 0);

    // The game builds a complete display list while the renderer may still
    // be consuming the previous one. Text must stay in a private build batch
    // until the whole display list is published atomically.
    GeVectorTextBuildState build = {};
    geVectorTextBuildBegin(&build);
    assert(geVectorTextBuildQueue(&build, 4) == 1);
    assert(geVectorTextBuildQueue(&build, 4) == 1);
    assert(build.build_count == 2);
    assert(geVectorTextBuildPublish(&build) == 2);
    assert(build.build_count == 0);
    assert(build.building == 0);
    assert(geVectorTextBuildQueue(&build, 4) == 0);
    geVectorTextBuildBegin(&build);
    for (unsigned i = 0; i < 4; ++i) {
        assert(geVectorTextBuildQueue(&build, 4) == 1);
    }
    assert(geVectorTextBuildQueue(&build, 4) == 0);
    assert(geVectorTextBuildPublish(&build) == 4);

    // Text recorded before the render task starts belongs to that task.
    assert(geVectorTextFrameQueue(&state, 64) == 1);
    geVectorTextFrameBegin(&state);
    assert(state.pending_count == 0);
    assert(state.render_count == 1);

    // A port-owned overlay is emitted after the game batch has been handed to
    // the renderer. It must join the current batch rather than being placed
    // in pending, where the next game build can overwrite it before draw.
    GeVectorTextFrameState current_state = {0, 1};
    assert(geVectorTextFrameQueueCurrent(&current_state, 64) == 1);
    assert(current_state.render_count == 2);
    assert(current_state.pending_count == 0);
    for (unsigned i = 2; i < 64; ++i) {
        assert(geVectorTextFrameQueueCurrent(&current_state, 64) == 1);
    }
    assert(current_state.render_count == 64);
    assert(geVectorTextFrameQueueCurrent(&current_state, 64) == 0);

    // Text recorded while the current task is rendering must be retained for
    // the next task instead of racing with or replacing the current batch.
    assert(geVectorTextFrameQueue(&state, 64) == 1);
    assert(state.render_count == 1);
    assert(state.pending_count == 1);
    geVectorTextFrameConsume(&state);
    assert(state.render_count == 0);
    geVectorTextFrameBegin(&state);
    assert(state.render_count == 1);
    assert(state.pending_count == 0);

    // The fixed-capacity boundary fails closed and never overwrites a queued
    // command that belongs to a later render task.
    for (unsigned i = 1; i < 64; ++i) {
        assert(geVectorTextFrameQueue(&state, 64) == 1);
    }
    assert(state.pending_count == 63);
    assert(geVectorTextFrameQueue(&state, 64) == 1);
    assert(state.pending_count == 64);
    assert(geVectorTextFrameQueue(&state, 64) == 0);
    assert(state.pending_count == 64);

    // A consumed batch is not replayed forever when the producer has no new
    // text; the next begin starts with an empty render batch.
    geVectorTextFrameConsume(&state);
    geVectorTextFrameBegin(&state);
    assert(state.render_count == 64);
    geVectorTextFrameConsume(&state);
    geVectorTextFrameBegin(&state);
    assert(state.render_count == 0);

    return 0;
}
