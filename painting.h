#pragma once
#include <windows.h>

// Every paint pass owns and releases its DCs; present only the finished pixels.
class PaintBuffer {
    HDC destination{}, memory{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    RECT bounds{};

  public:
    PaintBuffer(HDC target, RECT rect) : destination(target), bounds(rect) {
        if (rect.right <= 0 || rect.bottom <= 0)
            return;
        memory = CreateCompatibleDC(target);
        if (memory)
            bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
        if (bitmap)
            previous = SelectObject(memory, bitmap);
        else if (memory) {
            DeleteDC(memory);
            memory = nullptr;
        }
    }
    PaintBuffer(const PaintBuffer &) = delete;
    PaintBuffer &operator=(const PaintBuffer &) = delete;
    HDC dc() const {
        return memory ? memory : destination;
    }
    void present() {
        if (memory)
            BitBlt(destination, 0, 0, bounds.right, bounds.bottom, memory, 0, 0, SRCCOPY);
    }
    ~PaintBuffer() {
        if (memory) {
            SelectObject(memory, previous);
            DeleteObject(bitmap);
            DeleteDC(memory);
        }
    }
};

// Deferred top-level repainting must also restore the background it uncovered.
// Limit the invalidation to old bounds minus new bounds; never refresh the whole desktop.
void repaintVacated(HWND window, const RECT &previous) {
    HWND desktop = GetDesktopWindow();
    RECT old = previous, current{};
    GetWindowRect(window, &current);
    MapWindowPoints(nullptr, desktop, (POINT *)&old, 2);
    MapWindowPoints(nullptr, desktop, (POINT *)&current, 2);
    HRGN exposed = CreateRectRgn(old.left, old.top, old.right, old.bottom),
         covered = CreateRectRgn(current.left, current.top, current.right, current.bottom);
    if (exposed && covered && CombineRgn(exposed, exposed, covered, RGN_DIFF) > NULLREGION)
        RedrawWindow(desktop, nullptr, exposed,
                     RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    if (exposed)
        DeleteObject(exposed);
    if (covered)
        DeleteObject(covered);
}
