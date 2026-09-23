#ifndef GE007_GFX_WATER_HEIGHTFIELD_POLICY_H
#define GE007_GFX_WATER_HEIGHTFIELD_POLICY_H

/* Deterministic functional core for animated water.
 *
 * This is a bounded heightfield, not a full fluid solver.  The state mirrors
 * the useful part of the WebGL Water approach: height and vertical velocity
 * are stepped on a fixed grid, the four-neighbour average drives propagation,
 * and finite differences provide a surface normal.  Rendering and game state
 * remain outside this header so it can be tested without SDL, OpenGL, or a
 * ROM.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

inline constexpr std::size_t GE_WATER_HEIGHTFIELD_SIZE = 64U;
inline constexpr std::size_t GE_WATER_HEIGHTFIELD_CELL_COUNT =
    GE_WATER_HEIGHTFIELD_SIZE * GE_WATER_HEIGHTFIELD_SIZE;
inline constexpr float GE_WATER_HEIGHTFIELD_FIXED_STEP = 1.0f / 60.0f;
inline constexpr float GE_WATER_HEIGHTFIELD_DAMPING = 0.995f;
inline constexpr float GE_WATER_HEIGHTFIELD_STIFFNESS = 2.0f;
inline constexpr float GE_WATER_HEIGHTFIELD_MAX_HEIGHT = 0.25f;
inline constexpr float GE_WATER_HEIGHTFIELD_MAX_VELOCITY = 0.10f;
inline constexpr std::size_t GE_WATER_HEIGHTFIELD_MAX_SUBSTEPS = 8U;
inline constexpr float GE_WATER_HEIGHTFIELD_MIN_DROP_RADIUS = 0.01f;
inline constexpr float GE_WATER_HEIGHTFIELD_MAX_DROP_RADIUS = 0.50f;
inline constexpr float GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH = 0.06f;
inline constexpr float GE_WATER_HEIGHTFIELD_RENDER_SCALE_ENHANCED = 0.006f;
inline constexpr float GE_WATER_HEIGHTFIELD_RENDER_SCALE_REMASTER = 0.010f;
inline constexpr float GE_WATER_SURFACE_REPEAT_METERS = 256.0f;
inline constexpr float GE_WATER_BOAT_WAKE_SPEED_THRESHOLD = 0.05f;
inline constexpr std::size_t GE_WATER_BOAT_WAKE_IMPULSE_COUNT = 3U;

struct GeWaterHeightfieldView {
    std::uint32_t width;
    std::uint32_t height;
    const float *heights;
    const float *normal_x;
    const float *normal_z;
    float max_height;
};

struct GeWaterHeightfieldNormal {
    float x;
    float z;
};

struct GeWaterHeightfieldImpulse {
    float water_u;
    float water_v;
    float radius;
    float strength;
};

struct GeWaterBoatWakeInput {
    float water_u;
    float water_v;
    float heading_u;
    float heading_v;
    float speed;
    float beam;
};

struct GeWaterBoatWakeBatch {
    std::uint32_t count;
    GeWaterHeightfieldImpulse impulses[GE_WATER_BOAT_WAKE_IMPULSE_COUNT];
};

inline bool geWaterHeightfieldFinite(float value)
{
    return std::isfinite(value);
}

inline float geWaterHeightfieldClamp(float value, float lower, float upper)
{
    return std::clamp(value, lower, upper);
}

inline float geWaterHeightfieldWrap01(float value)
{
    if (!geWaterHeightfieldFinite(value)) {
        return 0.0f;
    }
    const float wrapped = value - std::floor(value);
    return wrapped >= 1.0f ? 0.0f : wrapped;
}

/* Convert a world-space point into the periodic material coordinates used by
 * the heightfield. The mapping deliberately has no camera term: moving the
 * view must not move a stationary boat, dam, or wake through the simulation.
 * The returned coordinates are unwrapped; shader sampling applies fract()
 * at the texture boundary so large world patches remain continuous. */
inline bool geWaterSurfaceUvFromWorld(float world_x, float world_z,
                                      float *water_u, float *water_v)
{
    if (water_u == nullptr || water_v == nullptr ||
        !geWaterHeightfieldFinite(world_x) ||
        !geWaterHeightfieldFinite(world_z) ||
        !geWaterHeightfieldFinite(GE_WATER_SURFACE_REPEAT_METERS) ||
        GE_WATER_SURFACE_REPEAT_METERS <= 0.0f) {
        return false;
    }

    *water_u = world_z / GE_WATER_SURFACE_REPEAT_METERS;
    *water_v = world_x / GE_WATER_SURFACE_REPEAT_METERS;
    return geWaterHeightfieldFinite(*water_u) &&
           geWaterHeightfieldFinite(*water_v);
}

inline GeWaterHeightfieldNormal geWaterHeightfieldNormalFromGradient(
    float horizontal, float vertical)
{
    const float nx = -horizontal * 0.5f;
    const float ny = 1.0f;
    const float nz = -vertical * 0.5f;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    /* ny is fixed at one, so every finite gradient has length >= 1. Only a
     * non-finite input can make the normalisation unsafe. */
    if (!geWaterHeightfieldFinite(length)) {
        return {0.0f, 0.0f};
    }
    return {nx / length, nz / length};
}

class GeWaterHeightfield {
public:
    GeWaterHeightfield()
    {
        reset();
    }

    void reset()
    {
        heights_.fill(0.0f);
        next_heights_.fill(0.0f);
        velocity_.fill(0.0f);
        next_velocity_.fill(0.0f);
        normal_x_.fill(0.0f);
        normal_z_.fill(0.0f);
        accumulator_ = 0.0f;
        steps_ = 0U;
        active_ = false;
    }

    bool add_drop(float water_u, float water_v, float radius, float strength)
    {
        if (!geWaterHeightfieldFinite(water_u) ||
            !geWaterHeightfieldFinite(water_v) ||
            !geWaterHeightfieldFinite(radius) ||
            !geWaterHeightfieldFinite(strength)) {
            return false;
        }

        /* Surface UVs are world-anchored and the compositor samples them with
         * fract(). Apply the same periodic mapping before depositing a CPU
         * disturbance so a repeated world tile cannot create a second wake
         * at a different edge of the simulation texture. */
        water_u = geWaterHeightfieldWrap01(water_u);
        water_v = geWaterHeightfieldWrap01(water_v);
        radius = geWaterHeightfieldClamp(
            radius, GE_WATER_HEIGHTFIELD_MIN_DROP_RADIUS,
            GE_WATER_HEIGHTFIELD_MAX_DROP_RADIUS);
        strength = geWaterHeightfieldClamp(
            strength, -GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH,
            GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH);
        if (strength != 0.0f) {
            active_ = true;
        }

        constexpr float pi = 3.14159265358979323846f;
        const float cell_denominator =
            static_cast<float>(GE_WATER_HEIGHTFIELD_SIZE - 1U);
        for (std::size_t y = 0; y < GE_WATER_HEIGHTFIELD_SIZE; ++y) {
            const float cell_v = static_cast<float>(y) / cell_denominator;
            for (std::size_t x = 0; x < GE_WATER_HEIGHTFIELD_SIZE; ++x) {
                const float cell_u = static_cast<float>(x) / cell_denominator;
                const float dx = cell_u - water_u;
                const float dy = cell_v - water_v;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance >= radius) {
                    continue;
                }

                const float normalized =
                    geWaterHeightfieldClamp(1.0f - distance / radius,
                                            0.0f, 1.0f);
                const float smooth =
                    0.5f - 0.5f * std::cos(normalized * pi);
                const std::size_t index = offset(x, y);
                heights_[index] = geWaterHeightfieldClamp(
                    heights_[index] + smooth * strength,
                    -GE_WATER_HEIGHTFIELD_MAX_HEIGHT,
                    GE_WATER_HEIGHTFIELD_MAX_HEIGHT);
            }
        }
        compute_normals();
        return true;
    }

    bool add_impulse(const GeWaterHeightfieldImpulse &impulse)
    {
        return add_drop(impulse.water_u, impulse.water_v, impulse.radius,
                        impulse.strength);
    }

    void advance(float delta_seconds)
    {
        if (!active_ || !geWaterHeightfieldFinite(delta_seconds) ||
            delta_seconds <= 0.0f) {
            return;
        }

        const float maximum_delta =
            GE_WATER_HEIGHTFIELD_FIXED_STEP *
            static_cast<float>(GE_WATER_HEIGHTFIELD_MAX_SUBSTEPS);
        delta_seconds = std::min(delta_seconds, maximum_delta);
        accumulator_ = std::min(accumulator_ + delta_seconds, maximum_delta);

        std::size_t performed = 0U;
        while (accumulator_ + 0.0000001f >=
                   GE_WATER_HEIGHTFIELD_FIXED_STEP &&
               performed < GE_WATER_HEIGHTFIELD_MAX_SUBSTEPS) {
            step_once();
            accumulator_ -= GE_WATER_HEIGHTFIELD_FIXED_STEP;
            ++performed;
            ++steps_;
        }

        /* A capped frame never retains a second burst of work: the accumulator
         * is capped to the same eight steps consumed by this loop, so the
         * normal remainder is already less than one fixed step. */
        if (!geWaterHeightfieldFinite(accumulator_) || accumulator_ < 0.0f) {
            accumulator_ = 0.0f;
        }
        if (performed != 0U && energy() < 0.00000001f) {
            heights_.fill(0.0f);
            next_heights_.fill(0.0f);
            velocity_.fill(0.0f);
            next_velocity_.fill(0.0f);
            normal_x_.fill(0.0f);
            normal_z_.fill(0.0f);
            accumulator_ = 0.0f;
            active_ = false;
        }
    }

    bool sample(float water_u, float water_v, float *height,
                float *normal_x, float *normal_z) const
    {
        if (height == nullptr || !geWaterHeightfieldFinite(water_u) ||
            !geWaterHeightfieldFinite(water_v)) {
            return false;
        }

        water_u = geWaterHeightfieldWrap01(water_u);
        water_v = geWaterHeightfieldWrap01(water_v);
        const float coordinate_max =
            static_cast<float>(GE_WATER_HEIGHTFIELD_SIZE - 1U);
        const float x = water_u * coordinate_max;
        const float y = water_v * coordinate_max;
        const std::size_t x0 = static_cast<std::size_t>(x);
        const std::size_t y0 = static_cast<std::size_t>(y);
        const std::size_t x1 = std::min(x0 + 1U,
                                        GE_WATER_HEIGHTFIELD_SIZE - 1U);
        const std::size_t y1 = std::min(y0 + 1U,
                                        GE_WATER_HEIGHTFIELD_SIZE - 1U);
        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);

        *height = bilinear(heights_, x0, y0, x1, y1, tx, ty);
        if (normal_x != nullptr) {
            *normal_x = bilinear(normal_x_, x0, y0, x1, y1, tx, ty);
        }
        if (normal_z != nullptr) {
            *normal_z = bilinear(normal_z_, x0, y0, x1, y1, tx, ty);
        }
        return geWaterHeightfieldFinite(*height) &&
               (normal_x == nullptr || geWaterHeightfieldFinite(*normal_x)) &&
               (normal_z == nullptr || geWaterHeightfieldFinite(*normal_z));
    }

    float height_at(std::size_t x, std::size_t y) const
    {
        return heights_[offset(std::min(x, GE_WATER_HEIGHTFIELD_SIZE - 1U),
                               std::min(y, GE_WATER_HEIGHTFIELD_SIZE - 1U))];
    }

    float velocity_at(std::size_t x, std::size_t y) const
    {
        return velocity_[offset(std::min(x, GE_WATER_HEIGHTFIELD_SIZE - 1U),
                                std::min(y, GE_WATER_HEIGHTFIELD_SIZE - 1U))];
    }

    float maximum_absolute_height() const
    {
        float maximum = 0.0f;
        for (float value : heights_) {
            maximum = std::max(maximum, std::fabs(value));
        }
        return maximum;
    }

    float energy() const
    {
        float total = 0.0f;
        for (std::size_t i = 0; i < GE_WATER_HEIGHTFIELD_CELL_COUNT; ++i) {
            total += heights_[i] * heights_[i] +
                     0.25f * velocity_[i] * velocity_[i];
        }
        return total;
    }

    std::size_t steps() const
    {
        return steps_;
    }

    float accumulator() const
    {
        return accumulator_;
    }

    bool active() const
    {
        return active_;
    }

    GeWaterHeightfieldView view() const
    {
        return {
            static_cast<std::uint32_t>(GE_WATER_HEIGHTFIELD_SIZE),
            static_cast<std::uint32_t>(GE_WATER_HEIGHTFIELD_SIZE),
            heights_.data(), normal_x_.data(), normal_z_.data(),
            GE_WATER_HEIGHTFIELD_MAX_HEIGHT};
    }

private:
    static std::size_t offset(std::size_t x, std::size_t y)
    {
        return y * GE_WATER_HEIGHTFIELD_SIZE + x;
    }

    static std::size_t clamped_offset(int x, int y)
    {
        const int last = static_cast<int>(GE_WATER_HEIGHTFIELD_SIZE - 1U);
        x = std::clamp(x, 0, last);
        y = std::clamp(y, 0, last);
        return offset(static_cast<std::size_t>(x), static_cast<std::size_t>(y));
    }

    static float bilinear(const std::array<float,
                                      GE_WATER_HEIGHTFIELD_CELL_COUNT> &values,
                          std::size_t x0, std::size_t y0, std::size_t x1,
                          std::size_t y1, float tx, float ty)
    {
        const float top = values[offset(x0, y0)] * (1.0f - tx) +
                          values[offset(x1, y0)] * tx;
        const float bottom = values[offset(x0, y1)] * (1.0f - tx) +
                             values[offset(x1, y1)] * tx;
        return top * (1.0f - ty) + bottom * ty;
    }

    void step_once()
    {
        for (int y = 0; y < static_cast<int>(GE_WATER_HEIGHTFIELD_SIZE); ++y) {
            for (int x = 0; x < static_cast<int>(GE_WATER_HEIGHTFIELD_SIZE);
                 ++x) {
                const std::size_t index =
                    clamped_offset(x, y);
                const float average =
                    (heights_[clamped_offset(x - 1, y)] +
                     heights_[clamped_offset(x + 1, y)] +
                     heights_[clamped_offset(x, y - 1)] +
                     heights_[clamped_offset(x, y + 1)]) *
                    0.25f;
                float velocity = velocity_[index] +
                                 (average - heights_[index]) *
                                     GE_WATER_HEIGHTFIELD_STIFFNESS;
                velocity = geWaterHeightfieldClamp(
                    velocity, -GE_WATER_HEIGHTFIELD_MAX_VELOCITY,
                    GE_WATER_HEIGHTFIELD_MAX_VELOCITY);
                next_velocity_[index] = velocity * GE_WATER_HEIGHTFIELD_DAMPING;
                next_heights_[index] = geWaterHeightfieldClamp(
                    heights_[index] + next_velocity_[index],
                    -GE_WATER_HEIGHTFIELD_MAX_HEIGHT,
                    GE_WATER_HEIGHTFIELD_MAX_HEIGHT);
            }
        }
        heights_.swap(next_heights_);
        velocity_.swap(next_velocity_);
        compute_normals();
    }

    void compute_normals()
    {
        for (int y = 0; y < static_cast<int>(GE_WATER_HEIGHTFIELD_SIZE); ++y) {
            for (int x = 0; x < static_cast<int>(GE_WATER_HEIGHTFIELD_SIZE);
                 ++x) {
                const float horizontal =
                    heights_[clamped_offset(x + 1, y)] -
                    heights_[clamped_offset(x - 1, y)];
                const float vertical =
                    heights_[clamped_offset(x, y + 1)] -
                    heights_[clamped_offset(x, y - 1)];
                const std::size_t index = clamped_offset(x, y);
                const GeWaterHeightfieldNormal normal =
                    geWaterHeightfieldNormalFromGradient(horizontal, vertical);
                normal_x_[index] = normal.x;
                normal_z_[index] = normal.z;
            }
        }
    }

    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> heights_{};
    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> next_heights_{};
    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> velocity_{};
    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> next_velocity_{};
    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> normal_x_{};
    std::array<float, GE_WATER_HEIGHTFIELD_CELL_COUNT> normal_z_{};
    float accumulator_ = 0.0f;
    std::size_t steps_ = 0U;
    bool active_ = false;
};

inline bool geWaterBuildBoatWake(const GeWaterBoatWakeInput &input,
                                 GeWaterBoatWakeBatch *batch)
{
    if (batch == nullptr) {
        return false;
    }
    *batch = {};
    if (!geWaterHeightfieldFinite(input.water_u) ||
        !geWaterHeightfieldFinite(input.water_v) ||
        !geWaterHeightfieldFinite(input.heading_u) ||
        !geWaterHeightfieldFinite(input.heading_v) ||
        !geWaterHeightfieldFinite(input.speed) ||
        !geWaterHeightfieldFinite(input.beam) || input.speed < 0.0f ||
        input.beam <= 0.0f) {
        return false;
    }

    const float heading_length = std::sqrt(
        input.heading_u * input.heading_u + input.heading_v * input.heading_v);
    if (!geWaterHeightfieldFinite(heading_length) || heading_length <= 0.000001f) {
        return false;
    }
    if (input.speed <= GE_WATER_BOAT_WAKE_SPEED_THRESHOLD) {
        return true;
    }

    const float center_u = geWaterHeightfieldWrap01(input.water_u);
    const float center_v = geWaterHeightfieldWrap01(input.water_v);
    const float heading_u = input.heading_u / heading_length;
    const float heading_v = input.heading_v / heading_length;
    const float perpendicular_u = -heading_v;
    const float perpendicular_v = heading_u;
    const float beam = geWaterHeightfieldClamp(input.beam, 0.04f, 0.30f);
    const float stern_u = center_u - heading_u * beam * 0.60f;
    const float stern_v = center_v - heading_v * beam * 0.60f;
    const float side_offset = beam * 0.50f;
    const float radius = geWaterHeightfieldClamp(
        beam * 0.35f, GE_WATER_HEIGHTFIELD_MIN_DROP_RADIUS,
        GE_WATER_HEIGHTFIELD_MAX_DROP_RADIUS);
    const float speed = std::min(input.speed, 3.0f);
    const float side_strength = geWaterHeightfieldClamp(
        speed * 0.018f, 0.004f, GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH);
    const float bow_strength = geWaterHeightfieldClamp(
        side_strength * 0.60f, 0.002f, GE_WATER_HEIGHTFIELD_MAX_DROP_STRENGTH);

    const auto wrap_coordinate = [](float value) {
        return geWaterHeightfieldWrap01(value);
    };
    batch->impulses[0] = {
        wrap_coordinate(center_u + heading_u * beam * 0.45f),
        wrap_coordinate(center_v + heading_v * beam * 0.45f), radius,
        bow_strength};
    /* Keep the order stable: impulse one is the negative-perpendicular side,
     * impulse two the positive-perpendicular side. Stable ordering makes
     * capture tests and event traces deterministic. */
    batch->impulses[1] = {
        wrap_coordinate(stern_u - perpendicular_u * side_offset),
        wrap_coordinate(stern_v - perpendicular_v * side_offset), radius,
        side_strength};
    batch->impulses[2] = {
        wrap_coordinate(stern_u + perpendicular_u * side_offset),
        wrap_coordinate(stern_v + perpendicular_v * side_offset), radius,
        side_strength};
    batch->count = static_cast<std::uint32_t>(GE_WATER_BOAT_WAKE_IMPULSE_COUNT);
    return true;
}

#endif /* GE007_GFX_WATER_HEIGHTFIELD_POLICY_H */
