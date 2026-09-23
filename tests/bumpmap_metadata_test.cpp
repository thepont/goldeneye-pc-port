#include "../tools/mktex/src/bumpmap_metadata.h"

#include <cassert>
#include <cstdlib>
#include <string>

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
    ge_image_def definition = {};
    ge_image_record record = {};

    assert(geBumpParseImageDefLine(
               " IMAGE(  AZTEC_88 , 0xD, HIT_STONE, HIT_METAL, 0, 0, 0, 0 )\n",
               &definition) == 1);
    assert(std::string(definition.name) == "AZTEC_88");
    assert(std::string(definition.hit_sound) == "HIT_STONE");
    assert(std::string(definition.hit_texture) == "HIT_METAL");

    assert(geBumpParseImageDefLine("not an IMAGE row\n", &definition) == 0);
    assert(geBumpParseImageDefLine(nullptr, &definition) == 0);
    assert(geBumpParseImageDefLine("IMAGE(NAME, 0x10, HIT_STONE", &definition) == -1);
    assert(geBumpParseImageDefLine("IMAGE(NAME, 0x10)\n", &definition) == -1);
    assert(geBumpParseImageDefLine(
               "IMAGE(NAME, 0x10, , HIT_STONE, 0)\n", &definition) == -1);
    assert(geBumpParseImageDefLine(
               "IMAGE(NAME, 0x10, HIT_STONE, , 0)\n", &definition) == -1);
    assert(geBumpParseImageDefLine("IMAGE(NAME, 0x10, HIT_STONE, HIT_STONE)\n",
                                   nullptr) == 0);

    std::string long_name(128, 'X');
    std::string long_line = "IMAGE(" + long_name + ", 0x10, HIT_STONE, HIT_STONE)";
    assert(geBumpParseImageDefLine(long_line.c_str(), &definition) == -1);
    std::string long_texture(32, 'Y');
    std::string long_texture_line =
        "IMAGE(NAME, 0x10, HIT_STONE, " + long_texture + ")";
    assert(geBumpParseImageDefLine(long_texture_line.c_str(), &definition) == -1);

    assert(geBumpParseImageRecord("  # comment\n", &record) == 0);
    assert(geBumpParseImageRecord("\t\n", &record) == 0);
    assert(geBumpParseImageRecord(nullptr, &record) == 0);
    assert(geBumpParseImageRecord("9403888,1876,assets/images/image0.bin\n",
                                  &record) == 1);
    assert(record.offset == 9403888);
    assert(record.size == 1876);
    assert(geBumpParseImageRecord("1 , 2 , ignored\n", &record) == 1);
    assert(record.offset == 1);
    assert(record.size == 2);
    assert(geBumpParseImageRecord("not-a-row\n", &record) == -1);
    assert(geBumpParseImageRecord("1;2\n", &record) == -1);
    assert(geBumpParseImageRecord("1,\n", &record) == -1);
    assert(geBumpParseImageRecord("184467440737095516160,2\n", &record) == -1);
    assert(geBumpParseImageRecord("1,2\n", nullptr) == 0);

    return 0;
}
