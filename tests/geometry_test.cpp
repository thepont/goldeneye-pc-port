/* Deterministic unit tests for the game's 2D collision geometry helpers. */
#include <cmath>
#include <cfloat>
#include <cstdio>

extern "C" {
struct coord2d
{
    union
    {
        struct
        {
            float x;
            float y;
        };
        float f[2];
    };
};

struct coord3d
{
    union
    {
        struct
        {
            float x;
            float y;
            float z;
        };
        float f[3];
    };
};

float calculateSegmentIntersectionFraction(coord2d *start1, coord2d *end1,
                                            coord2d *start2, coord2d *end2);
float pointLineDistanceWithRadius(coord3d *point, coord2d *line_start,
                                  coord2d *line_end);
float calculateRayToSegmentIntersectionNormalized(coord3d *point,
                                                   coord2d *line_start,
                                                   coord2d *line_end,
                                                   coord2d *direction);
}

namespace
{

struct TestState
{
    int failures = 0;

    void expect(bool condition, const char *message)
    {
        if (!condition)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s\n", message);
        }
    }

    void near(float actual, float expected, const char *message,
              float epsilon = 0.0001f)
    {
        if (!std::isfinite(actual) || std::fabs(actual - expected) > epsilon)
        {
            ++failures;
            std::fprintf(stderr, "FAIL: %s (actual=%g expected=%g)\n",
                         message, actual, expected);
        }
    }
};

coord2d point2(float x, float y)
{
    coord2d point = {};
    point.x = x;
    point.y = y;
    return point;
}

coord3d point3(float radius, float x, float z)
{
    coord3d point = {};
    point.x = radius;
    point.y = x;
    point.z = z;
    return point;
}

void test_segment_fraction(TestState &tests)
{
    coord2d start1 = point2(0.0f, 0.0f);
    coord2d end1 = point2(10.0f, 0.0f);
    coord2d start2 = point2(5.0f, -1.0f);
    coord2d end2 = point2(5.0f, 1.0f);
    tests.near(calculateSegmentIntersectionFraction(
                   &start1, &end1, &start2, &end2),
               0.5f, "crossing segments return the first-line fraction");

    start2 = point2(15.0f, -1.0f);
    end2 = point2(15.0f, 1.0f);
    tests.near(calculateSegmentIntersectionFraction(
                   &start1, &end1, &start2, &end2),
               1.0f, "crossing outside the first segment returns sentinel");

    start2 = point2(0.0f, 2.0f);
    end2 = point2(10.0f, 2.0f);
    tests.near(calculateSegmentIntersectionFraction(
                   &start1, &end1, &start2, &end2),
               1.0f, "parallel segments return sentinel");

    start1 = point2(5.0f, 0.0f);
    end1 = point2(5.0f, 0.0f);
    tests.near(calculateSegmentIntersectionFraction(
                   &start1, &end1, &start2, &end2),
               1.0f, "zero-length first segment returns sentinel");
}

void test_radius_line_distance(TestState &tests)
{
    coord3d point = point3(1.0f, 0.0f, 0.0f);
    coord2d direction = point2(1.0f, 0.0f);
    coord2d endpoint = point2(5.0f, 0.0f);
    tests.near(pointLineDistanceWithRadius(&point, &direction, &endpoint),
               4.0f, "radius line distance subtracts the radius");

    coord2d offset_endpoint = point2(0.0f, 2.0f);
    tests.expect(
        pointLineDistanceWithRadius(&point, &direction, &offset_endpoint) ==
            FLT_MAX,
        "radius line distance rejects a line outside the radius");

    coord2d behind_endpoint = point2(0.0f, 0.0f);
    tests.near(pointLineDistanceWithRadius(&point, &direction,
                                           &behind_endpoint),
               0.0f, "radius line distance accepts an overlap behind origin");

    coord2d off_axis_direction = point2(1.0f, -1.0f);
    coord2d off_axis_endpoint = point2(0.0f, 1.0f);
    tests.expect(pointLineDistanceWithRadius(&point, &off_axis_direction,
                                             &off_axis_endpoint) == FLT_MAX,
                 "radius line distance rejects a projected point outside the radius");
}

void test_ray_segment_fraction(TestState &tests)
{
    coord3d point = point3(1.0f, 0.0f, 0.0f);
    coord2d edge_start = point2(5.0f, -10.0f);
    coord2d edge_end = point2(5.0f, 10.0f);
    coord2d movement = point2(10.0f, 0.0f);
    tests.near(calculateRayToSegmentIntersectionNormalized(
                   &point, &edge_start, &edge_end, &movement),
               0.4f, "ray fraction stops one radius before a vertical edge");

    coord2d zero_direction = point2(0.0f, 0.0f);
    tests.near(calculateRayToSegmentIntersectionNormalized(
                   &point, &edge_start, &edge_end, &zero_direction),
               1.0f, "zero direction returns the no-intersection sentinel");

    coord2d zero_edge = point2(5.0f, 0.0f);
    tests.near(calculateRayToSegmentIntersectionNormalized(
                   &point, &zero_edge, &zero_edge, &movement),
               0.4f, "zero-length edge uses the endpoint distance");

    coord2d far_edge_start = point2(5.0f, 10.0f);
    coord2d far_edge_end = point2(5.0f, 20.0f);
    tests.near(calculateRayToSegmentIntersectionNormalized(
                   &point, &far_edge_start, &far_edge_end, &movement),
               1.0f, "edge outside the ray returns the sentinel");

    coord2d parallel_edge_start = point2(-10.0f, 5.0f);
    coord2d parallel_edge_end = point2(10.0f, 5.0f);
    const float parallel_result = calculateRayToSegmentIntersectionNormalized(
        &point, &parallel_edge_start, &parallel_edge_end, &movement);
    tests.near(parallel_result, 1.0f,
                 "parallel edge checks both endpoints");

    coord3d point_beyond_edge = point3(1.0f, 0.0f, 20.0f);
    const float beyond_result = calculateRayToSegmentIntersectionNormalized(
        &point_beyond_edge, &edge_start, &edge_end, &movement);
    tests.near(beyond_result, 1.0f,
                 "ray outside the segment span checks the nearer endpoint");
}

} // namespace

int main()
{
    TestState tests;
    test_segment_fraction(tests);
    test_radius_line_distance(tests);
    test_ray_segment_fraction(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d collision-geometry contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("collision-geometry unit suite passed");
    return 0;
}
