.DEFAULT_GOAL := default
.PHONY: default tex2png create_bumpmaps clean

TEX2PNG_C_FILES := \
    src/tex2png.c \
    src/libpdtex/pdtex.c \
    src/libpdtex/reader.c \
    src/libpdtex/writer.c

BUMPMAP_C_FILES := \
    src/create_bumpmaps.c \
    src/bumpmap_decode.c \
    src/bumpmap_metadata.c \
    src/libpdtex/pdtex.c \
    src/libpdtex/reader.c \
    src/libpdtex/writer.c

# Compile tex2png, ensuring the build directory exists first
tex2png: $(TEX2PNG_C_FILES)
	mkdir -p build
	gcc $(TEX2PNG_C_FILES) -lpng -lz -O3 -o build/tex2png

create_bumpmaps: $(BUMPMAP_C_FILES) src/bumpmap_policy.h
	mkdir -p build
	gcc $(BUMPMAP_C_FILES) -I src -lpng -lz -lm -O3 -o build/create_bumpmaps

clean:
	rm -f build/tex2png build/create_bumpmaps

default: tex2png create_bumpmaps
