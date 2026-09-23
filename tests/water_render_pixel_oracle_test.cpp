/* Small, dependency-free PPM oracles used by the display-backed water BDD.
 * These intentionally measure image relationships rather than assert that a
 * framebuffer merely contains non-black pixels. */
#include "water_pixel_oracle.h"

#include <iostream>
#include <string>

using geWaterPixelOracle::Image;
using geWaterPixelOracle::edge_count;
using geWaterPixelOracle::load_ppm;
using geWaterPixelOracle::mean_difference;
using geWaterPixelOracle::roi_variance;
using geWaterPixelOracle::self_test_image;
using geWaterPixelOracle::vertical_asymmetry;

static int usage()
{
    std::cerr << "usage: water_render_pixel_oracle_test "
                 "--variance|--reflection-asymmetry|--occluder-mask|"
                 "--viewport-isolation image.ppm | --temporal a.ppm b.ppm\n";
    return 2;
}

static int self_test()
{
    const Image image = self_test_image(32, 32, false);
    const Image changed = self_test_image(32, 32, true);
    const float variance = roi_variance(image, image.height / 2,
                                        image.height);
    const float asymmetry = vertical_asymmetry(image);
    const std::size_t edges = edge_count(image, image.height / 2,
                                         image.height);
    const float temporal = mean_difference(image, changed, 0, image.height);

    if (!(variance > 0.00001f) || !(asymmetry > 0.002f) || edges <= 8 ||
        !(temporal > 0.002f)) {
        std::cerr << "water pixel oracle self-test failed variance=" << variance
                  << " asymmetry=" << asymmetry << " edges=" << edges
                  << " temporal=" << temporal << '\n';
        return 1;
    }
    std::cerr << "water pixel oracle self-test passed\n";
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
        return self_test();
    }
    if (argc < 3) return usage();
    const std::string mode = argv[1];
    if (mode == "--temporal") {
        if (argc != 4) return usage();
        Image before, after;
        if (!load_ppm(argv[2], before) || !load_ppm(argv[3], after)) return 2;
        const float difference = mean_difference(before, after, 0,
                                                 before.height);
        std::cerr << "water temporal difference=" << difference << '\n';
        return difference > 0.002f ? 0 : 1;
    }

    if (argc != 3) return usage();
    Image image;
    if (!load_ppm(argv[2], image)) return 2;
    if (mode == "--variance") {
        const float variance = roi_variance(image, image.height / 2,
                                            image.height);
        std::cerr << "water pixel variance=" << variance << '\n';
        return variance > 0.00001f ? 0 : 1;
    }
    if (mode == "--reflection-asymmetry") {
        const float asymmetry = vertical_asymmetry(image);
        std::cerr << "water reflection asymmetry=" << asymmetry << '\n';
        return asymmetry > 0.002f ? 0 : 1;
    }
    if (mode == "--occluder-mask") {
        const std::size_t edges = edge_count(image, image.height / 2,
                                             image.height);
        std::cerr << "water occluder mask edges=" << edges << '\n';
        return edges > 8 ? 0 : 1;
    }
    if (mode == "--viewport-isolation") {
        const int split = image.height / 2;
        const float top = roi_variance(image, 0, split);
        const float bottom = roi_variance(image, split, image.height);
        std::cerr << "water viewport isolation top=" << top
                  << " bottom=" << bottom << '\n';
        return top > 0.00001f && bottom > 0.00001f &&
                       std::fabs(top - bottom) > 0.000001f
                   ? 0
                   : 1;
    }
    return usage();
}
