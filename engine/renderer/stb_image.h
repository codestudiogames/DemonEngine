#pragma once
// ==============================================================================
//  DemonEngine::stb_image wrapper
//  NOTE: This is a minimal wrapper interface. For real image loading,
//  replace this file with the official stb_image.h and update stb_image.cpp
//  to use STB_IMAGE_IMPLEMENTATION.
// ==============================================================================
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

struct StbImageData {
    unsigned char* pixels;
    int w;
    int h;
    int channels;
};

StbImageData stb_load_rgba(const char* path);
void         stb_free(unsigned char* pixels);

#ifdef __cplusplus
}
#endif
