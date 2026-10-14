#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <functional>
#include "glass.h"
#include "tear.h"
#include "book.h"
#include "painting.h"
#include "storage.h"

namespace fs = std::filesystem;
constexpr UINT TrayMessage = WM_APP + 1;
constexpr int New = 101, Pin = 102, Lock = 106, Edit = 110, Status = 130;
constexpr int ShowAll = 901, HideAll = 902, Quit = 903;
constexpr int Restore = 904, OpenSettings = 905, Tear = 107;
constexpr int NextCard = 108, PreviousCard = 109, NextStack = 906;
constexpr int NoteSettings = 146, NoteStyle = 147, NoteExit = 148, PageCounter = 152,
              TaskGutter = 153;
constexpr int OpenStyle = 907;
constexpr int MagicV1 = 0x4E4F5431, MagicV2 = 0x4E4F5432, MagicV3 = 0x4E4F5433;
struct TaskRange {
    int begin, end, after;
};
struct Note {
    HWND win{}, edit{}, gutter{}, status{}, tooltip{};
    HFONT font{}, smallFont{};
    HBRUSH brush{};
    fs::path file;
    std::wstring text, message = L"已保存", taskSnapshot;
    std::vector<TaskRange> tasks;
    bool tasksDirty = true, taskInside = false;
    int taskPressed = -1;
    uint64_t taskRevision = 0, taskPressRevision = 0;
    int color = 0, fontSize = 18, x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 410, h = 560;
    int material = 0, blur = 12, transparency = 50;
    bool glassReady = false, excluded = false;
    HBITMAP surface{};
    uint64_t glassRevision = 0;
    RECT glassRect{};
    HWND tearWindow{};
    std::unique_ptr<tear::Animation> animation;
    POINT tearGrab{};
    bool gripping = false;
    int pull = 0, dragX = 0;
    bool counted = true, resizing = false, moving = false;
    POINT resizeGrab{};
    RECT resizeRect{};
    int resizeHit = 0;
    bool locked = false, ready = false, pendingSave = false;
    long long due = 0;
    UINT dpi = 96;
};
std::vector<std::unique_ptr<Note>> notes;
fs::path folder;
HWND controller{};
HICON appIcon{};
NOTIFYICONDATAW tray{};
bool trayAvailable = false, quitting = false;
bool testMode = false;
uint64_t capturePauseUntil = 0;
void testProperty(HWND window, const wchar_t *name, HANDLE value) {
    if (testMode)
        SetPropW(window, name, value);
}
void clearTestProperty(HWND window, const wchar_t *name) {
    if (testMode)
        RemovePropW(window, name);
}
void testCount(HWND window, const wchar_t *name) {
    if (testMode)
        SetPropW(window, name, (HANDLE)((uintptr_t)GetPropW(window, name) + 1));
}
HWND settingsWindow{}, styleWindow{}, geometryWindow{};
bool initialSettings = true;
HWND stackTop{}, displayedPage{};
struct Preferences {
    int corner = 1, material = 1, blur = 12, transparency = 50, fontSize = 18, color = 0, width = 0,
        height = 0, left = -100001, top = -100001;
    bool fixed = false, sound = true, lowPower = false, remember = true, topmost = false,
         autostart = false;
} preferences;
#include "startup.h"
void syncPanel(HWND hwnd);
void syncGeometry(HWND hwnd);
void openGeometry();
bool save(Note &n);
COLORREF colors[] = {RGB(255, 248, 211), RGB(224, 244, 230), RGB(225, 238, 255),
                     RGB(246, 228, 244), RGB(239, 235, 255), RGB(240, 241, 244)};
int px(const Note &n, int v) {
    return MulDiv(v, n.dpi, 96);
}
void refresh(Note &n);
void fonts(Note &n);
void updateGlass(Note &n, bool repaint = true, const RECT *bounds = nullptr);
void updatePageControls(Note &n);
void layout(Note &n);
void invalidateTasks(Note &n, bool changed = false);
void paintTaskLines(Note &n, HDC dc);
void createNote(fs::path file = {});
void anchorAll();
void switchStack(int direction = 1);
void restoreRecent();
void startTear(Note &n);
void paperBrush(Note &n);
bool saveSession();
void restoreSession();
void showAll() {
    initialSettings = false;
    anchorAll();
    if (stackTop) {
        SetForegroundWindow(stackTop);
        HWND editor = GetDlgItem(stackTop, Edit);
        if (IsWindowVisible(editor))
            SetFocus(editor);
        syncPadTopmost();
    }
}
void showPanel(HWND window) {
    ShowWindow(window, SW_SHOWNORMAL);
    SetForegroundWindow(window);
    syncTopmost(window);
}
void openSettings() {
    ShowWindow(geometryWindow, SW_HIDE);
    ShowWindow(styleWindow, SW_HIDE);
    syncPanel(settingsWindow);
    showPanel(settingsWindow);
}
void openStyle() {
    ShowWindow(geometryWindow, SW_HIDE);
    ShowWindow(settingsWindow, SW_HIDE);
    syncPanel(styleWindow);
    showPanel(styleWindow);
}
std::wstring readText(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(edit, s.data(), len + 1);
    s.resize(len);
    return s;
}
std::pair<int, int> pageNumbers() {
    int total = 0, current = 0;
    for (auto &page : notes)
        if (page->counted) {
            ++total;
            if (page->win == stackTop && !page->animation)
                current = total;
        }
    return current ? std::pair{current, total} : std::pair{0, 0};
}
void loadPreferences() {
    auto path = folder / L"settings.ini";
    auto get = [&](const wchar_t *key, int fallback, int low, int high) {
        return std::clamp((int)GetPrivateProfileIntW(L"appearance", key, fallback, path.c_str()),
                          low, high);
    };
    preferences.corner = get(L"corner", 1, 0, 4);
    preferences.material = get(L"material", 1, 0, 2);
    preferences.blur = get(L"blur", 12, 0, 24);
    preferences.transparency = get(L"transparency", 50, 10, 90);
    preferences.fontSize = get(L"font", 18, 14, 30);
    preferences.color = get(L"color", 0, 0, 5);
    preferences.fixed = get(L"fixed", 0, 0, 1) != 0;
    preferences.sound = get(L"sound", 1, 0, 1) != 0;
    preferences.lowPower = get(L"lowPower", 0, 0, 1) != 0;
    preferences.width = get(L"width", 0, 0, 2000);
    preferences.height = get(L"height", 0, 0, 2000);
    preferences.left = get(L"left", -100001, -100001, 100000);
    preferences.top = get(L"top", -100001, -100001, 100000);
    preferences.remember = get(L"remember", 1, 0, 1) != 0;
    preferences.topmost = get(L"topmost", 0, 0, 1) != 0;
    preferences.autostart = startupEnabled();
}
bool savePreferences() {
    auto &p = preferences;
    std::string data =
        "[appearance]\r\ncorner=" + std::to_string(p.corner) +
        "\r\nmaterial=" + std::to_string(p.material) + "\r\nblur=" + std::to_string(p.blur) +
        "\r\ntransparency=" + std::to_string(p.transparency) +
        "\r\nfont=" + std::to_string(p.fontSize) + "\r\ncolor=" + std::to_string(p.color) +
        "\r\nfixed=" + std::to_string(p.fixed) + "\r\nsound=" + std::to_string(p.sound) +
        "\r\nlowPower=" + std::to_string(p.lowPower) + "\r\nwidth=" + std::to_string(p.width) +
        "\r\nheight=" + std::to_string(p.height) + "\r\nleft=" + std::to_string(p.left) +
        "\r\ntop=" + std::to_string(p.top) + "\r\n";
    data += "remember=" + std::to_string(p.remember) + "\r\n";
    data += "topmost=" + std::to_string(p.topmost) + "\r\n";
    auto file = folder / L"settings.ini", tmp = folder / L"settings.ini.tmp";
    return writeTemporary(tmp, data.data(), (DWORD)data.size()) &&
           MoveFileExW(tmp.c_str(), file.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

#include "stack.h"
bool rememberGeometry(Note &n, bool moved) {
    if (preferences.fixed && moved)
        return false;
    auto previous = preferences;
    RECT r{};
    GetWindowRect(n.win, &r);
    preferences.width = MulDiv(r.right - r.left, 96, n.dpi);
    preferences.height = MulDiv(r.bottom - r.top, 96, n.dpi);
    if (!preferences.fixed) {
        preferences.left = r.left;
        preferences.top = r.top;
    }
    if (moved)
        preferences.corner = 4;
    if (!savePreferences()) {
        preferences = previous;
        MessageBoxW(n.win, L"位置和大小保存失败，请检查磁盘或权限。", L"轻笺",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    anchorAll();
    if (n.material && IsWindowVisible(n.win))
        updateGlass(n);
    GetWindowRect(n.win, &r);
    if (preferences.corner == 4 && (preferences.left != r.left || preferences.top != r.top)) {
        preferences.left = r.left;
        preferences.top = r.top;
        savePreferences();
    }
    bool ok = true;
    for (auto &page : notes)
        if (page->win && page->ready && !page->animation && page->counted)
            if (!save(*page))
                ok = false;
    return ok;
}
// Read into a temporary model; reject incomplete data before changing live state.
bool readFile(const fs::path &file, Note &n) {
    std::error_code ec;
    auto size = fs::file_size(file, ec);
    if (ec || size < 36 || size > 1000100)
        return false;
    std::ifstream in(file, std::ios::binary);
    int magic = 0;
    in.read((char *)&magic, 4);
    if (magic != MagicV1 && magic != MagicV2 && magic != MagicV3)
        return false;
    int c, p, x, y, w, h;
    for (int *v : {&c, &p, &x, &y, &w, &h})
        in.read((char *)v, 4);
    long long due = 0;
    in.read((char *)&due, 8);
    int locked = 0, font = 18;
    if (magic != MagicV1) {
        in.read((char *)&locked, 4);
        in.read((char *)&font, 4);
    }
    int material = 0, blur = 12, transparency = 50;
    if (magic == MagicV3) {
        in.read((char *)&material, 4);
        in.read((char *)&blur, 4);
        in.read((char *)&transparency, 4);
    }
    if (!in || c < 0 || c >= 6 || (p != 0 && p != 1) || (locked != 0 && locked != 1) || due < 0 ||
        due > 32503680000LL)
        return false;
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() % 2)
        return false;
    std::wstring text(bytes.size() / 2, L'\0');
    if (!bytes.empty())
        std::memcpy(text.data(), bytes.data(), bytes.size());
    if (text.find(L'\0') != std::wstring::npos)
        return false;
    if (material < 0 || material > 2 || blur < 0 || blur > 24 || transparency < 10 ||
        transparency > 90)
        return false;
    n.text = std::move(text);
    n.color = c;
    n.locked = locked;
    n.fontSize = std::clamp(font, 14, 30);
    n.due = due;
    n.material = material;
    n.blur = blur;
    n.transparency = transparency;
    n.x = x;
    n.y = y;
    n.w = std::clamp(w, 370, 2000);
    n.h = std::clamp(h, 340, 2000);
    return true;
}
void load(Note &n) {
    if (readFile(n.file, n))
        return;
    auto backup = n.file;
    backup += L".bak";
    if (readFile(backup, n)) {
        n.message = L"已从备份恢复";
        auto damaged = n.file;
        damaged += L".corrupt-" + std::to_wstring(GetTickCount64());
        CopyFileW(n.file.c_str(), damaged.c_str(), TRUE);
    } else {
        n.message = L"文件损坏，原文件已保留";
        auto damaged = n.file;
        damaged += L".corrupt-" + std::to_wstring(GetTickCount64());
        CopyFileW(n.file.c_str(), damaged.c_str(), TRUE);
    }
}
bool save(Note &n) {
    if (!n.ready || !n.win || n.animation)
        return true;
    n.text = readText(n.edit);
    RECT r{};
    GetWindowRect(n.win, &r);
    if (!IsIconic(n.win)) {
        n.x = r.left;
        n.y = r.top;
        n.w = r.right - r.left;
        n.h = r.bottom - r.top;
    }
    std::vector<char> data;
    auto append = [&](const auto &value) {
        const char *p = (const char *)&value;
        data.insert(data.end(), p, p + sizeof(value));
    };
    append(MagicV3);
    for (int value : {n.color, 0, n.x, n.y, n.w, n.h})
        append(value);
    append(n.due);
    append((int)n.locked);
    append(n.fontSize);
    append(n.material);
    append(n.blur);
    append(n.transparency);
    const char *p = (const char *)n.text.data();
    data.insert(data.end(), p, p + n.text.size() * 2);
    auto tmp = n.file;
    tmp += L".tmp";
    bool ok = writeTemporary(tmp, data.data(), (DWORD)data.size());
    auto backup = n.file;
    backup += L".bak";
    std::error_code existsError;
    bool existing = fs::exists(n.file, existsError);
    if (existsError)
        ok = false;
    if (ok && existing) {
        Note previous;
        if (readFile(n.file, previous))
            ok = CopyFileW(n.file.c_str(), backup.c_str(), FALSE) != FALSE;
    }
    if (ok)
        ok = MoveFileExW(tmp.c_str(), n.file.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    n.pendingSave = !ok;
    if (ok)
        KillTimer(n.win, 2);
    n.message = ok ? L"已保存" : L"保存失败，请检查磁盘或权限";
    refresh(n);
    return ok;
}
void scheduleSave(Note &n) {
    if (n.ready) {
        n.pendingSave = true;
        n.message = L"正在编辑…";
        refresh(n);
        SetTimer(n.win, 2, 500, nullptr);
    }
}
#include "session.h"
void fonts(Note &n) {
    HFONT oldFont = n.font, oldSmallFont = n.smallFont;
    n.font = CreateFontW(-px(n, n.fontSize), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                         CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    n.smallFont = CreateFontW(-px(n, 13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
    for (int id : {New, Pin, Tear, PreviousCard, NextCard, Edit, TaskGutter, NoteSettings,
                   NoteStyle, NoteExit, PageCounter})
        SendMessageW(GetDlgItem(n.win, id), WM_SETFONT, (WPARAM)(id == Edit ? n.font : n.smallFont),
                     TRUE);
    if (n.tooltip)
        SendMessageW(n.tooltip, WM_SETFONT, (WPARAM)n.smallFont, FALSE);
    if (oldFont)
        DeleteObject(oldFont);
    if (oldSmallFont)
        DeleteObject(oldSmallFont);
}
void setControlText(HWND control, const std::wstring &text) {
    if (control && readText(control) != text)
        SetWindowTextW(control, text.c_str());
}
void updatePageControls(Note &n) {
    auto [current, total] = pageNumbers();
    testProperty(n.win, L"CurrentPage", (HANDLE)(intptr_t)current);
    testProperty(n.win, L"TotalPages", (HANDLE)(intptr_t)total);
    setControlText(GetDlgItem(n.win, PageCounter),
                   std::to_wstring(current) + L" / " + std::to_wstring(total));
    int live = 0;
    for (auto &page : notes)
        if (page->win && page->ready && !page->animation && page->counted)
            ++live;
    auto enabled = [&](int id, bool value) {
        HWND child = GetDlgItem(n.win, id);
        if ((IsWindowEnabled(child) != FALSE) != value)
            EnableWindow(child, value);
    };
    enabled(Tear, n.ready && n.counted && !n.animation);
    enabled(PreviousCard, live > 1);
    enabled(NextCard, live > 1);
}
void refresh(Note &n) {
    if (!n.win)
        return;
    if (!n.brush)
        n.brush = CreateSolidBrush(colors[n.color]);
    setControlText(GetDlgItem(n.win, Pin), preferences.fixed ? L"已固定" : L"固定");
    testProperty(n.win, L"PadFixed", (HANDLE)(intptr_t)preferences.fixed);
    bool readOnly = (GetWindowLongPtrW(n.edit, GWL_STYLE) & ES_READONLY) != 0;
    if (readOnly != n.locked)
        SendMessageW(n.edit, EM_SETREADONLY, n.locked, 0);
    if (testMode) {
        std::wstring state = n.message;
        if (n.due) {
            time_t t = n.due;
            tm local{};
            localtime_s(&local, &t);
            wchar_t b[64];
            wcsftime(b, 64, L" · %m/%d %H:%M 提醒", &local);
            state += b;
        }
        if (n.locked)
            state += L" · 只读";
        if (n.material)
            state += n.glassReady ? L" · 实时玻璃" : L" · 玻璃待恢复，暂用纸张";
        setControlText(n.status, state);
        testProperty(n.win, L"NoteMaterial", (HANDLE)(intptr_t)(n.material + 1));
    }
}
void layout(Note &n) {
    testCount(n.win, L"LayoutPasses");
    RECT r{};
    GetClientRect(n.win, &r);
    int w = r.right, h = r.bottom;
    HDWP batch = BeginDeferWindowPos(11);
    std::vector<std::pair<HWND, RECT>> changes;
    auto move = [&](int id, int x, int y, int width, int height) {
        HWND child = GetDlgItem(n.win, id);
        RECT old{};
        GetWindowRect(child, &old);
        MapWindowPoints(nullptr, n.win, (POINT *)&old, 2);
        RECT target{px(n, x), px(n, y), px(n, x + width), px(n, y + height)};
        if (!EqualRect(&old, &target)) {
            changes.push_back({child, target});
            if (batch)
                batch =
                    DeferWindowPos(batch, child, nullptr, target.left, target.top,
                                   target.right - target.left, target.bottom - target.top,
                                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
        }
    };
    int width = MulDiv(w, 96, n.dpi) - 18, height = MulDiv(h, 96, n.dpi) - 18;
    move(NoteSettings, 12, 8, 48, 28);
    move(NoteStyle, 66, 8, 48, 28);
    move(Pin, width - 114, 8, 48, 28);
    move(NoteExit, width - 60, 8, 48, 28);
    bool empty = !n.ready && !n.counted && !n.animation;
    move(PageCounter, empty ? width / 2 - 46 : width / 2 - 28, 8, empty ? 48 : 56, 28);
    move(New, empty ? width / 2 + 2 : width - 266, empty ? 8 : height - 40, 54, 28);
    HWND add = GetDlgItem(n.win, New);
    if (((GetWindowLongPtrW(add, GWL_STYLE) & WS_VISIBLE) != 0) != !n.animation)
        ShowWindow(add, n.animation ? SW_HIDE : SW_SHOW);
    int contentWidth = MulDiv(w, 96, n.dpi) - 36, contentHeight = std::max(50, height - 108);
    move(Edit, 18, 58, contentWidth - 34, contentHeight);
    move(TaskGutter, 18 + contentWidth - 34, 58, 34, contentHeight);
    invalidateTasks(n);
    move(Tear, width - 206, height - 40, 54, 28);
    move(PreviousCard, width - 146, height - 40, 64, 28);
    move(NextCard, width - 76, height - 40, 64, 28);
    bool placed = batch && EndDeferWindowPos(batch);
    if (!placed)
        for (auto &[child, target] : changes)
            SetWindowPos(child, nullptr, target.left, target.top, target.right - target.left,
                         target.bottom - target.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    updatePageControls(n);
    RedrawWindow(n.win, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void paperBrush(Note &n) {
    if (n.brush)
        DeleteObject(n.brush);
    if (n.surface)
        DeleteObject(n.surface);
    n.surface = nullptr;
    n.brush = CreateSolidBrush(colors[n.color]);
    n.glassReady = false;
    n.glassRevision = 0;
    clearTestProperty(n.win, L"GlassReady");
}
void resizeGlassCache(Note &n, const RECT &rect) {
    if (!n.surface || !n.glassReady)
        return;
    BITMAP oldSize{};
    GetObjectW(n.surface, sizeof(oldSize), &oldSize);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width == oldSize.bmWidth && height == oldSize.bmHeight)
        return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC source = CreateCompatibleDC(nullptr), target = CreateCompatibleDC(nullptr);
    if (!bitmap || !source || !target) {
        if (bitmap)
            DeleteObject(bitmap);
        if (source)
            DeleteDC(source);
        if (target)
            DeleteDC(target);
        return;
    }
    auto oldSource = SelectObject(source, n.surface), oldTarget = SelectObject(target, bitmap);
    SetStretchBltMode(target, HALFTONE);
    SetBrushOrgEx(target, 0, 0, nullptr);
    bool copied = StretchBlt(target, 0, 0, width, height, source, 0, 0, oldSize.bmWidth,
                             oldSize.bmHeight, SRCCOPY) != FALSE;
    GdiFlush();
    SelectObject(source, oldSource);
    SelectObject(target, oldTarget);
    DeleteDC(source);
    DeleteDC(target);
    HBRUSH brush = copied ? CreatePatternBrush(bitmap) : nullptr;
    if (!brush) {
        DeleteObject(bitmap);
        return;
    }
    DeleteObject(n.brush);
    DeleteObject(n.surface);
    n.brush = brush;
    n.surface = bitmap;
    n.glassRect = rect;
    n.glassRevision = 0;
    testCount(n.win, L"ResizeGlassCacheFrames");
}
void updateGlass(Note &n, bool repaint, const RECT *bounds) {
    if (!n.material || !IsWindowVisible(n.win) || n.animation ||
        (repaint && (n.resizing || n.moving)))
        return;
    RECT r{};
    GetClientRect(n.win, &r);
    POINT pt{};
    ClientToScreen(n.win, &pt);
    OffsetRect(&r, pt.x, pt.y);
    glass::Frame frame;
    if (bounds)
        r = *bounds;
    if (!n.excluded || !glass::backdrop(r, frame)) {
        testProperty(n.win, L"GlassError", (HANDLE)(intptr_t)glass::error());
        if (bounds)
            resizeGlassCache(n, r);
        return;
    }
    if (n.glassReady && n.glassRevision == frame.revision && EqualRect(&r, &n.glassRect))
        return;
    auto pixels = glass::material(frame, n.material == 1 ? n.blur : 0, n.transparency,
                                  colors[n.color], n.material == 2);
    // Keep a clear reading surface without fading the text together with the glass.
    RECT editor{};
    if (bounds) {
        int w = MulDiv(frame.width, 96, n.dpi), h = MulDiv(frame.height, 96, n.dpi);
        editor = {px(n, 18), px(n, 58), px(n, w - 52), px(n, h - 68)};
    } else {
        GetWindowRect(n.edit, &editor);
        MapWindowPoints(nullptr, n.win, (POINT *)&editor, 2);
    }
    int veil = n.material == 2 ? 35 : 12;
    for (int y = std::max(0L, editor.top); y < std::min(frame.height, (int)editor.bottom); ++y)
        for (int x = std::max(0L, editor.left); x < std::min(frame.width, (int)editor.right); ++x) {
            auto &p = pixels[(size_t)y * frame.width + x];
            uint32_t out = 0xff000000u;
            for (int c = 0; c < 3; ++c)
                out |= ((((p >> (8 * c)) & 255) * (100 - veil) + 255 * veil) / 100) << (8 * c);
            p = out;
        }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = frame.width;
    bi.bmiHeader.biHeight = -frame.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap)
        return;
    std::memcpy(bits, pixels.data(), pixels.size() * 4);
    HBRUSH brush = CreatePatternBrush(bitmap);
    if (!brush) {
        DeleteObject(bitmap);
        return;
    }
    if (n.brush)
        DeleteObject(n.brush);
    if (n.surface)
        DeleteObject(n.surface);
    n.surface = bitmap;
    n.brush = brush;
    n.glassRevision = frame.revision;
    n.glassRect = r;
    bool first = !n.glassReady;
    n.glassReady = true;
    if (bounds)
        testCount(n.win, L"ResizeGlassCaptures");
    testProperty(n.win, L"GlassReady", (HANDLE)1);
    clearTestProperty(n.win, L"GlassError");
    if (first)
        refresh(n);
    if (repaint)
        RedrawWindow(n.win, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void alignBrush(HDC dc, HWND child, Note &n) {
    POINT pt{};
    MapWindowPoints(child, n.win, &pt, 1);
    SetBrushOrgEx(dc, -pt.x, -pt.y, nullptr);
}
#include "tasks.h"
void startTear(Note &n) {
    if (n.animation || !n.ready || !n.counted || !save(n))
        return;
    auto animation = tear::capture(n.win, n.dragX < 0 ? -140 : 140);
    if (!animation) {
        n.message = L"撕下动画暂不可用，便签已保留";
        refresh(n);
        return;
    }
    HWND falling =
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                        L"STATIC", L"轻笺 · 内容纸张", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                        GetModuleHandleW(nullptr), nullptr);
    if (!falling) {
        n.message = L"动画窗口创建失败，便签已保留";
        refresh(n);
        return;
    }
    auto recycle = folder / L"Recycled";
    std::error_code ec;
    fs::create_directories(recycle, ec);
    auto target = recycle / n.file.filename();
    if (!ec)
        fs::rename(n.file, target, ec);
    if (ec) {
        DestroyWindow(falling);
        n.message = L"撕下失败，便签已保留";
        refresh(n);
        return;
    }
    auto backup = n.file;
    backup += L".bak";
    auto backupTarget = target;
    backupTarget += L".bak";
    if (fs::exists(backup, ec))
        fs::rename(backup, backupTarget, ec);
    n.tearWindow = falling;
    n.file = target;
    n.animation = std::move(animation);
    n.ready = false;
    n.gripping = false;
    EnumChildWindows(
        n.win,
        [](HWND child, LPARAM parent) -> BOOL {
            RECT r{};
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, (HWND)parent, (POINT *)&r, 2);
            if (r.top >= MulDiv(46, GetDpiForWindow((HWND)parent), 96))
                ShowWindow(child, SW_HIDE);
            return TRUE;
        },
        (LPARAM)n.win);
    RECT r{};
    GetClientRect(n.win, &r);
    book::roundShape(n.win, n.dpi, true);
    anchorAll();
    updatePageControls(n);
    SetWindowPos(n.win, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(falling, preferences.topmost ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(n.win, 3, 16, nullptr);
    tear::step(falling, *n.animation);
}
void restoreRecent() {
    auto recycle = folder / L"Recycled";
    std::error_code ec;
    if (!fs::exists(recycle, ec))
        return;
    fs::path latest;
    fs::file_time_type time = fs::file_time_type::min();
    for (auto &entry : fs::directory_iterator(recycle, ec))
        if (entry.path().extension() == L".note") {
            auto t = entry.last_write_time(ec);
            if (!ec && t > time) {
                time = t;
                latest = entry.path();
            }
        }
    if (latest.empty())
        return;
    auto target = folder / latest.filename();
    fs::rename(latest, target, ec);
    if (ec) {
        MessageBoxW(settingsWindow, L"恢复失败，回收区中的文件仍保留。", L"轻笺", MB_OK);
        return;
    }
    auto backup = latest;
    backup += L".bak";
    auto targetBackup = target;
    targetBackup += L".bak";
    if (fs::exists(backup, ec))
        MoveFileExW(backup.c_str(), targetBackup.c_str(), MOVEFILE_REPLACE_EXISTING);
    for (auto &page : notes)
        if (!page->win && page->file == latest)
            page->counted = false;
    createNote(target);
    showAll();
}
LRESULT CALLBACK tearGripProc(HWND button, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR,
                              DWORD_PTR data) {
    auto &n = *(Note *)data;
    if (msg == WM_LBUTTONDOWN) {
        n.gripping = true;
        n.pull = 0;
        n.dragX = 0;
        n.tearGrab = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(button, &n.tearGrab);
        SetCapture(button);
        SendMessageW(button, BM_SETSTATE, TRUE, 0);
        return 0;
    }
    if (msg == WM_MOUSEMOVE && n.gripping) {
        POINT now{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ClientToScreen(button, &now);
        n.dragX = now.x - n.tearGrab.x;
        n.pull = std::clamp(now.y - n.tearGrab.y, 0L, (LONG)px(n, 100));
        InvalidateRect(n.win, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_LBUTTONUP && n.gripping) {
        int pull = n.pull, drag = n.dragX;
        ReleaseCapture();
        SendMessageW(button, BM_SETSTATE, FALSE, 0);
        n.gripping = false;
        n.dragX = drag;
        if (pull >= px(n, 60) || (pull < px(n, 4) && std::abs(drag) < px(n, 4)))
            startTear(n);
        else {
            n.pull = 0;
            InvalidateRect(n.win, nullptr, TRUE);
        }
        return 0;
    }
    if (msg == WM_CAPTURECHANGED) {
        n.gripping = false;
        n.pull = 0;
        InvalidateRect(n.win, nullptr, TRUE);
    }
    return DefSubclassProc(button, msg, wp, lp);
}
#include "geometry.h"
#include "panels.h"
#include "shortcuts.h"
#include "testhooks.h"
LRESULT CALLBACK editProc(HWND edit, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) {
    auto &n = *(Note *)data;
    if (msg == WM_KEYDOWN && noteShortcut(n, wp))
        return 0;
    if (msg == WM_MOUSEWHEEL || msg == WM_VSCROLL || msg == WM_HSCROLL || msg == EM_LINESCROLL ||
        msg == EM_SCROLL || msg == EM_SCROLLCARET)
        return 0;
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_PAINT) {
        testCount(n.win, L"EditorPaints");
        PAINTSTRUCT ps;
        HDC target = BeginPaint(edit, &ps);
        RECT r{};
        GetClientRect(edit, &r);
        PaintBuffer buffer(target, r);
        alignBrush(buffer.dc(), edit, n);
        FillRect(buffer.dc(), &r, n.brush);
        DefSubclassProc(edit, WM_PRINTCLIENT, (WPARAM)buffer.dc(), PRF_CLIENT);
        paintTaskLines(n, buffer.dc());
        buffer.present();
        EndPaint(edit, &ps);
        return 0;
    }
    LRESULT result = DefSubclassProc(edit, msg, wp, lp);
    int scrolled = (int)DefSubclassProc(edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    if (scrolled > 0)
        DefSubclassProc(edit, EM_LINESCROLL, 0, -scrolled);
    if (msg == WM_SETTEXT || msg == WM_SIZE || msg == WM_SETFONT)
        ShowScrollBar(edit, SB_BOTH, FALSE);
    if (msg == WM_PRINTCLIENT || msg == WM_PRINT)
        paintTaskLines(n, (HDC)wp);
    if (msg == WM_SETTEXT || msg == EM_REPLACESEL || msg == WM_CHAR || msg == WM_CUT ||
        msg == WM_PASTE || msg == WM_CLEAR || msg == WM_UNDO || msg == EM_UNDO)
        invalidateTasks(n, true);
    else if (msg == WM_SIZE || msg == WM_SETFONT || msg == EM_SETREADONLY || msg == WM_KEYDOWN ||
             msg == WM_MOUSEWHEEL || msg == WM_VSCROLL || msg == WM_HSCROLL ||
             msg == EM_LINESCROLL || msg == WM_LBUTTONUP)
        invalidateTasks(n);
    return result;
}
void notify(Note &n) {
    stackTop = n.win;
    ShowWindow(n.win, SW_SHOWNORMAL);
    anchorAll();
    n.message = n.message.starts_with(L"保存失败") ? L"提醒已到时 · 保存失败" : L"提醒已到时";
    refresh(n);
    FlashWindow(n.win, TRUE);
    if (preferences.sound)
        MessageBeep(MB_ICONINFORMATION);
    if (trayAvailable) {
        tray.uFlags = NIF_INFO;
        wcscpy_s(tray.szInfoTitle, L"轻笺 · 到时提醒");
        auto s = n.text.empty() ? std::wstring(L"便签提醒时间到了。") : n.text;
        wcsncpy_s(tray.szInfo, s.c_str(), _TRUNCATE);
        tray.dwInfoFlags = NIIF_INFO | (preferences.sound ? 0 : NIIF_NOSOUND);
        Shell_NotifyIconW(NIM_MODIFY, &tray);
    }
}
void installHints(Note &note) {
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, controller, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    if (!tip)
        return;
    // Tooltip z-order changes must not demote the note that it describes.
    note.tooltip = tip;
    SendMessageW(tip, WM_SETFONT, (WPARAM)note.smallFont, FALSE);
    SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, 360);
    struct Hint {
        int id;
        const wchar_t *text;
    };
    Hint hints[] = {{NoteSettings, L"桌面位置、当前页提醒与常规选项"},
                    {NoteStyle, L"调整纸张材质、颜色和正文字号"},
                    {New, L"新增纸张，保持当前页 · Ctrl+N"},
                    {TaskGutter, L"勾选右侧方框，完成并移除对应事件"},
                    {Tear, L"点一下撕下本页，或按住向下拖动"},
                    {PreviousCard, L"切换上一张可用纸张 · Ctrl+Shift+Tab"},
                    {NextCard, L"切换下一张可用纸张 · Ctrl+Tab"},
                    {Pin, L"固定或解锁整本的位置 · Ctrl+P"},
                    {NoteExit, L"保存全部便签并退出"}};
    for (auto hint : hints) {
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = note.win;
        info.uId = (UINT_PTR)GetDlgItem(note.win, hint.id);
        info.lpszText = (wchar_t *)hint.text;
        SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    }
}
LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto n = (Note *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        n = (Note *)((CREATESTRUCT *)lp)->lpCreateParams;
        n->win = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)n);
    }
    if (!n)
        return DefWindowProcW(hwnd, msg, wp, lp);
    if (n->animation && msg == WM_COMMAND)
        return 0;
    switch (msg) {
    case WM_CREATE: {
        n->dpi = GetDpiForWindow(hwnd);
        n->brush = CreateSolidBrush(colors[n->color]);
        auto control = [&](const wchar_t *cls, const wchar_t *title, DWORD style, int id) {
            return CreateWindowExW(0, cls, title, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0,
                                   0, 0, hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr),
                                   nullptr);
        };
        control(L"BUTTON", L"添加", BS_OWNERDRAW, New);
        control(L"STATIC", L"1 / 1", SS_CENTER | SS_CENTERIMAGE, PageCounter);
        control(L"BUTTON", L"固定", BS_OWNERDRAW, Pin);
        control(L"BUTTON", L"撕下", BS_OWNERDRAW, Tear);
        SetWindowSubclass(GetDlgItem(hwnd, Tear), tearGripProc, 1, (DWORD_PTR)n);
        control(L"BUTTON", L"上一页", BS_OWNERDRAW, PreviousCard);
        control(L"BUTTON", L"下一页", BS_OWNERDRAW, NextCard);
        control(L"BUTTON", L"设置", BS_OWNERDRAW, NoteSettings);
        control(L"BUTTON", L"风格", BS_OWNERDRAW, NoteStyle);
        control(L"BUTTON", L"退出", BS_OWNERDRAW, NoteExit);
        // Keep multiline storage capacity; editProc pins the viewport and blocks every scroll
        // input.
        n->edit =
            control(L"EDIT", n->text.c_str(), ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, Edit);
        ShowScrollBar(n->edit, SB_BOTH, FALSE);
        SendMessageW(n->edit, EM_SETLIMITTEXT, 500000, 0);
        SetWindowSubclass(n->edit, editProc, 1, (DWORD_PTR)n);
        SendMessageW(n->edit, EM_SETCUEBANNER, FALSE, (LPARAM)L"每行写一件待办，勾选右侧方框完成");
        n->gutter =
            CreateWindowExW(0, L"LightNotesTasks", L"勾选完成事件", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                            0, hwnd, (HMENU)(INT_PTR)TaskGutter, GetModuleHandleW(nullptr), n);
        if (testMode)
            n->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 0, 0, hwnd,
                                        (HMENU)(INT_PTR)Status, GetModuleHandleW(nullptr), nullptr);
        fonts(*n);
        BOOL disableTransitions = TRUE;
        DwmSetWindowAttribute(hwnd, 3, &disableTransitions, sizeof(disableTransitions));
        int rounded = 1;
        DwmSetWindowAttribute(hwnd, 33, &rounded, sizeof(rounded));
        DWMNCRENDERINGPOLICY framePolicy = DWMNCRP_DISABLED;
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &framePolicy, sizeof(framePolicy));
        DWORD borderColor = 0xfffffffeu;
        DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));
        n->ready = true;
        layout(*n);
        refresh(*n);
        installHints(*n);
        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }
    case WM_SIZE:
        layout(*n);
        if (!n->animation) {
            book::roundShape(hwnd, n->dpi, !n->ready && !n->counted, false);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        rememberGeometry(*n, false);
        return 0;
    case WM_MOUSEACTIVATE:
        if (!n->animation && stackTop != hwnd) {
            stackTop = hwnd;
            anchorAll();
        }
        return MA_ACTIVATE;
    case WM_DPICHANGED: {
        n->dpi = HIWORD(wp);
        auto r = (RECT *)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        fonts(*n);
        layout(*n);
        return 0;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lp)->ptMinTrackSize = {px(*n, 370), px(*n, 340)};
        return 0;
    case WM_SETCURSOR: {
        int hit = LOWORD(lp);
        LPCWSTR cursor = nullptr;
        if (hit == HTLEFT || hit == HTRIGHT)
            cursor = IDC_SIZEWE;
        else if (hit == HTTOP || hit == HTBOTTOM)
            cursor = IDC_SIZENS;
        else if (hit == HTTOPLEFT || hit == HTBOTTOMRIGHT)
            cursor = IDC_SIZENWSE;
        else if (hit == HTTOPRIGHT || hit == HTBOTTOMLEFT)
            cursor = IDC_SIZENESW;
        if (cursor) {
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        break;
    }
    case WM_NCLBUTTONDOWN:
        if (preferences.fixed && wp != HTRIGHT && wp != HTBOTTOM && wp != HTBOTTOMRIGHT)
            return 0;
        if (wp == HTCAPTION) {
            n->moving = true;
            n->resizeGrab = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            GetWindowRect(hwnd, &n->resizeRect);
            SetCapture(hwnd);
            return 0;
        }
        if (wp >= HTLEFT && wp <= HTBOTTOMRIGHT) {
            n->resizing = true;
            n->resizeHit = (int)wp;
            n->resizeGrab = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            GetWindowRect(hwnd, &n->resizeRect);
            SetCapture(hwnd);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (preferences.fixed && n->moving)
            return 0;
        if (n->moving) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &point);
            RECT r = n->resizeRect;
            OffsetRect(&r, point.x - n->resizeGrab.x, point.y - n->resizeGrab.y);
            RECT old{};
            GetWindowRect(hwnd, &old);
            if (!EqualRect(&old, &r)) {
                updateGlass(*n, false, &r);
                // Preserve the moving sheet, then restore only the background it vacated.
                SetWindowPos(hwnd, nullptr, r.left, r.top, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                repaintVacated(hwnd, old);
                DwmFlush();
            }
            return 0;
        }
        if (n->resizing) {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ClientToScreen(hwnd, &point);
            int dx = point.x - n->resizeGrab.x, dy = point.y - n->resizeGrab.y;
            RECT r = n->resizeRect;
            int hit = n->resizeHit;
            bool left = hit == HTLEFT || hit == HTTOPLEFT || hit == HTBOTTOMLEFT,
                 right = hit == HTRIGHT || hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT,
                 top = hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT,
                 bottom = hit == HTBOTTOM || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT;
            if (left)
                r.left = std::min(r.left + dx, r.right - px(*n, 370));
            if (right)
                r.right = std::max(r.right + dx, r.left + px(*n, 370));
            if (top)
                r.top = std::min(r.top + dy, r.bottom - px(*n, 340));
            if (bottom)
                r.bottom = std::max(r.bottom + dy, r.top + px(*n, 340));
            RECT old{};
            GetWindowRect(hwnd, &old);
            if (!EqualRect(&old, &r)) {
                updateGlass(*n, false, &r);
                SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
                RedrawWindow(hwnd, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                repaintVacated(hwnd, old);
                DwmFlush();
                testCount(hwnd, L"ResizeFrames");
            }
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (n->resizing || n->moving) {
            bool moved = n->moving;
            n->resizing = n->moving = false;
            ReleaseCapture();
            rememberGeometry(*n, moved);
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (n->resizing || n->moving) {
            bool moved = n->moving;
            n->resizing = n->moving = false;
            rememberGeometry(*n, moved);
        }
        break;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return TRUE;
    case WM_NCCALCSIZE:
        if (wp) {
            auto p = (NCCALCSIZE_PARAMS *)lp;
            auto &now = p->rgrc[0];
            auto &old = p->rgrc[1];
            return now.right - now.left != old.right - old.left ||
                           now.bottom - now.top != old.bottom - old.top
                       ? WVR_REDRAW
                       : 0;
        }
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit != HTCLIENT && hit != HTCAPTION)
            return hit;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        RECT r;
        GetClientRect(hwnd, &r);
        int b = px(*n, 7);
        bool l = pt.x < b, right = pt.x >= r.right - b, top = pt.y < b,
             bottom = pt.y >= r.bottom - b;
        if (preferences.fixed) {
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (right && !top)
                return HTRIGHT;
            if (bottom && !l)
                return HTBOTTOM;
            return HTCLIENT;
        }
        if (top && l)
            return HTTOPLEFT;
        if (top && right)
            return HTTOPRIGHT;
        if (bottom && l)
            return HTBOTTOMLEFT;
        if (bottom && right)
            return HTBOTTOMRIGHT;
        if (l)
            return HTLEFT;
        if (right)
            return HTRIGHT;
        if (top)
            return HTTOP;
        if (bottom)
            return HTBOTTOM;
        if (pt.y < px(*n, 46))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
        alignBrush((HDC)wp, (HWND)lp, *n);
        SetTextColor((HDC)wp, RGB(35, 42, 52));
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)n->brush;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC targetDC = BeginPaint(hwnd, &ps);
        RECT r;
        GetClientRect(hwnd, &r);
        PaintBuffer buffer(targetDC, r);
        HDC dc = buffer.dc();
        FillRect(dc, &r, n->brush);
        HPEN pen = CreatePen(PS_DASH, 1, RGB(60, 65, 72));
        auto old = SelectObject(dc, pen);
        SetBkMode(dc, TRANSPARENT);
        int seam = px(*n, 46);
        MoveToEx(dc, 1, seam, nullptr);
        LineTo(dc, r.right - 1, seam);
        if (n->gripping && n->pull > 0) {
            SelectObject(dc, GetStockObject(BLACK_PEN));
            int reach = MulDiv(r.right, n->pull, px(*n, 100));
            MoveToEx(dc, 0, seam, nullptr);
            for (int x = 0; x < reach; x += px(*n, 6))
                LineTo(dc, x, seam + ((x / px(*n, 6)) % 2 ? px(*n, 3) : 0));
        }
        SelectObject(dc, old);
        DeleteObject(pen);
        buffer.present();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto d = (DRAWITEMSTRUCT *)lp;
        alignBrush(d->hDC, d->hwndItem, *n);
        FillRect(d->hDC, &d->rcItem, n->brush);
        SelectObject(d->hDC, n->smallFont);
        SetBkMode(d->hDC, TRANSPARENT);
        COLORREF ink = RGB(35, 42, 52);
        if (d->itemState & ODS_DISABLED)
            ink = RGB(155, 152, 143);
        else if (d->itemState & ODS_SELECTED)
            ink = RGB(104, 111, 119);
        SetTextColor(d->hDC, ink);
        wchar_t title[40];
        GetWindowTextW(d->hwndItem, title, 40);
        RECT r = d->rcItem;
        DrawTextW(d->hDC, title, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_APP + 2:
        if (testMode) {
            capturePauseUntil = wp ? GetTickCount64() + 2000 : 0;
            for (auto &note : notes)
                if (note->win) {
                    SetWindowDisplayAffinity(note->win, wp ? WDA_NONE : 0x11);
                    note->excluded = !wp;
                }
            return 0;
        }
        break;
    case WM_COMMAND:
        if (testStyleCommand(*n, LOWORD(wp)))
            return 0;
        switch (LOWORD(wp)) {
        case NoteSettings:
            openSettings();
            break;
        case NoteStyle:
            openStyle();
            break;
        case NoteExit:
            SendMessageW(controller, WM_COMMAND, Quit, 0);
            break;
        case New:
            createNote();
            break;
        case Pin: {
            auto previous = preferences;
            preferences.fixed = !preferences.fixed;
            if (preferences.fixed) {
                RECT r{};
                GetWindowRect(hwnd, &r);
                preferences.width = MulDiv(r.right - r.left, 96, n->dpi);
                preferences.height = MulDiv(r.bottom - r.top, 96, n->dpi);
                preferences.left = r.left;
                preferences.top = r.top;
                preferences.corner = 4;
            }
            if (!savePreferences()) {
                preferences = previous;
                n->message = L"整本固定状态保存失败";
                refresh(*n);
                break;
            }
            for (auto &page : notes)
                if (page->win && !page->animation) {
                    page->resizing = page->moving = false;
                    refresh(*page);
                }
            if (GetCapture())
                ReleaseCapture();
            anchorAll();
            for (auto &page : notes)
                if (page->win && !page->animation)
                    save(*page);
            syncPanel(settingsWindow);
            syncGeometry(geometryWindow);
            break;
        }
        case PreviousCard:
            switchStack(-1);
            break;
        case NextCard:
            switchStack();
            break;
        case Tear:
            startTear(*n);
            break;
        case Lock:
            n->locked = !n->locked;
            save(*n);
            break;
        case Edit:
            if (HIWORD(wp) == EN_CHANGE) {
                invalidateTasks(*n, true);
                scheduleSave(*n);
            } else if (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_HSCROLL)
                invalidateTasks(*n);
            else if (HIWORD(wp) == EN_KILLFOCUS && n->ready && n->pendingSave)
                save(*n);
            break;
        }
        return 0;
    case WM_TIMER:
        if (wp == 3 && n->animation) {
            if (!tear::step(n->tearWindow, *n->animation)) {
                KillTimer(hwnd, 3);
                bool empty = std::none_of(notes.begin(), notes.end(),
                                          [](auto &page) { return page->win && !page->animation; });
                if (empty) {
                    DestroyWindow(n->tearWindow);
                    n->tearWindow = nullptr;
                    n->animation.reset();
                    for (auto &page : notes)
                        page->counted = false;
                    layout(*n);
                    SetWindowTextW(GetDlgItem(hwnd, PageCounter), L"0 / 0");
                    stackTop = nullptr;
                    clearTestProperty(controller, L"StackTop");
                    InvalidateRect(hwnd, nullptr, TRUE);
                } else {
                    DestroyWindow(hwnd);
                    anchorAll();
                }
            }
            return 0;
        }
        if (n->animation)
            return 0;
        if (wp == 2) {
            KillTimer(hwnd, 2);
            save(*n);
        } else if (!initialSettings && n->due && std::time(nullptr) >= n->due) {
            n->due = 0;
            save(*n);
            notify(*n);
        }
        return 0;
    case WM_CLOSE:
        if (save(*n)) {
            if (trayAvailable) {
                for (auto &note : notes)
                    if (note->win && !note->animation && save(*note))
                        ShowWindow(note->win, SW_HIDE);
            } else
                DestroyWindow(hwnd);
        }
        return 0;
    case WM_QUERYENDSESSION:
        return save(*n) && saveSession() ? TRUE : FALSE;
    case WM_DESTROY:
        if (n->tooltip) {
            DestroyWindow(n->tooltip);
            n->tooltip = nullptr;
        }
        if (n->tearWindow) {
            DestroyWindow(n->tearWindow);
            n->tearWindow = nullptr;
        }
        n->ready = false;
        n->win = nullptr;
        n->animation.reset();
        DeleteObject(n->font);
        DeleteObject(n->smallFont);
        DeleteObject(n->brush);
        if (n->surface)
            DeleteObject(n->surface);
        if (!quitting)
            anchorAll();
        if (!trayAvailable && !quitting &&
            std::none_of(notes.begin(), notes.end(), [](auto &p) { return p->win != nullptr; }))
            PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void addTray() {
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = controller;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(tray.szTip, L"轻笺 · 双击打开设置 / 右键管理");
    trayAvailable = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
}
LRESULT CALLBACK controllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static UINT taskbar = RegisterWindowMessageW(L"TaskbarCreated");
    if (msg == taskbar) {
        addTray();
        if (!trayAvailable)
            showAll();
        return 0;
    }
    if (msg == WM_HOTKEY) {
        createNote();
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE) {
        glass::release();
        for (auto &n : notes)
            n->glassRevision = 0;
        anchorAll();
        return 0;
    }
    if (msg == WM_TIMER && wp == 2) {
        KillTimer(hwnd, 2);
        if (!saveSession())
            for (auto &n : notes)
                if (n->win == stackTop) {
                    n->message = L"页面记忆保存失败，事项仍保存在本地";
                    refresh(*n);
                }
        return 0;
    }
    if (msg == WM_TIMER) {
        if (std::any_of(notes.begin(), notes.end(),
                        [](auto &n) { return n->animation != nullptr; }))
            return 0;
        if (capturePauseUntil > GetTickCount64())
            return 0;
        capturePauseUntil = 0;
        bool active = std::any_of(notes.begin(), notes.end(), [](auto &n) {
            return n->win && n->material && !n->animation && IsWindowVisible(n->win);
        });
        bool safe = true;
        for (auto &n : notes)
            if (n->win) {
                if (active != n->excluded) {
                    bool ok = SetWindowDisplayAffinity(n->win, active ? 0x11 : WDA_NONE) != FALSE;
                    if (ok)
                        n->excluded = active;
                    else
                        safe = false;
                }
            }
        if (!active) {
            glass::release();
            for (auto &n : notes)
                n->glassRevision = 0;
        } else if (safe)
            for (auto &n : notes)
                if (n->win)
                    updateGlass(*n);
        return 0;
    }
    if (msg == TrayMessage) {
        if (lp == WM_LBUTTONDBLCLK)
            openSettings();
        else if (lp == NIN_BALLOONUSERCLICK)
            showAll();
        else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, OpenSettings, L"便签设置");
            AppendMenuW(menu, MF_STRING, OpenStyle, L"外观风格");
            AppendMenuW(menu, MF_STRING, New, L"新建便签  Ctrl+Alt+N");
            AppendMenuW(menu, MF_STRING, NextStack, L"下一张便签");
            AppendMenuW(menu, MF_STRING, ShowAll, L"显示整叠");
            AppendMenuW(menu, MF_STRING, HideAll, L"隐藏整叠");
            AppendMenuW(menu, MF_STRING, Restore, L"恢复最近撕下的便签");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, Quit, L"保存并退出");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int id =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if (id)
                SendMessageW(hwnd, WM_COMMAND, id, 0);
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
        case OpenSettings:
            openSettings();
            break;
        case OpenStyle:
            openStyle();
            break;
        case Restore:
            restoreRecent();
            break;
        case NextStack:
            switchStack();
            break;
        case New:
            initialSettings = false;
            createNote();
            break;
        case ShowAll:
            showAll();
            break;
        case HideAll:
            for (auto &n : notes)
                if (n->win && save(*n) && trayAvailable)
                    ShowWindow(n->win, SW_HIDE);
            break;
        case Quit: {
            bool ok = true;
            for (auto &n : notes)
                if (n->win && !save(*n))
                    ok = false;
            if (ok)
                ok = saveSession();
            if (!ok) {
                showAll();
                MessageBoxW(nullptr, L"便签或页面记忆未能保存，请处理后再退出。", L"轻笺",
                            MB_OK | MB_ICONERROR);
                break;
            }
            quitting = true;
            for (auto &n : notes)
                if (n->win)
                    DestroyWindow(n->win);
            Shell_NotifyIconW(NIM_DELETE, &tray);
            PostQuitMessage(0);
            break;
        }
        }
        return 0;
    }
    if (msg == WM_QUERYENDSESSION) {
        bool ok = true;
        for (auto &n : notes)
            if (n->win && !save(*n))
                ok = false;
        return ok && saveSession();
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void createNote(fs::path file) {
    for (auto &page : notes)
        if (page->win && !page->counted && !page->animation)
            DestroyWindow(page->win);
    auto ptr = std::make_unique<Note>();
    auto n = ptr.get();
    bool fresh = file.empty();
    GUID id{};
    CoCreateGuid(&id);
    wchar_t guid[40];
    StringFromGUID2(id, guid, 40);
    n->file = fresh ? folder / (std::wstring(guid) + L".note") : file;
    if (fresh && stackTop) {
        RECT front{};
        GetWindowRect(stackTop, &front);
        n->w = front.right - front.left;
        n->h = front.bottom - front.top;
    }
    if (!fresh)
        load(*n);
    else {
        n->material = preferences.material;
        n->blur = preferences.blur;
        n->transparency = preferences.transparency;
        n->color = preferences.color;
        n->fontSize = preferences.fontSize;
        int offset = (int)(notes.size() % 8) * 24;
        n->x = 100 + offset;
        n->y = 100 + offset;
    }
    syncStackMaterial(*n);
    if (n->x != CW_USEDEFAULT) {
        RECT r{n->x, n->y, n->x + n->w, n->y + n->h};
        HMONITOR monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(monitor, &mi);
        n->w = std::min<int>(n->w, mi.rcWork.right - mi.rcWork.left);
        n->h = std::min<int>(n->h, mi.rcWork.bottom - mi.rcWork.top);
        n->x = std::clamp<int>(n->x, mi.rcWork.left, mi.rcWork.right - n->w);
        n->y = std::clamp<int>(n->y, mi.rcWork.top, mi.rcWork.bottom - n->h);
    }
    notes.push_back(std::move(ptr));
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"DesktopNotes", L"轻笺 · 桌面便签",
                                WS_POPUP | WS_CLIPCHILDREN, n->x, n->y, n->w, n->h, nullptr,
                                nullptr, GetModuleHandleW(nullptr), n);
    if (!hwnd) {
        MessageBoxW(nullptr, L"便签窗口创建失败。", L"轻笺", MB_OK | MB_ICONERROR);
        return;
    }
    int width = fresh && !stackTop ? px(*n, 410) : std::max(n->w, px(*n, 370));
    int height = fresh && !stackTop ? px(*n, 560) : std::max(n->h, px(*n, 410));
    RECT requested{n->x, n->y, n->x + width, n->y + height};
    MONITORINFO workArea{};
    workArea.cbSize = sizeof(workArea);
    GetMonitorInfoW(MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST), &workArea);
    width = std::min<int>(width, workArea.rcWork.right - workArea.rcWork.left);
    height = std::min<int>(height, workArea.rcWork.bottom - workArea.rcWork.top);
    int x = std::clamp<int>(n->x, workArea.rcWork.left, workArea.rcWork.right - width),
        y = std::clamp<int>(n->y, workArea.rcWork.top, workArea.rcWork.bottom - height);
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    if (!initialSettings) {
        if (!stackTop) {
            stackTop = hwnd;
        }
        anchorAll();
        if (stackTop == hwnd) {
            SetForegroundWindow(hwnd);
            SetFocus(n->edit);
        }
    }
    syncPadTopmost();
    if (fresh)
        save(*n);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wchar_t themeExe[32768];
    GetModuleFileNameW(nullptr, themeExe, 32768);
    auto themeFile = fs::path(themeExe).parent_path() / L"LightNotes.manifest";
    ACTCTXW context{};
    context.cbSize = sizeof(context);
    context.lpSource = themeFile.c_str();
    HANDLE theme = CreateActCtxW(&context);
    ULONG_PTR themeCookie = 0;
    if (theme != INVALID_HANDLE_VALUE)
        ActivateActCtx(theme, &themeCookie);
    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_DATE_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&ic);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t app[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, app)))
        return 1;
    folder = fs::path(app) / L"LightNotes";
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; i++) {
        if (std::wstring(argv[i]) == L"--test-mode")
            testMode = true;
        else if (std::wstring(argv[i]) == L"--data-dir" && i + 1 < argc)
            folder = fs::absolute(argv[++i]);
    }
    LocalFree(argv);
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    auto iconFile = fs::path(executable).parent_path() / L"LightNotes.ico";
    appIcon = (HICON)LoadImageW(nullptr, iconFile.c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE);
    auto mutexName =
        L"Local\\LightNotes-" + std::to_wstring(std::hash<std::wstring>{}(folder.wstring()));
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"LightNotesController", mutexName.c_str());
        if (existing) {
            DWORD pid = 0;
            GetWindowThreadProcessId(existing, &pid);
            AllowSetForegroundWindow(pid);
            SendMessageW(existing, WM_COMMAND, ShowAll, 0);
        }
        CloseHandle(mutex);
        return 0;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        MessageBoxW(nullptr, L"无法创建数据目录。", L"轻笺", MB_OK | MB_ICONERROR);
        return 1;
    }
    loadPreferences();
    WNDCLASSW wc{};
    wc.lpfnWndProc = controllerProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"LightNotesController";
    RegisterClassW(&wc);
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, mutexName.c_str(), WS_POPUP, 0,
                                 0, 0, 0, nullptr, nullptr, instance, nullptr);
    addTray();
    RegisterHotKey(controller, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'N');
    WNDCLASSW tasksClass{};
    tasksClass.lpfnWndProc = tasksProc;
    tasksClass.hInstance = instance;
    tasksClass.lpszClassName = L"LightNotesTasks";
    tasksClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&tasksClass);
    wc.lpfnWndProc = proc;
    wc.lpszClassName = L"DesktopNotes";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = appIcon ? appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassW(&wc);
    restoreSession();
    SetTimer(controller, 1, preferences.lowPower ? 260 : 120, nullptr);
    wc.lpfnWndProc = settingsProc;
    wc.lpszClassName = L"LightNotesSettings";
    RegisterClassW(&wc);
    UINT systemDpi = GetDpiForSystem();
    auto optionsPanel = [&](const wchar_t *cls, const wchar_t *title, void *data) {
        DWORD flags = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r{0, 0, MulDiv(OptionsPanelWidth, systemDpi, 96),
               MulDiv(OptionsPanelHeight, systemDpi, 96)};
        AdjustWindowRectExForDpi(&r, flags, FALSE, 0, systemDpi);
        return CreateWindowExW(0, cls, title, flags, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left,
                               r.bottom - r.top, nullptr, nullptr, instance, data);
    };
    settingsWindow = optionsPanel(wc.lpszClassName, L"轻笺 · 设置", nullptr);
    wc.lpszClassName = L"LightNotesStyle";
    RegisterClassW(&wc);
    styleWindow = optionsPanel(wc.lpszClassName, L"轻笺 · 风格", (LPVOID)1);
    wc.lpfnWndProc = geometryProc;
    wc.lpszClassName = L"LightNotesGeometry";
    RegisterClassW(&wc);
    geometryWindow = CreateWindowExW(
        0, wc.lpszClassName, L"轻笺 · 位置与大小", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(440, systemDpi, 96), MulDiv(410, systemDpi, 96),
        nullptr, nullptr, instance, nullptr);
    showAll();
    testProperty(controller, L"AppReady", (HANDLE)1);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        bool handled = false;
        if (msg.message == WM_KEYDOWN) {
            for (auto &n : notes)
                if (n->win && !n->animation && (msg.hwnd == n->win || IsChild(n->win, msg.hwnd))) {
                    handled = noteShortcut(*n, msg.wParam);
                    break;
                }
        }
        if (!handled && (msg.hwnd == settingsWindow || IsChild(settingsWindow, msg.hwnd)))
            handled = IsDialogMessageW(settingsWindow, &msg) != FALSE;
        if (!handled && (msg.hwnd == styleWindow || IsChild(styleWindow, msg.hwnd)))
            handled = IsDialogMessageW(styleWindow, &msg) != FALSE;
        if (!handled && (msg.hwnd == geometryWindow || IsChild(geometryWindow, msg.hwnd)))
            handled = IsDialogMessageW(geometryWindow, &msg) != FALSE;
        if (!handled)
            for (auto &n : notes)
                if (n->win && !n->animation && (msg.hwnd == n->win || IsChild(n->win, msg.hwnd)) &&
                    IsDialogMessageW(n->win, &msg)) {
                    handled = true;
                    break;
                }
        if (!handled) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    UnregisterHotKey(controller, 1);
    Shell_NotifyIconW(NIM_DELETE, &tray);
    DestroyWindow(settingsWindow);
    DestroyWindow(styleWindow);
    DestroyWindow(geometryWindow);
    DestroyWindow(controller);
    glass::release();
    CloseHandle(mutex);
    if (appIcon)
        DestroyIcon(appIcon);
    CoUninitialize();
    if (themeCookie)
        DeactivateActCtx(0, themeCookie);
    if (theme != INVALID_HANDLE_VALUE)
        ReleaseActCtx(theme);
    return 0;
}
