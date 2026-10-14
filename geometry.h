// Position and size editor for the whole pad. Dimensions use the same logical units as the paper
// layout.
constexpr int GeoWidth = 400, GeoHeight = 401, GeoX = 402, GeoY = 403, GeoMode = 404,
              GeoApply = 405, GeoClose = 406, GeoStatus = 407;
struct GeometryContext {
    UINT dpi{};
    HFONT font{}, title{};
    HBRUSH brush{};
};
Note *geometryTarget() {
    for (auto &n : notes)
        if (n->win == stackTop && !n->animation)
            return n.get();
    for (auto &n : notes)
        if (n->win && !n->animation)
            return n.get();
    return nullptr;
}
void syncGeometry(HWND hwnd) {
    auto n = geometryTarget();
    RECT r{};
    if (n)
        GetWindowRect(n->win, &r);
    int dpi = n ? n->dpi : GetDpiForSystem();
    auto text = [&](int id, int value) {
        SetWindowTextW(GetDlgItem(hwnd, id), std::to_wstring(value).c_str());
    };
    text(GeoWidth, n                   ? MulDiv(r.right - r.left, 96, dpi)
                   : preferences.width ? preferences.width
                                       : 410);
    text(GeoHeight, n                    ? MulDiv(r.bottom - r.top, 96, dpi)
                    : preferences.height ? preferences.height
                                         : 560);
    text(GeoX, n ? r.left : preferences.left == -100001 ? 100 : preferences.left);
    text(GeoY, n ? r.top : preferences.top == -100001 ? 100 : preferences.top);
    SendMessageW(GetDlgItem(hwnd, GeoMode), CB_SETCURSEL, preferences.corner, 0);
    EnableWindow(GetDlgItem(hwnd, GeoMode), !preferences.fixed);
    for (int id : {GeoX, GeoY})
        EnableWindow(GetDlgItem(hwnd, id), preferences.corner == 4 && !preferences.fixed);
    SetWindowTextW(GetDlgItem(hwnd, GeoStatus),
                   preferences.fixed
                       ? L"整本位置已固定，仍可调整宽度和高度。\n点击顶栏“已固定”可重新移动整本。"
                       : L"尺寸随系统缩放适配，坐标为屏幕像素。\n拖动顶栏可移动；拖动边缘可缩放。");
}
void openGeometry() {
    ShowWindow(settingsWindow, SW_HIDE);
    ShowWindow(styleWindow, SW_HIDE);
    syncGeometry(geometryWindow);
    showPanel(geometryWindow);
}
LRESULT CALLBACK geometryProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto c = (GeometryContext *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        c = new GeometryContext;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
    }
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_CREATE) {
        c->dpi = GetDpiForWindow(hwnd);
        auto scale = [&](int v) { return MulDiv(v, c->dpi, 96); };
        c->brush = CreateSolidBrush(RGB(248, 248, 246));
        c->font = CreateFontW(-scale(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        c->title = CreateFontW(-scale(23), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        auto control = [&](const wchar_t *cls, const wchar_t *title, DWORD style, int id, int x,
                           int y, int w, int h) {
            HWND child = CreateWindowExW(0, cls, title,
                                         WS_CHILD | WS_VISIBLE |
                                             (wcscmp(cls, L"STATIC") ? WS_TABSTOP : 0) | style,
                                         scale(x), scale(y), scale(w), scale(h), hwnd,
                                         (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, (WPARAM)c->font, TRUE);
            return child;
        };
        auto title = control(L"STATIC", L"位置与大小", 0, 410, 28, 20, 374, 36);
        SendMessageW(title, WM_SETFONT, (WPARAM)c->title, TRUE);
        control(L"STATIC", L"调整整叠便签，切页后保持一致。", 0, 411, 28, 65, 374, 26);
        control(L"STATIC", L"定位方式", 0, 412, 28, 108, 130, 25);
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST, GeoMode, 166, 100, 236, 180);
        for (auto text : {L"左上角", L"右上角", L"左下角", L"右下角", L"自由定位"})
            SendMessageW(GetDlgItem(hwnd, GeoMode), CB_ADDSTRING, 0, (LPARAM)text);
        control(L"STATIC", L"宽度", 0, 413, 28, 159, 60, 24);
        control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, GeoWidth, 90, 150, 102, 30);
        control(L"STATIC", L"高度", 0, 414, 218, 159, 50, 24);
        control(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, GeoHeight, 278, 150, 124, 30);
        control(L"STATIC", L"横坐标", 0, 415, 28, 212, 65, 24);
        control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, GeoX, 100, 203, 92, 30);
        control(L"STATIC", L"纵坐标", 0, 416, 218, 212, 60, 24);
        control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, GeoY, 288, 203, 114, 30);
        control(L"STATIC", L"", 0, GeoStatus, 28, 252, 374, 52);
        control(L"BUTTON", L"应用位置和大小", BS_DEFPUSHBUTTON, GeoApply, 28, 320, 224, 38);
        control(L"BUTTON", L"关闭", BS_PUSHBUTTON, GeoClose, 286, 320, 116, 38);
        syncGeometry(hwnd);
        return 0;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == GeoMode && HIWORD(wp) == CBN_SELCHANGE) {
            bool free = !preferences.fixed &&
                        SendMessageW(GetDlgItem(hwnd, GeoMode), CB_GETCURSEL, 0, 0) == 4;
            for (int field : {GeoX, GeoY})
                EnableWindow(GetDlgItem(hwnd, field), free);
            return 0;
        }
        if (id == GeoClose || id == IDCANCEL) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (id == GeoApply) {
            auto parse = [&](int field, int &value) {
                auto text = readText(GetDlgItem(hwnd, field));
                wchar_t *end{};
                long result = wcstol(text.c_str(), &end, 10);
                if (text.empty() || end != text.c_str() + text.size() || result < -100000 ||
                    result > 100000)
                    return false;
                value = (int)result;
                return true;
            };
            int width, height, x, y;
            if (!parse(GeoWidth, width) || !parse(GeoHeight, height) || !parse(GeoX, x) ||
                !parse(GeoY, y) || width < 370 || height < 340 || width > 2000 || height > 2000) {
                SetWindowTextW(GetDlgItem(hwnd, GeoStatus),
                               L"请输入有效坐标。宽度至少370，高度至少340；\n尺寸最多2000，过大时会"
                               L"限制在屏幕范围内。");
                return 0;
            }
            auto previous = preferences;
            preferences.width = width;
            preferences.height = height;
            if (!preferences.fixed) {
                preferences.left = x;
                preferences.top = y;
                preferences.corner =
                    (int)SendMessageW(GetDlgItem(hwnd, GeoMode), CB_GETCURSEL, 0, 0);
            }
            if (!savePreferences()) {
                preferences = previous;
                SetWindowTextW(GetDlgItem(hwnd, GeoStatus), L"保存失败，请检查磁盘或权限。");
                return 0;
            }
            anchorAll();
            auto target = geometryTarget();
            if (target && !rememberGeometry(*target, false)) {
                SetWindowTextW(GetDlgItem(hwnd, GeoStatus), L"便签保存失败，请检查后重试。");
                return 0;
            }
            showAll();
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        SetTextColor((HDC)wp, RGB(40, 48, 55));
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)c->brush;
    }
    if (msg == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(hwnd, &r);
        FillRect((HDC)wp, &r, c->brush);
        return 1;
    }
    if (msg == WM_CLOSE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        DeleteObject(c->font);
        DeleteObject(c->title);
        DeleteObject(c->brush);
        delete c;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
