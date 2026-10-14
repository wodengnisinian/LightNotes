#define NOMINMAX
#include "glass.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <memory>
#include <cstring>

namespace glass {
static HRESULT lastError = E_FAIL;
long error() {
    return lastError;
}
template <class T> struct Com {
    T *p = nullptr;
    ~Com() {
        reset();
    }
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    void reset() {
        if (p)
            p->Release();
        p = nullptr;
    }
    T **put() {
        reset();
        return &p;
    }
    T *operator->() const {
        return p;
    }
    explicit operator bool() const {
        return p != nullptr;
    }
};
struct Output {
    HMONITOR monitor{};
    RECT bounds{};
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    Com<IDXGIOutputDuplication> duplicate;
    Com<ID3D11Texture2D> staging;
    std::vector<uint32_t> pixels;
    int width = 0, height = 0;
    uint64_t revision = 0, lastPoll = 0, lastInit = 0;
    bool init() {
        if (GetTickCount64() - lastInit < 1500)
            return false;
        lastInit = GetTickCount64();
        duplicate.reset();
        staging.reset();
        device.reset();
        context.reset();
        pixels.clear();
        revision = 0;
        Com<IDXGIFactory1> factory;
        lastError = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)factory.put());
        if (FAILED(lastError))
            return false;
        for (UINT a = 0;; ++a) {
            Com<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(a, adapter.put()) == DXGI_ERROR_NOT_FOUND)
                break;
            if (!adapter)
                continue;
            for (UINT i = 0;; ++i) {
                Com<IDXGIOutput> out;
                if (adapter->EnumOutputs(i, out.put()) == DXGI_ERROR_NOT_FOUND)
                    break;
                if (!out)
                    continue;
                DXGI_OUTPUT_DESC desc{};
                out->GetDesc(&desc);
                if (desc.Monitor != monitor)
                    continue;
                if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY &&
                    desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
                    lastError = E_NOTIMPL;
                    return false;
                }
                bounds = desc.DesktopCoordinates;
                lastError = D3D11CreateDevice(
                    adapter.p, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                    nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
                if (FAILED(lastError))
                    return false;
                Com<IDXGIOutput1> output;
                if (FAILED(out->QueryInterface(__uuidof(IDXGIOutput1), (void **)output.put())))
                    return false;
                lastError = output->DuplicateOutput(device.p, duplicate.put());
                return SUCCEEDED(lastError);
            }
        }
        lastError = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        return false;
    }
    bool update() {
        if (!duplicate && !init())
            return false;
        auto now = GetTickCount64();
        if (now - lastPoll < 100)
            return !pixels.empty();
        lastPoll = now;
        DXGI_OUTDUPL_FRAME_INFO info{};
        Com<IDXGIResource> resource;
        HRESULT hr = duplicate->AcquireNextFrame(0, &info, resource.put());
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            lastError = pixels.empty() ? hr : S_OK;
            return !pixels.empty();
        }
        if (FAILED(hr)) {
            lastError = hr;
            duplicate.reset();
            pixels.clear();
            return false;
        }
        // A pointer-only update does not change the backdrop; do not recopy or reblur it.
        if (info.LastPresentTime.QuadPart == 0 && !pixels.empty()) {
            duplicate->ReleaseFrame();
            return true;
        }
        Com<ID3D11Texture2D> texture;
        bool ok =
            SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)texture.put()));
        if (ok) {
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (!staging || width != (int)desc.Width || height != (int)desc.Height) {
                width = desc.Width;
                height = desc.Height;
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0;
                ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, staging.put()));
            }
            if (ok) {
                context->CopyResource(staging.p, texture.p);
                D3D11_MAPPED_SUBRESOURCE map{};
                ok = SUCCEEDED(context->Map(staging.p, 0, D3D11_MAP_READ, 0, &map));
                if (ok) {
                    pixels.resize((size_t)width * height);
                    for (int y = 0; y < height; ++y)
                        std::memcpy(pixels.data() + (size_t)y * width,
                                    (char *)map.pData + (size_t)y * map.RowPitch,
                                    (size_t)width * 4);
                    context->Unmap(staging.p, 0);
                    ++revision;
                }
            }
        }
        duplicate->ReleaseFrame();
        if (!ok)
            pixels.clear();
        lastError = ok ? S_OK : E_FAIL;
        return ok;
    }
};
std::vector<std::unique_ptr<Output>> outputs;
void release() {
    outputs.clear();
}
static bool gdiBackdrop(RECT r, Frame &result) {
    int w = r.right - r.left, h = r.bottom - r.top;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *data = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &data, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr), screen = GetDC(nullptr);
    if (!bitmap || !dc || !screen) {
        if (bitmap)
            DeleteObject(bitmap);
        if (dc)
            DeleteDC(dc);
        if (screen)
            ReleaseDC(nullptr, screen);
        return false;
    }
    auto old = SelectObject(dc, bitmap);
    bool ok = BitBlt(dc, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT) != FALSE;
    GdiFlush();
    if (ok) {
        result.width = w;
        result.height = h;
        result.pixels.resize((size_t)w * h);
        std::memcpy(result.pixels.data(), data, result.pixels.size() * 4);
        uint64_t hash = 1469598103934665603ULL;
        for (auto pixel : result.pixels) {
            hash ^= pixel;
            hash *= 1099511628211ULL;
        }
        result.revision = hash;
    }
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return ok;
}
bool backdrop(RECT r, Frame &result) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return false;
    HMONITOR monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
    Output *output = nullptr;
    for (auto &o : outputs)
        if (o->monitor == monitor)
            output = o.get();
    if (!output) {
        auto o = std::make_unique<Output>();
        o->monitor = monitor;
        output = o.get();
        outputs.push_back(std::move(o));
    }
    // DuplicateOutput may not supply an initial frame on a completely static desktop.
    // Use the same exclusion-aware Windows screen capture until the first DXGI frame.
    if (!output->update())
        return gdiBackdrop(r, result);
    result.width = w;
    result.height = h;
    result.revision = output->revision ^ (uint64_t)(uintptr_t)monitor;
    result.pixels.resize((size_t)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sx = std::clamp<int>(r.left + x - output->bounds.left, 0, output->width - 1),
                sy = std::clamp<int>(r.top + y - output->bounds.top, 0, output->height - 1);
            result.pixels[(size_t)y * w + x] = output->pixels[(size_t)sy * output->width + sx];
        }
    return true;
}
// Linear-time separable box blur; three passes approximate a Gaussian blur.
static void pass(const std::vector<uint32_t> &in, std::vector<uint32_t> &out, int w, int h,
                 int radius, bool horizontal) {
    out.resize(in.size());
    int lines = horizontal ? h : w, length = horizontal ? w : h;
    auto index = [&](int line, int p) {
        return horizontal ? (size_t)line * w + p : (size_t)p * w + line;
    };
    int count = radius * 2 + 1;
    for (int line = 0; line < lines; ++line) {
        int sums[3]{};
        auto add = [&](int p, int sign) {
            uint32_t v = in[index(line, std::clamp(p, 0, length - 1))];
            for (int c = 0; c < 3; ++c)
                sums[c] += sign * ((v >> (c * 8)) & 255);
        };
        for (int i = -radius; i <= radius; ++i)
            add(i, 1);
        for (int p = 0; p < length; ++p) {
            out[index(line, p)] = (sums[0] / count) | ((sums[1] / count) << 8) |
                                  ((sums[2] / count) << 16) | 0xff000000u;
            add(p - radius, -1);
            add(p + radius + 1, 1);
        }
    }
}
std::vector<uint32_t> material(const Frame &frame, int radius, int transparency, COLORREF tint,
                               bool smooth) {
    auto image = frame.pixels;
    radius = std::clamp(radius, 0, 24);
    transparency = std::clamp(transparency, 10, 90);
    if (radius > 0) {
        std::vector<uint32_t> scratch;
        int r = std::max(1, radius / 2);
        for (int i = 0; i < 3; ++i) {
            pass(image, scratch, frame.width, frame.height, r, true);
            pass(scratch, image, frame.width, frame.height, r, false);
        }
    }
    int tinted[3] = {(int)GetBValue(tint), (int)GetGValue(tint), (int)GetRValue(tint)};
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            auto &v = image[(size_t)y * frame.width + x];
            uint32_t out = 0xff000000u;
            int sheen = smooth ? std::max(0, 9 - y * 9 / std::max(1, frame.height)) : 2;
            for (int c = 0; c < 3; ++c) {
                int value = ((int)((v >> (8 * c)) & 255) * transparency +
                             tinted[c] * (100 - transparency)) /
                            100;
                value = (value * (100 - sheen) + 255 * sheen) / 100;
                out |= (uint32_t)value << (8 * c);
            }
            v = out;
        }
    return image;
}
} // namespace glass
