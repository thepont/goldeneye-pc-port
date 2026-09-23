#include "model_render_pixel_oracle.h"

#include <iostream>
#include <string>

using geModelPixelOracle::CaptureResult;
using geModelPixelOracle::fill_region;
using geModelPixelOracle::inspect;
using geTestImage::Image;
using geTestImage::load_ppm;

static int self_test()
{
    Image               black        = geModelPixelOracle::black_fixture(508, 380);
    const CaptureResult black_result = inspect(black);
    if (black_result.passes())
    {
        std::cerr << "model pixel oracle accepted a black player and prop\n";
        return 1;
    }

    Image colored = black;
    fill_region(colored, geModelPixelOracle::kPlayerModelInTopViewport, 25, 130, 165);
    fill_region(colored, geModelPixelOracle::kPlayerModelInBottomViewport, 25, 130, 165);
    fill_region(colored, geModelPixelOracle::kGreenPropRegion, 28, 86, 30);
    const CaptureResult colored_result = inspect(colored);
    if (!colored_result.passes())
    {
        std::cerr << "model pixel oracle rejected colored fixture: top-player="
                  << colored_result.top_view_player_color_pixels
                  << " bottom-player="
                  << colored_result.bottom_view_player_color_pixels << " prop="
                  << colored_result.green_prop_pixels << '\n';
        return 1;
    }

    Image missing_top_player = black;
    fill_region(missing_top_player,
                geModelPixelOracle::kPlayerModelInBottomViewport,
                25,
                130,
                165);
    fill_region(missing_top_player, geModelPixelOracle::kGreenPropRegion, 28, 86, 30);
    if (inspect(missing_top_player).passes())
    {
        std::cerr << "model pixel oracle accepted a missing top-viewport player\n";
        return 1;
    }

    Image missing_bottom_player = black;
    fill_region(missing_bottom_player,
                geModelPixelOracle::kPlayerModelInTopViewport,
                25,
                130,
                165);
    fill_region(missing_bottom_player, geModelPixelOracle::kGreenPropRegion, 28, 86, 30);
    if (inspect(missing_bottom_player).passes())
    {
        std::cerr << "model pixel oracle accepted a missing bottom-viewport player\n";
        return 1;
    }

    Image missing_prop = black;
    fill_region(missing_prop, geModelPixelOracle::kPlayerModelInTopViewport, 25, 130, 165);
    fill_region(missing_prop,
                geModelPixelOracle::kPlayerModelInBottomViewport,
                25,
                130,
                165);
    if (inspect(missing_prop).passes())
    {
        std::cerr << "model pixel oracle accepted a black world prop\n";
        return 1;
    }

    std::cout << "MODEL_PIXEL_ORACLE_SELF_TEST: black player/prop regions rejected; colored accepted\n";
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::string(argv[1]) == "--self-test") return self_test();
    if (argc != 3 || std::string(argv[1]) != "--check-coop-dam")
    {
        std::cerr << "usage: model_render_pixel_oracle_test "
                     "--self-test | --check-coop-dam capture.ppm\n";
        return 2;
    }

    Image image;
    if (!load_ppm(argv[2], image))
    {
        std::cerr << "model pixel oracle could not load a complete RGB PPM\n";
        return 2;
    }
    const CaptureResult result = inspect(image);
    std::cout << "MODEL_RENDER_PIXELS: top_player="
              << result.top_view_player_color_pixels << " bottom_player="
              << result.bottom_view_player_color_pixels << " green_prop="
              << result.green_prop_pixels
              << " dimensions=" << image.width << 'x' << image.height << '\n';
    return result.passes() ? 0 : 1;
}
