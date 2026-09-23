#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef GE007_HAVE_FREETYPE_TEST
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace fs = std::filesystem;

static int fail(const std::string &message)
{
    std::cerr << message << '\n';
    return 1;
}

int main()
{
    const fs::path root = fs::path(GE007_SOURCE_DIR);
    const fs::path font = root / "assets" / "fonts" /
        "ScienceGothic[CTRS,slnt,wdth,wght].ttf";
    const fs::path license = root / "assets" / "fonts" / "OFL.txt";

    if (!fs::is_regular_file(font)) {
        return fail("vector UI font asset is missing from the source package");
    }
    if (fs::file_size(font) < 1024 * 1024) {
        return fail("vector UI font asset is unexpectedly truncated");
    }
    if (!fs::is_regular_file(license)) {
        return fail("vector UI font license is missing from the source package");
    }

    std::ifstream stream(font, std::ios::binary);
    std::uint8_t sfnt_header[4] = {};
    stream.read(reinterpret_cast<char *>(sfnt_header), sizeof(sfnt_header));
    if (!stream || sfnt_header[0] != 0x00 || sfnt_header[1] != 0x01 ||
        sfnt_header[2] != 0x00 || sfnt_header[3] != 0x00) {
        return fail("vector UI font asset is not a TrueType font");
    }

#ifdef GE007_HAVE_FREETYPE_TEST
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    if (FT_Init_FreeType(&library) != 0 ||
        FT_New_Face(library, font.c_str(), 0, &face) != 0) {
        if (face != nullptr) {
            FT_Done_Face(face);
        }
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
        return fail("FreeType could not load the packaged vector UI font");
    }
    if (face->num_glyphs < 90 || FT_Set_Pixel_Sizes(face, 0, 13) != 0) {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        return fail("packaged vector UI font has an unusable glyph face");
    }
    /* vector_text.cpp accepts printable ASCII and builds an atlas for that
     * exact range. Exercise the whole contract so a missing punctuation or
     * lower-case glyph cannot turn one menu label into an empty-looking run.
     * Space is intentionally exempt from the bitmap-area check: it has a
     * valid advance but no covered pixels. */
    for (int codepoint = 32; codepoint < 127; ++codepoint) {
        if (FT_Get_Char_Index(face, static_cast<FT_ULong>(codepoint)) == 0 ||
            FT_Load_Char(face, static_cast<FT_ULong>(codepoint), FT_LOAD_RENDER) != 0 ||
            face->glyph->advance.x <= 0 ||
            (codepoint != ' ' &&
             (face->glyph->bitmap.width == 0 || face->glyph->bitmap.rows == 0))) {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return fail("FreeType could not rasterize a printable UI glyph");
        }
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
#endif

    return 0;
}
