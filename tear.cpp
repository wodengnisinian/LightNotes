#define NOMINMAX
#include "tear.h"
#include <cmath>
#include <algorithm>
#include <cstring>
namespace tear {
Animation::~Animation() {
    if (dc && previous)
        SelectObject(dc, previous);
    if (bitmap)
        DeleteObject(bitmap);
    if (dc)
        DeleteDC(dc);
}
std::unique_ptr<Animation> capture(HWND window, double velocity) {
    RECT rect{};
    GetWindowRect(window, &rect);
    auto a = std::make_unique<Animation>();
    a->width = rect.right - rect.left;
    a->height = rect.bottom - rect.top;
    a->left = rect.left;
    a->top = rect.top;
    a->horizontalVelocity = velocity;
    a->seam = MulDiv(46, GetDpiForWindow(window), 96);
    if (a->width <= 0 || a->height <= 0 || a->width > 2500 || a->height > 2500)
        return {};
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = a->width;
    bi.bmiHeader.biHeight = -a->height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *data = nullptr;
    HBITMAP snap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &data, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!snap || !dc) {
        if (snap)
            DeleteObject(snap);
        if (dc)
            DeleteDC(dc);
        return {};
    }
    auto old = SelectObject(dc, snap);
    DWORD affinity = 0;
    GetWindowDisplayAffinity(window, &affinity);
    SetWindowDisplayAffinity(window, WDA_NONE);
    bool ok = PrintWindow(window, dc, 0) != FALSE;
    SetWindowDisplayAffinity(window, affinity);
    if (ok) {
        a->paper.resize((size_t)a->width * a->height);
        std::memcpy(a->paper.data(), data, a->paper.size() * 4);
        HRGN shape = CreateRectRgn(0, 0, 0, 0);
        bool shaped = GetWindowRgn(window, shape) != ERROR;
        for (int y = 0; y < a->height; ++y)
            for (int x = 0; x < a->width; ++x) {
                auto &pixel = a->paper[(size_t)y * a->width + x];
                pixel = shaped && !PtInRegion(shape, x, y) ? 0 : pixel | 0xff000000u;
            }
        DeleteObject(shape);
    }
    SelectObject(dc, old);
    DeleteObject(snap);
    DeleteDC(dc);
    if (!ok)
        return {};
    a->canvas = (int)std::ceil(std::hypot(a->width, a->height)) + 8;
    bi.bmiHeader.biWidth = a->canvas;
    bi.bmiHeader.biHeight = -a->canvas;
    a->bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void **)&a->pixels, nullptr, 0);
    a->dc = CreateCompatibleDC(nullptr);
    if (!a->bitmap || !a->dc)
        return {};
    a->previous = SelectObject(a->dc, a->bitmap);
    a->started = GetTickCount64();
    return a;
}

struct Vertex {
    double x, y, u, v;
};
static void triangle(Animation &a, Vertex p, Vertex q, Vertex r, double alpha) {
    double det = (q.y - r.y) * (p.x - r.x) + (r.x - q.x) * (p.y - r.y);
    if (std::abs(det) < 0.01)
        return;
    int x0 = std::max(0, (int)std::floor(std::min({p.x, q.x, r.x}))),
        x1 = std::min(a.canvas - 1, (int)std::ceil(std::max({p.x, q.x, r.x})));
    int y0 = std::max(0, (int)std::floor(std::min({p.y, q.y, r.y}))),
        y1 = std::min(a.canvas - 1, (int)std::ceil(std::max({p.y, q.y, r.y})));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            double b = ((q.y - r.y) * (x - r.x) + (r.x - q.x) * (y - r.y)) / det,
                   c = ((r.y - p.y) * (x - r.x) + (p.x - r.x) * (y - r.y)) / det, d = 1 - b - c;
            if (b < -.001 || c < -.001 || d < -.001)
                continue;
            int u = std::clamp((int)(b * p.u + c * q.u + d * r.u), 0, a.width - 1),
                v = std::clamp((int)(b * p.v + c * q.v + d * r.v), a.seam, a.height - 1);
            int edge = a.seam + 2 + ((u * 17 + u / 7) % 5);
            if (v < edge)
                continue;
            uint32_t src = a.paper[(size_t)v * a.width + u];
            if (!(src >> 24))
                continue;
            uint32_t out = (uint32_t)(255 * alpha) << 24;
            for (int k = 0; k < 3; ++k) {
                int value = (src >> (k * 8)) & 255;
                out |= (uint32_t)(value * alpha) << (k * 8);
            }
            a.pixels[(size_t)y * a.canvas + x] = out;
        }
}
bool step(HWND window, Animation &a) {
    double t = (GetTickCount64() - a.started) / 1000.0;
    if (t >= 2.05)
        return false;
    double peel = std::clamp(t / .72, 0.0, 1.0), eased = peel * peel * (3 - 2 * peel),
           fall = std::max(0.0, t - .72);
    double direction = a.horizontalVelocity < 0 ? -1 : 1,
           angle = direction * (.045 * eased + .32 * fall + .055 * std::sin(fall * 7));
    double cs = std::cos(angle), sn = std::sin(angle),
           fade = std::clamp(1 - std::max(0.0, fall - .7) / .63, 0.0, 1.0);
    std::fill(a.pixels, a.pixels + (size_t)a.canvas * a.canvas, 0);
    // The binding remains stationary as the tear moves across the perforation.
    auto vertex = [&](double u, double v) {
        double xn = u / a.width, yn = (v - a.seam) / (a.height - a.seam);
        double released = std::clamp((eased - xn) * 5, 0.0, 1.0);
        if (peel >= 1)
            released = 1;
        double bend = std::sin(yn * 3.14159265) * released * (1 - peel * .65);
        double dx = u - a.width / 2.0 - 18 * bend,
               dy = v - a.height / 2.0 + released * (24 * eased + 26 * yn * eased) + 10 * bend;
        double scale = 1 - .12 * released * eased * yn;
        return Vertex{a.canvas / 2.0 + (cs * dx - sn * dy) * scale,
                      a.canvas / 2.0 + sn * dx + cs * dy, u, v};
    };
    constexpr int nx = 28, ny = 32;
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            double u0 = a.width * x / (double)nx, u1 = a.width * (x + 1) / (double)nx,
                   v0 = a.seam + (a.height - a.seam) * y / (double)ny,
                   v1 = a.seam + (a.height - a.seam) * (y + 1) / (double)ny;
            auto p = vertex(u0, v0), q = vertex(u1, v0), r = vertex(u1, v1), s = vertex(u0, v1);
            triangle(a, p, q, r, fade);
            triangle(a, p, r, s, fade);
        }
    POINT destination{a.left + a.width / 2 - a.canvas / 2 +
                          (int)(direction * (35 * fall + 18 * std::sin(fall * 3))),
                      a.top + a.height / 2 - a.canvas / 2 + (int)(80 * fall + 680 * fall * fall)},
        source{};
    SIZE size{a.canvas, a.canvas};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    bool ok = UpdateLayeredWindow(window, screen, &destination, &size, a.dc, &source, 0, &blend,
                                  ULW_ALPHA) != FALSE;
    ReleaseDC(nullptr, screen);
    return ok;
}
} // namespace tear
