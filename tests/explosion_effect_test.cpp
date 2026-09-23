#include "../port/fast3d/explosion_effect_policy.h"

#include <cassert>
#include <cmath>
#include <cstdlib>

/* The repository's compatibility assert routes failures through the game's
 * N64 debug printer. Keep this standalone policy test fail-fast without
 * pulling the full runtime into the test executable. */
extern "C" void osSyncPrintf(const char *, ...)
{
    std::abort();
}

static void assert_near(float actual, float expected)
{
    assert(std::fabs(actual - expected) < 0.0001f);
}

int main()
{
    // Original presentation is a compatibility contract: it must not add
    // any geometry on top of the N64 billboard.
    assert(geExplosionPresentationExtraLayerCount(-1) == 0);
    assert(geExplosionPresentationExtraLayerCount(0) == 0);

    // Enhanced and Remaster share the layered-card presentation for now.
    assert(geExplosionPresentationExtraLayerCount(1) == 2);
    assert(geExplosionPresentationExtraLayerCount(2) == 2);
    assert(geExplosionPresentationExtraLayerCount(99) == 2);

    const GeExplosionPresentationLayer first =
        geExplosionPresentationExtraLayer(0);
    const GeExplosionPresentationLayer second =
        geExplosionPresentationExtraLayer(1);

    // Keep the authored presentation deterministic and bounded. The two
    // cards are intentionally oblique in different axes so the effect is not
    // a single camera-facing sheet at higher render modes.
    assert_near(first.scale, 0.90f);
    assert_near(first.yaw, 0.55f);
    assert_near(first.pitch, 0.0f);
    assert(first.alpha == 152);

    assert_near(second.scale, 0.82f);
    assert_near(second.yaw, 0.0f);
    assert_near(second.pitch, 0.55f);
    assert(second.alpha == 112);

    for (int index = 0; index < 2; ++index) {
        const GeExplosionPresentationLayer layer =
            geExplosionPresentationExtraLayer(index);
        assert(std::isfinite(layer.scale));
        assert(std::isfinite(layer.yaw));
        assert(std::isfinite(layer.pitch));
        assert(layer.scale > 0.0f && layer.scale <= 1.0f);
        assert(layer.alpha > 0 && layer.alpha < 255);
    }

    // Invalid indices must fail closed rather than accidentally drawing an
    // uninitialized layer if a renderer loop is changed later.
    const GeExplosionPresentationLayer invalid =
        geExplosionPresentationExtraLayer(2);
    assert(invalid.scale == 0.0f);
    assert(invalid.alpha == 0);

    return 0;
}
