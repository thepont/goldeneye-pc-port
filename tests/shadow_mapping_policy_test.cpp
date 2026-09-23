#include "../port/fast3d/shadow_mapping_policy.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>

static bool near(float actual, float expected, float epsilon = 0.0002f)
{
    return std::isfinite(actual) && std::fabs(actual - expected) <= epsilon;
}

static int fail(const char *message)
{
    std::fprintf(stderr, "shadow mapping policy failed: %s\n", message);
    return 1;
}

static const float shadow_test_vertices[9] = {
    -1.0f, -1.0f, -2.0f,
     1.0f, -1.0f, -2.0f,
     0.0f,  1.0f, -2.0f,
};

static const float shadow_test_direction[3] = {0.25f, 0.8f, 0.4f};

static bool shadowReplayReachesEnd(const unsigned *opcodes,
                                   std::size_t opcode_count,
                                   std::size_t *end_index)
{
    const unsigned *cursor = opcodes;
    const unsigned *const end = opcodes + opcode_count;
    for (std::size_t steps = 0; steps <= opcode_count; ++steps) {
        if (cursor >= end) {
            return false;
        }
        const unsigned opcode = *cursor;
        if (geShadowReplayAdvanceIgnoredCommand(cursor, opcode)) {
            continue;
        }
        if (opcode == static_cast<std::uint8_t>(G_ENDDL)) {
            if (end_index != nullptr) {
                *end_index = static_cast<std::size_t>(cursor - opcodes);
            }
            return true;
        }
        ++cursor;
    }
    return false;
}

static int GivenIgnoredOneWordOpcode_WhenShadowReplayContinues_ThenFollowingEndIsReached(
    unsigned ignored_opcode)
{
    /* Arrange: an ignored one-entry packet immediately precedes the sentinel. */
    const unsigned opcodes[] = {
        ignored_opcode, static_cast<std::uint8_t>(G_ENDDL)};
    std::size_t end_index = 0;

    /* Act */
    const bool reached_end = shadowReplayReachesEnd(
        opcodes, std::size(opcodes), &end_index);

    /* Assert */
    return reached_end && end_index == 1 ? 0 :
        fail("ignored one-word command must advance to the following G_ENDDL");
}

static int GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
    unsigned ignored_opcode, std::size_t expected_cursor_advance)
{
    /* Arrange: payload words must be skipped before the next opcode. */
    std::array<unsigned, 4> opcodes = {};
    opcodes[0] = ignored_opcode;
    opcodes[expected_cursor_advance] = static_cast<std::uint8_t>(G_ENDDL);
    std::size_t end_index = 0;

    /* Act */
    const bool reached_end = shadowReplayReachesEnd(
        opcodes.data(), expected_cursor_advance + 1, &end_index);

    /* Assert */
    const GeShadowReplaySkip skip =
        geShadowReplayIgnoredCommand(ignored_opcode);
    return reached_end && end_index == expected_cursor_advance && skip.ignored &&
                   skip.cursor_advance == expected_cursor_advance
        ? 0
        : fail("ignored multiword command must skip its full packet width and preserve alignment");
}

static int GivenNullVector_WhenNormalizing_ThenNormalizationIsRejected(void)
{
    /* Arrange */
    float result[3] = {};

    /* Act */
    const bool normalized = geShadowNormalize3(nullptr, result);

    /* Assert */
    return normalized ? fail("null normalization input must be rejected") : 0;
}

static int GivenNullDestination_WhenNormalizing_ThenNormalizationIsRejected(void)
{
    /* Arrange */
    const float source[3] = {1.0f, 0.0f, 0.0f};

    /* Act */
    const bool normalized = geShadowNormalize3(source, nullptr);

    /* Assert */
    return normalized ? fail("null normalization destination must be rejected") : 0;
}

static int GivenZeroVector_WhenNormalizing_ThenNormalizationIsRejected(void)
{
    /* Arrange */
    const float source[3] = {};
    float result[3] = {};

    /* Act */
    const bool normalized = geShadowNormalize3(source, result);

    /* Assert */
    return normalized ? fail("zero-length normalization input must be rejected") : 0;
}

static int GivenNullMatrixOutput_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, shadow_test_direction, 0.05f, nullptr);

    /* Assert */
    return built ? fail("null matrix output must be rejected") : 0;
}

static int GivenFewerThanThreeVertices_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 2, shadow_test_direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("fewer than one triangle must be rejected") : 0;
}

static int GivenNonTriangleVertexCount_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 4, shadow_test_direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("non-triangle vertex counts must be rejected") : 0;
}

static int GivenNullLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, nullptr, 0.05f, &matrix);

    /* Assert */
    return built ? fail("null light direction must be rejected") : 0;
}

static int GivenNonFiniteLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    const float direction[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("non-finite light direction must be rejected") : 0;
}

static int GivenNanPadding_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, shadow_test_direction,
        std::numeric_limits<float>::quiet_NaN(), &matrix);

    /* Assert */
    return built ? fail("non-finite padding must be rejected") : 0;
}

static int GivenNegativePadding_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, shadow_test_direction, -0.01f, &matrix);

    /* Assert */
    return built ? fail("negative padding must be rejected") : 0;
}

static int GivenPaddingAboveOne_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, shadow_test_direction, 1.01f, &matrix);

    /* Assert */
    return built ? fail("padding above one must be rejected") : 0;
}

static int GivenZeroLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    const float direction[3] = {};
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        shadow_test_vertices, 3, direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("zero light direction must be rejected") : 0;
}

static int GivenOverflowingProjectedCoordinate_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    const float limit = std::numeric_limits<float>::max();
    const float vertices[9] = {
        -limit, 0.0f, limit,
        -limit, 0.0f, limit,
        -limit, 0.0f, limit,
    };
    const float direction[3] = {1.0f, 0.0f, 1.0f};
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        vertices, 3, direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("overflowing light-space coordinates must be rejected") : 0;
}

static int GivenHugePointBounds_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    const float vertices[9] = {
        2.0e38f, 2.0e38f, 2.0e38f,
        2.0e38f, 2.0e38f, 2.0e38f,
        2.0e38f, 2.0e38f, 2.0e38f,
    };
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        vertices, 3, shadow_test_direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("point bounds at large magnitudes must not create a matrix") : 0;
}

static int GivenOverflowingMatrixTranslation_WhenBuildingDirectionalMatrix_ThenBuildIsRejected(void)
{
    /* Arrange */
    const float vertices[9] = {
        -2.0e38f, 0.0f, 0.0f,
        -1.5e38f, 1.0f, 1.0f,
        -1.5e38f, 2.0f, 2.0f,
    };
    const float direction[3] = {0.0f, 0.0f, 1.0f};
    GeShadowMatrix matrix = {};

    /* Act */
    const bool built = geShadowBuildDirectionalMatrix(
        vertices, 3, direction, 0.05f, &matrix);

    /* Assert */
    return built ? fail("non-finite fitted matrix values must be rejected") : 0;
}

static int GivenNullProjectedPoint_WhenProjecting_ThenProjectionIsRejected(void)
{
    /* Arrange */
    const float position[3] = {0.0f, 0.0f, 0.0f};
    const GeShadowMatrix matrix = {};

    /* Act */
    const bool projected = geShadowProject(matrix, position, nullptr);

    /* Assert */
    return projected ? fail("null projected output must be rejected") : 0;
}

static int GivenNullPosition_WhenProjecting_ThenProjectionIsRejected(void)
{
    /* Arrange */
    GeShadowProjectedPoint projected = {};
    const GeShadowMatrix matrix = {};

    /* Act */
    const bool projected_ok = geShadowProject(matrix, nullptr, &projected);

    /* Assert */
    return projected_ok ? fail("null projected position must be rejected") : 0;
}

static int GivenZeroClipW_WhenProjecting_ThenProjectionIsRejected(void)
{
    /* Arrange */
    const float position[3] = {0.0f, 0.0f, 0.0f};
    GeShadowProjectedPoint projected = {};
    const GeShadowMatrix matrix = {};

    /* Act */
    const bool projected_ok = geShadowProject(matrix, position, &projected);

    /* Assert */
    return projected_ok ? fail("zero clip w must be rejected") : 0;
}

static int GivenNonFiniteClipCoordinate_WhenProjecting_ThenProjectionIsRejected(void)
{
    /* Arrange */
    const float position[3] = {0.0f, 0.0f, 0.0f};
    GeShadowProjectedPoint projected = {};
    GeShadowMatrix matrix = {};
    matrix.column_major[0] = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const bool projected_ok = geShadowProject(matrix, position, &projected);

    /* Assert */
    return projected_ok ? fail("non-finite clip coordinates must be rejected") : 0;
}

static int GivenOverflowingProjectedDepth_WhenProjecting_ThenProjectionIsRejected(void)
{
    /* Arrange */
    const float position[3] = {1.0f, 0.0f, 0.0f};
    GeShadowProjectedPoint projected = {};
    GeShadowMatrix matrix = {};
    matrix.column_major[0] = std::numeric_limits<float>::max();
    matrix.column_major[15] = 1.0e-7f;

    /* Act */
    const bool projected_ok = geShadowProject(matrix, position, &projected);

    /* Assert */
    return projected_ok ? fail("overflowing projected values must be rejected") : 0;
}

static int GivenYOutsideTheFrustum_WhenCheckingBounds_ThenPointIsOutside(void)
{
    /* Arrange */
    const float ndc[3] = {0.0f, 1.1f, 0.0f};

    /* Act */
    const bool outside = geShadowProjectedPointOutside(ndc);

    /* Assert */
    return outside ? 0 : fail("y coordinates outside the frustum must be detected");
}

static int GivenDepthOutsideTheFrustum_WhenCheckingBounds_ThenPointIsOutside(void)
{
    /* Arrange */
    const float ndc[3] = {0.0f, 0.0f, 1.1f};

    /* Act */
    const bool outside = geShadowProjectedPointOutside(ndc);

    /* Assert */
    return outside ? 0 : fail("depth coordinates outside the frustum must be detected");
}

static int GivenNonFiniteFrustumCoordinates_WhenCheckingBounds_ThenPointIsNotOutside(void)
{
    /* Arrange */
    const float ndc[3] = {
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};

    /* Act */
    const bool outside = geShadowProjectedPointOutside(ndc);

    /* Assert */
    return outside ? fail("non-finite frustum coordinates must fail open") : 0;
}

static int GivenNonFiniteNormal_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */
    const float normal = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const float bias = geShadowSlopeBias(normal, 0.0f, 0.0f, 1.0f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("non-finite slope normal must return zero bias");
}

static int GivenNonFiniteMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */
    const float minimum = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, minimum, 0.0f, 1.0f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("non-finite minimum bias must return zero");
}

static int GivenNonFiniteSlopeScale_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */
    const float slope = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, 0.0f, slope, 1.0f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("non-finite slope scale must return zero");
}

static int GivenNonFiniteMaximum_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */
    const float maximum = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, 0.0f, 0.0f, maximum);

    /* Assert */
    return bias == 0.0f ? 0 : fail("non-finite maximum bias must return zero");
}

static int GivenNegativeMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, -1.0f, 0.0f, 1.0f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("negative minimum bias must return zero");
}

static int GivenNegativeSlopeScale_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, 0.0f, -1.0f, 1.0f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("negative slope scale must return zero");
}

static int GivenMaximumBelowMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero(void)
{
    /* Arrange */

    /* Act */
    const float bias = geShadowSlopeBias(0.0f, 0.5f, 0.0f, 0.4f);

    /* Assert */
    return bias == 0.0f ? 0 : fail("maximum below minimum must return zero");
}

static int GivenFacingBelowZero_WhenCalculatingSlopeBias_ThenFacingIsClamped(void)
{
    /* Arrange */

    /* Act */
    const float bias = geShadowSlopeBias(-1.0f, geShadowMinimumBias,
                                         geShadowSlopeBiasScale,
                                         geShadowMaximumBias);

    /* Assert */
    return near(bias, geShadowSlopeBiasScale)
               ? 0
               : fail("negative slope facing must clamp to zero");
}

static int GivenFacingAboveOne_WhenCalculatingSlopeBias_ThenFacingIsClamped(void)
{
    /* Arrange */

    /* Act */
    const float bias = geShadowSlopeBias(2.0f, geShadowMinimumBias,
                                         geShadowSlopeBiasScale,
                                         geShadowMaximumBias);

    /* Assert */
    return near(bias, geShadowMinimumBias)
               ? 0
               : fail("slope facing above one must clamp to one");
}

static int GivenNonFiniteStoredDepth_WhenCheckingOcclusion_ThenSurfaceRemainsLit(void)
{
    /* Arrange */
    const float closest = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const bool occluded = geShadowIsOccluded(0.5f, closest, 0.0f);

    /* Assert */
    return occluded ? fail("non-finite stored depth must fail open") : 0;
}

static int GivenNonFiniteOcclusionBias_WhenCheckingOcclusion_ThenSurfaceRemainsLit(void)
{
    /* Arrange */
    const float bias = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const bool occluded = geShadowIsOccluded(0.5f, 0.4f, bias);

    /* Assert */
    return occluded ? fail("non-finite occlusion bias must fail open") : 0;
}

static int GivenNonFinitePcfBias_WhenFilteringSamples_ThenShadowFractionIsZero(void)
{
    /* Arrange */
    const float samples[9] = {};
    const float bias = std::numeric_limits<float>::quiet_NaN();

    /* Act */
    const float shadow_fraction = geShadowPcf3x3(0.5f, bias, samples);

    /* Assert */
    return shadow_fraction == 0.0f
               ? 0
               : fail("non-finite PCF bias must produce a fully lit result");
}

static int GivenNullPcfSamples_WhenFilteringSamples_ThenShadowFractionIsZero(void)
{
    /* Arrange */

    /* Act */
    const float shadow_fraction = geShadowPcf3x3(0.5f, 0.0f, nullptr);

    /* Assert */
    return shadow_fraction == 0.0f
               ? 0
               : fail("null PCF samples must produce a fully lit result");
}

int main()
{
    const unsigned one_word_ignored_opcodes[] = {
        G_CLEAR_DEPTH_EXT,
        G_FILLRECT,
        G_SETTIMG_FB_EXT,
        G_SETFB_EXT,
        G_COPYFB_EXT,
        G_INVALTEXCACHE_EXT,
    };
    for (const unsigned opcode : one_word_ignored_opcodes) {
        if (GivenIgnoredOneWordOpcode_WhenShadowReplayContinues_ThenFollowingEndIsReached(
                opcode) != 0) {
            return 1;
        }
    }
    if (GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
            G_TEXRECT, 3) != 0 ||
        GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
            G_TEXRECTFLIP, 3) != 0 ||
        GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
            G_TEXRECT_WIDE_EXT, 3) != 0 ||
        GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
            G_IMAGERECT_EXT, 3) != 0 ||
        GivenIgnoredMultiwordOpcode_WhenShadowReplayContinues_ThenPacketIsSkipped(
            G_FILLRECT_WIDE_EXT, 2) != 0) {
        return 1;
    }

    if (!geShadowCasterEligible(false, false, false, 0.0f) ||
        geShadowCasterEligible(true, false, false, 0.0f) ||
        geShadowCasterEligible(false, true, false, 0.0f) ||
        geShadowCasterEligible(false, false, true, 0.0f) ||
        geShadowCasterEligible(false, false, false, 0.5f) ||
        geShadowCasterEligible(false, false, false,
                               std::numeric_limits<float>::quiet_NaN())) {
        return fail("only unmasked opaque perspective geometry may enter the caster stream");
    }
    for (unsigned int mask = 0; mask < 32; ++mask) {
        const bool enhanced = (mask & 1U) != 0;
        const bool alpha = (mask & 2U) != 0;
        const bool texture_edge = (mask & 4U) != 0;
        const bool fog = (mask & 8U) != 0;
        const bool grayscale = (mask & 16U) != 0;
        const bool expected = enhanced && !alpha && !texture_edge &&
                              !fog && !grayscale;
        if (geShadowReceiverEligible(enhanced, alpha, texture_edge, fog,
                                     grayscale) != expected) {
            return fail("receiver eligibility must reject unsupported materials in every mode combination");
        }
    }

    const std::array<float, 18> vertices = {
        -2.0f, -1.0f, -4.0f,  2.0f, -1.0f, -4.0f,  0.0f,  2.0f, -4.0f,
        -1.0f, -1.0f, -8.0f,  1.0f, -1.0f, -8.0f,  0.0f,  1.0f, -8.0f,
    };
    const std::array<float, 3> direction_to_light = {0.25f, 0.8f, 0.4f};
    GeShadowMatrix matrix = {};
    if (!geShadowBuildDirectionalMatrix(vertices.data(), vertices.size() / 3,
                                        direction_to_light.data(), 0.05f,
                                        &matrix)) {
        return fail("valid finite scene should produce an orthographic light matrix");
    }
    for (std::size_t i = 0; i < vertices.size(); i += 3) {
        GeShadowProjectedPoint projected = {};
        if (!geShadowProject(matrix, &vertices[i], &projected) ||
            projected.u < -0.0002f || projected.u > 1.0002f ||
            projected.v < -0.0002f || projected.v > 1.0002f ||
            projected.depth < -0.0002f || projected.depth > 1.0002f) {
            return fail("fitted scene vertices must project inside the shadow map");
        }
    }

    GeShadowProjectedPoint center = {};
    const float center_position[3] = {0.0f, 0.0f, -6.0f};
    if (!geShadowProject(matrix, center_position, &center) ||
        !std::isfinite(center.u) || !std::isfinite(center.v) ||
        !std::isfinite(center.depth)) {
        return fail("projected center must remain finite");
    }

    if (!near(geShadowSlopeBias(1.0f, geShadowMinimumBias,
                                geShadowSlopeBiasScale,
                                geShadowMaximumBias), geShadowMinimumBias) ||
        !near(geShadowSlopeBias(0.0f, geShadowMinimumBias,
                                geShadowSlopeBiasScale,
                                geShadowMaximumBias),
              geShadowSlopeBiasScale)) {
        return fail("slope bias must stay at the floor when aligned and grow at grazing angles");
    }
    if (geShadowIsOccluded(0.6f, 0.5f, 0.11f) ||
        !geShadowIsOccluded(0.6f, 0.5f, 0.001f) ||
        geShadowIsOccluded(0.5f, 0.5f, 0.0f)) {
        return fail("depth comparison must apply bias and keep equal-depth receivers lit");
    }

    const float pcf_samples[9] = {
        0.2f, 0.2f, 0.2f,
        0.2f, 0.2f, 0.8f,
        0.8f, 0.8f, 0.8f,
    };
    if (!near(geShadowPcf3x3(0.6f, 0.0f, pcf_samples), 5.0f / 9.0f)) {
        return fail("3x3 PCF must average comparison outcomes, not filter raw depth");
    }

    const float outside_ndc[3] = {1.1f, 0.5f, 0.5f};
    if (!geShadowProjectedPointOutside(outside_ndc) ||
        geShadowProjectedPointOutside(nullptr)) {
        return fail("outside light frustum must be identified while null is rejected safely");
    }

    const std::array<float, 3> vertical_light = {0.0f, 1.0f, 0.0f};
    if (!geShadowBuildDirectionalMatrix(vertices.data(), vertices.size() / 3,
                                        vertical_light.data(), 0.0f,
                                        &matrix)) {
        return fail("near-vertical light must choose a stable non-parallel up axis");
    }
    const float degenerate_vertices[9] = {
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    };
    if (geShadowBuildDirectionalMatrix(degenerate_vertices, 3,
                                       direction_to_light.data(), 0.05f,
                                       &matrix)) {
        return fail("degenerate point-only bounds must not create an unstable projection");
    }

    const std::array<float, 18> invalid_vertices = [] {
        std::array<float, 18> values = {};
        values[4] = std::numeric_limits<float>::infinity();
        return values;
    }();
    if (geShadowBuildDirectionalMatrix(invalid_vertices.data(),
                                       invalid_vertices.size() / 3,
                                       direction_to_light.data(), 0.05f,
                                       &matrix) ||
        geShadowBuildDirectionalMatrix(nullptr, 1,
                                       direction_to_light.data(), 0.05f,
                                       &matrix)) {
        return fail("non-finite or missing geometry must disable the shadow map");
    }

    const float invalid_depth = std::numeric_limits<float>::quiet_NaN();
    if (geShadowIsOccluded(invalid_depth, 0.0f, 0.0f) ||
        geShadowPcf3x3(invalid_depth, 0.0f, pcf_samples) != 0.0f) {
        return fail("malformed shadow depths must fail open as fully lit");
    }

    if (GivenNullVector_WhenNormalizing_ThenNormalizationIsRejected() != 0 ||
        GivenNullDestination_WhenNormalizing_ThenNormalizationIsRejected() != 0 ||
        GivenZeroVector_WhenNormalizing_ThenNormalizationIsRejected() != 0 ||
        GivenNullMatrixOutput_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenFewerThanThreeVertices_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNonTriangleVertexCount_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNullLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNonFiniteLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNanPadding_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNegativePadding_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenPaddingAboveOne_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenZeroLightDirection_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenOverflowingProjectedCoordinate_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenHugePointBounds_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenOverflowingMatrixTranslation_WhenBuildingDirectionalMatrix_ThenBuildIsRejected() != 0 ||
        GivenNullProjectedPoint_WhenProjecting_ThenProjectionIsRejected() != 0 ||
        GivenNullPosition_WhenProjecting_ThenProjectionIsRejected() != 0 ||
        GivenZeroClipW_WhenProjecting_ThenProjectionIsRejected() != 0 ||
        GivenNonFiniteClipCoordinate_WhenProjecting_ThenProjectionIsRejected() != 0 ||
        GivenOverflowingProjectedDepth_WhenProjecting_ThenProjectionIsRejected() != 0 ||
        GivenYOutsideTheFrustum_WhenCheckingBounds_ThenPointIsOutside() != 0 ||
        GivenDepthOutsideTheFrustum_WhenCheckingBounds_ThenPointIsOutside() != 0 ||
        GivenNonFiniteFrustumCoordinates_WhenCheckingBounds_ThenPointIsNotOutside() != 0 ||
        GivenNonFiniteNormal_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenNonFiniteMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenNonFiniteSlopeScale_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenNonFiniteMaximum_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenNegativeMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenNegativeSlopeScale_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenMaximumBelowMinimum_WhenCalculatingSlopeBias_ThenBiasIsZero() != 0 ||
        GivenFacingBelowZero_WhenCalculatingSlopeBias_ThenFacingIsClamped() != 0 ||
        GivenFacingAboveOne_WhenCalculatingSlopeBias_ThenFacingIsClamped() != 0 ||
        GivenNonFiniteStoredDepth_WhenCheckingOcclusion_ThenSurfaceRemainsLit() != 0 ||
        GivenNonFiniteOcclusionBias_WhenCheckingOcclusion_ThenSurfaceRemainsLit() != 0 ||
        GivenNonFinitePcfBias_WhenFilteringSamples_ThenShadowFractionIsZero() != 0 ||
        GivenNullPcfSamples_WhenFilteringSamples_ThenShadowFractionIsZero() != 0) {
        return 1;
    }
    return 0;
}
