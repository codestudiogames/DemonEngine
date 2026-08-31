#pragma once
// =============================================================================
//  STUB stb_truetype.h
//  NOTE: Replace this stub with the real stb_truetype.h from:
//  https://github.com/nothings/stb/blob/master/stb_truetype.h
// =============================================================================

// This stub only defines the minimal API used by GUIFont.cpp.
// It enables compilation but does NOT rasterize fonts.

#ifndef STB_TRUETYPE_STUB
#define STB_TRUETYPE_STUB

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stbtt_pack_context {
    int dummy;
} stbtt_pack_context;

typedef struct stbtt_packedchar {
    unsigned short x0, y0, x1, y1;
    float xoff, yoff, xadvance;
} stbtt_packedchar;

typedef struct stbtt_fontinfo {
    int dummy;
} stbtt_fontinfo;

static inline int stbtt_PackBegin(stbtt_pack_context* /*spc*/, unsigned char* /*pixels*/,
                                  int /*width*/, int /*height*/, int /*stride_in_bytes*/,
                                  int /*padding*/, void* /*alloc_context*/) {
    return 1;
}

static inline void stbtt_PackSetOversampling(stbtt_pack_context* /*spc*/, unsigned int /*h_oversample*/, unsigned int /*v_oversample*/) {}

static inline int stbtt_PackFontRange(stbtt_pack_context* /*spc*/, const unsigned char* /*fontdata*/, int /*font_index*/,
                                      float /*font_size*/, int /*first_unicode_char_in_range*/, int num_chars_in_range,
                                      stbtt_packedchar* chardata_for_range) {
    // Populate with zeroed glyphs to keep downstream code safe.
    if (!chardata_for_range) return 0;
    for (int i = 0; i < num_chars_in_range; ++i) {
        chardata_for_range[i].x0 = chardata_for_range[i].y0 = 0;
        chardata_for_range[i].x1 = chardata_for_range[i].y1 = 0;
        chardata_for_range[i].xoff = 0.0f;
        chardata_for_range[i].yoff = 0.0f;
        chardata_for_range[i].xadvance = 0.0f;
    }
    return 1;
}

static inline void stbtt_PackEnd(stbtt_pack_context* /*spc*/) {}

static inline int stbtt_InitFont(stbtt_fontinfo* /*info*/, const unsigned char* /*data*/, int /*offset*/) { return 1; }

static inline void stbtt_GetFontVMetrics(const stbtt_fontinfo* /*info*/, int* ascent, int* descent, int* lineGap) {
    if (ascent) *ascent = 0;
    if (descent) *descent = 0;
    if (lineGap) *lineGap = 0;
}

static inline float stbtt_ScaleForPixelHeight(const stbtt_fontinfo* /*info*/, float /*pixels*/) { return 1.0f; }

#ifdef __cplusplus
}
#endif

#endif // STB_TRUETYPE_STUB
