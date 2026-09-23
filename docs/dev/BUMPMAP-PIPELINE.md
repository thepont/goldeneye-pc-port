# Offline bump-map generation

tools_pc/create_bumpmaps.sh creates deterministic height maps and tangent-space
normal maps from a legally owned big-endian (.z64) GoldenEye ROM. It reuses the
repository's existing PD texture decoder and the checked-in assets/images.def
and imagelist.u.csv metadata.

    ./tools_pc/create_bumpmaps.sh \
      --rom /path/to/GoldenEye.z64 \
      --out /path/to/goldeneye-bumpmaps

The output directory contains manifest.csv and one .height.png / .normal.png
pair for each eligible texture LOD. The manifest records the ROM hash, source
slice hash, material decision, dimensions, and generated filenames, so
generated files can be traced back to the exact ROM data.

By default the policy processes opaque physical materials such as stone, wood,
metal, dirt, mud, tile, and snow. It excludes UI, font, briefing, effect,
character, glass, and water textures. To include untagged HIT_DEFAULT textures
(still excluding those presentation and unsafe classes), pass:

    ./tools_pc/create_bumpmaps.sh --rom GoldenEye.z64 --out bumpmaps \
      --include-default --strength 2.5

The generated files are local ROM-derived artifacts; do not commit them to the
repository. The PC renderer consumes the `normal_map` entries at runtime when
the enhanced or remaster renderer is selected. The default runtime directory
is `$S/bumpmaps` (normally `data/bumpmaps`), and the relevant configuration is:

    Video.RenderMode=1       # enhanced; use 2 for remaster
    Video.BumpMaps=1
    Video.BumpMapsPath=$S/bumpmaps

The normal PNG is uploaded as raw `GL_RGBA8` data on a separate texture unit;
it is deliberately not uploaded as sRGB. The shader uses the decoded normal
only for a bounded diffuse-light adjustment. It does not add specular,
reflection, or Fresnel terms, so a data-space mistake cannot turn every
material into a wet-looking surface. The runtime records each decompressed
texture's original ROM offset before upload, so heap/RDRAM texture pointers do
not break manifest matching. Missing or invalid sidecars fall back to the
ordinary material texture without changing the base renderer.

For a local end-to-end run:

    ./tools_pc/create_bumpmaps.sh \
      --rom data/ge007.ntsc-final.z64 \
      --out data/bumpmaps
    GE_RENDER_MODE=1 ./build-pc/ge007.x86_64 -level_09
