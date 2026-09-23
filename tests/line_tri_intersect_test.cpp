/* Deterministic tests for the game's ray/triangle collision helper. */
#include <cmath>
#include <cstdint>
#include <cstdio>

extern "C" {
struct coord3d
{
    float x;
    float y;
    float z;
};

struct Vertex
{
    struct
    {
        int16_t x;
        int16_t y;
        int16_t z;
    } coord;
    int16_t index;
    uint32_t linked_to;
    uint32_t collision_related_node;
    uint8_t color[4];
};

struct HitThing
{
    coord3d hitpos;
    coord3d normal;
    void *vtx0;
    void *vtx1;
    void *vtx2;
    void *tricmd;
    int16_t unk28;
    int16_t texturenum;
    int16_t tileformat;
    int16_t tilesize;
};

bool intersectRayTriangle(Vertex *vertex0, Vertex *vertex1, Vertex *vertex2,
                          coord3d *vertex_offset, coord3d *ray_start,
                          coord3d *line_point, coord3d *ray_direction,
                          HitThing *hit);
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

Vertex vertex(int16_t x, int16_t y, int16_t z)
{
    Vertex result = {};
    result.coord.x = x;
    result.coord.y = y;
    result.coord.z = z;
    return result;
}

coord3d point(float x, float y, float z)
{
    return {x, y, z};
}

void test_hit_and_output(TestState &tests)
{
    Vertex v0 = vertex(0, 0, 0);
    Vertex v1 = vertex(10, 0, 0);
    Vertex v2 = vertex(0, 10, 0);
    coord3d offset = point(0.0f, 0.0f, 0.0f);
    coord3d ray_start = point(2.0f, 3.0f, -2.0f);
    coord3d line_point = point(2.0f, 3.0f, -1.0f);
    coord3d ray_direction = point(0.0f, 0.0f, 1.0f);
    HitThing hit = {};

    tests.expect(intersectRayTriangle(
                     &v0, &v1, &v2, &offset, &ray_start, &line_point,
                     &ray_direction, &hit),
                 "ray hits triangle interior");
    tests.near(hit.hitpos.x, 2.0f, "triangle hit x");
    tests.near(hit.hitpos.y, 3.0f, "triangle hit y");
    tests.near(hit.hitpos.z, 0.0f, "triangle hit z");
    tests.near(hit.normal.x, 0.0f, "triangle normal x");
    tests.near(hit.normal.y, 0.0f, "triangle normal y");
    tests.near(hit.normal.z, 100.0f, "triangle unnormalized normal z");
    tests.expect(hit.texturenum == 0,
                 "triangle hit initializes the texture number");
}

void test_rejection_paths(TestState &tests)
{
    Vertex v0 = vertex(0, 0, 0);
    Vertex v1 = vertex(10, 0, 0);
    Vertex v2 = vertex(0, 10, 0);
    coord3d offset = point(0.0f, 0.0f, 0.0f);
    coord3d direction = point(0.0f, 0.0f, 1.0f);
    HitThing hit = {};

    coord3d start = point(8.0f, 8.0f, -2.0f);
    coord3d line = point(8.0f, 8.0f, -1.0f);
    tests.expect(!intersectRayTriangle(&v0, &v1, &v2, &offset, &start,
                                       &line, &direction, &hit),
                 "ray rejects an intersection outside triangle edges");

    start = point(2.0f, 3.0f, 1.0f);
    line = point(2.0f, 3.0f, -1.0f);
    tests.expect(!intersectRayTriangle(&v0, &v1, &v2, &offset, &start,
                                       &line, &direction, &hit),
                 "ray rejects an intersection behind ray start");

    coord3d parallel = point(1.0f, 0.0f, 0.0f);
    start = point(2.0f, 3.0f, -1.0f);
    tests.expect(!intersectRayTriangle(&v0, &v1, &v2, &offset, &start,
                                       &line, &parallel, &hit),
                 "parallel line rejects a plane intersection");
}

void test_projection_fallbacks(TestState &tests)
{
    coord3d offset = point(0.0f, 0.0f, 0.0f);
    coord3d direction = point(1.0f, 0.0f, 0.0f);
    coord3d start = point(-1.0f, 2.0f, 3.0f);
    coord3d line = point(-1.0f, 2.0f, 3.0f);
    HitThing hit = {};

    /* The XY projection is degenerate; the YZ projection is usable. */
    Vertex yz0 = vertex(0, 0, 0);
    Vertex yz1 = vertex(0, 10, 0);
    Vertex yz2 = vertex(0, 0, 10);
    tests.expect(intersectRayTriangle(
                     &yz0, &yz1, &yz2, &offset, &start, &line, &direction,
                     &hit),
                 "triangle uses YZ barycentric projection fallback");

    /* Both XY and YZ projections are degenerate; XZ remains usable. */
    direction = point(0.0f, 1.0f, 0.0f);
    start = point(2.0f, -1.0f, 3.0f);
    line = point(2.0f, -1.0f, 3.0f);
    Vertex xz0 = vertex(0, 0, 0);
    Vertex xz1 = vertex(10, 0, 0);
    Vertex xz2 = vertex(0, 0, 10);
    tests.expect(intersectRayTriangle(
                     &xz0, &xz1, &xz2, &offset, &start, &line, &direction,
                     &hit),
                 "triangle uses XZ barycentric projection fallback");
}

} // namespace

int main()
{
    TestState tests;
    test_hit_and_output(tests);
    test_rejection_paths(tests);
    test_projection_fallbacks(tests);

    if (tests.failures != 0)
    {
        std::fprintf(stderr, "%d ray-triangle contract(s) failed\n",
                     tests.failures);
        return 1;
    }
    std::puts("ray-triangle unit suite passed");
    return 0;
}
