// ==============================================================================
//  DemonEngine::stb_image.cpp
//
//  Windows WIC-backed image loader (RGBA8).
//  Supports PNG/JPG/JPEG/BMP/TGA via WIC codecs.
// ==============================================================================
#include "stb_image.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <string>

static std::wstring utf8ToWide(const char* s)
{
    if (!s) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) {
        len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
        if (len <= 0) return {};
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s, -1, out.data(), len);
        return out;
    }
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

extern "C" StbImageData stb_load_rgba(const char* path)
{
    StbImageData out{};
    if (!path) return out;

    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInit = SUCCEEDED(hrCo);
    if (hrCo == RPC_E_CHANGED_MODE) {
        // COM already initialized with different model; continue without uninit.
        coInit = false;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    UINT w = 0, h = 0;

    std::wstring wpath = utf8ToWide(path);
    bool ok = false;

    do {
        if (wpath.empty()) break;

        hrCo = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&factory));
        if (FAILED(hrCo) || !factory) break;

        hrCo = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr,
                                                  GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
        if (FAILED(hrCo) || !decoder) break;

        hrCo = decoder->GetFrame(0, &frame);
        if (FAILED(hrCo) || !frame) break;

        hrCo = factory->CreateFormatConverter(&converter);
        if (FAILED(hrCo) || !converter) break;

        hrCo = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                      WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        if (FAILED(hrCo)) break;

        converter->GetSize(&w, &h);
        if (w == 0 || h == 0) break;

        size_t size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        out.pixels = static_cast<unsigned char*>(std::malloc(size));
        if (!out.pixels) break;

        hrCo = converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(size), out.pixels);
        if (FAILED(hrCo)) {
            std::free(out.pixels);
            out.pixels = nullptr;
            break;
        }

        out.w = static_cast<int>(w);
        out.h = static_cast<int>(h);
        out.channels = 4;
        ok = true;
    } while (false);

    if (!ok && out.pixels) {
        std::free(out.pixels);
        out.pixels = nullptr;
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (coInit) CoUninitialize();
    return out;
}

extern "C" void stb_free(unsigned char* pixels)
{
    if (pixels) std::free(pixels);
}

#else
extern "C" StbImageData stb_load_rgba(const char* /*path*/) { return {}; }
extern "C" void stb_free(unsigned char* /*pixels*/) {}
#endif
