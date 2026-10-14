#define NOMINMAX
#include "book.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
namespace book {
// The pad has one flat rounded outline, independent of page count.
void roundShape(HWND window, UINT dpi, bool headerOnly, bool repaint) {
    RECT r{};
    GetClientRect(window, &r);
    if (r.right <= 0 || r.bottom <= 0)
        return;
    uint64_t dimensions = ((uint64_t)r.right << 32) | (uint32_t)r.bottom;
    uintptr_t mode = ((uintptr_t)dpi << 1) | (headerOnly ? 1 : 0);
    if ((uint64_t)(uintptr_t)GetPropW(window, L"BookRoundSize") == dimensions &&
        (uintptr_t)GetPropW(window, L"BookRoundMode") == mode)
        return;
    SetPropW(window, L"BookRoundSize", (HANDLE)(uintptr_t)dimensions);
    SetPropW(window, L"BookRoundMode", (HANDLE)mode);
    double radius = MulDiv(7, dpi, 96);
    struct P {
        double x, y;
    };
    std::vector<P> vertices{
        {0, 0}, {(double)r.right, 0}, {(double)r.right, (double)r.bottom}, {0, (double)r.bottom}};
    std::vector<POINT> outline;
    for (size_t i = 0; i < vertices.size(); ++i) {
        auto p = vertices[i], prev = vertices[(i + vertices.size() - 1) % vertices.size()],
             next = vertices[(i + 1) % vertices.size()];
        double in = std::hypot(prev.x - p.x, prev.y - p.y),
               out = std::hypot(next.x - p.x, next.y - p.y),
               cut = std::min({radius, in * .4, out * .4});
        P entry{p.x + (prev.x - p.x) * cut / in, p.y + (prev.y - p.y) * cut / in},
            exit{p.x + (next.x - p.x) * cut / out, p.y + (next.y - p.y) * cut / out};
        for (int step = 0; step <= 8; ++step) {
            double t = step / 8.0, k = 1 - t;
            outline.push_back(
                POINT{(LONG)std::lround(k * k * entry.x + 2 * k * t * p.x + t * t * exit.x),
                      (LONG)std::lround(k * k * entry.y + 2 * k * t * p.y + t * t * exit.y)});
        }
    }
    HRGN shape = CreatePolygonRgn(outline.data(), (int)outline.size(), WINDING);
    if (headerOnly) {
        HRGN header = CreateRectRgn(0, 0, r.right, MulDiv(46, dpi, 96));
        CombineRgn(shape, shape, header, RGN_AND);
        DeleteObject(header);
    }
    if (!SetWindowRgn(window, shape, repaint))
        DeleteObject(shape);
}
} // namespace book
