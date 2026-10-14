// Internal settings/style panels. Included once by main.cpp after the note operations.
constexpr int OptionsPanelWidth = 416, OptionsPanelHeight = 450;
constexpr int SettingCorner = 210, SettingMaterial = 211, SettingTransparency = 212,
              SettingBlur = 213, SettingFont = 214, SettingColor = 216, ApplySettings = 217,
              RecoverSetting = 218;
constexpr int ResetStyle = 219, ClosePanel = 225, StyleColorName = 227, SettingSound = 230,
              SettingPower = 231, SettingReadonly = 232, PanelDate = 240, PanelTime = 241,
              PanelSetReminder = 242, PanelCancelReminder = 243, PanelStatus = 244,
              OpenDataFolder = 245, GeometryButton = 246;
constexpr int SettingRemember = 248, RememberHint = 249;
constexpr int SettingTopmost = 250, SettingAutostart = 251;
struct PanelContext {
    bool style{};
    HWND target{};
    UINT dpi{};
    HFONT font{}, title{};
    HBRUSH brush{};
};
Note *panelNote(PanelContext &panel) {
    for (auto &n : notes)
        if (n->win == panel.target && n->ready && n->counted && !n->animation)
            return n.get();
    return nullptr;
}
const wchar_t *colorNames[] = {L"奶油黄", L"薄荷绿", L"晴空蓝", L"浅樱粉", L"薰衣草", L"雾灰"};
void updateSettingLabels(HWND hwnd) {
    auto value = [&](int id) { return (int)SendMessageW(GetDlgItem(hwnd, id), TBM_GETPOS, 0, 0); };
    SetWindowTextW(GetDlgItem(hwnd, 220), std::to_wstring(value(SettingBlur)).c_str());
    SetWindowTextW(GetDlgItem(hwnd, 221),
                   (std::to_wstring(value(SettingTransparency)) + L"%").c_str());
    SetWindowTextW(GetDlgItem(hwnd, 222), std::to_wstring(value(SettingFont)).c_str());
    int mode = (int)SendMessageW(GetDlgItem(hwnd, SettingMaterial), CB_GETCURSEL, 0, 0);
    EnableWindow(GetDlgItem(hwnd, SettingBlur), mode == 1);
    EnableWindow(GetDlgItem(hwnd, SettingTransparency), mode != 0);
    int color =
        std::clamp((int)SendMessageW(GetDlgItem(hwnd, SettingColor), CB_GETCURSEL, 0, 0), 0, 5);
    SetWindowTextW(GetDlgItem(hwnd, StyleColorName), colorNames[color]);
    for (int id = 300; id < 306; ++id)
        InvalidateRect(GetDlgItem(hwnd, id), nullptr, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}
void syncPanel(HWND hwnd) {
    auto c = (PanelContext *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!c)
        return;
    if (c->style) {
        SendMessageW(GetDlgItem(hwnd, SettingMaterial), CB_SETCURSEL, preferences.material, 0);
        SendMessageW(GetDlgItem(hwnd, SettingColor), CB_SETCURSEL, preferences.color, 0);
        for (auto item : {std::pair{SettingBlur, preferences.blur},
                          std::pair{SettingTransparency, preferences.transparency},
                          std::pair{SettingFont, preferences.fontSize}})
            SendMessageW(GetDlgItem(hwnd, item.first), TBM_SETPOS, TRUE, item.second);
        updateSettingLabels(hwnd);
        return;
    }
    c->target = stackTop;
    SendMessageW(GetDlgItem(hwnd, SettingCorner), CB_SETCURSEL, preferences.corner, 0);
    EnableWindow(GetDlgItem(hwnd, SettingCorner), !preferences.fixed);
    preferences.autostart = startupEnabled();
    for (auto item :
         {std::pair{SettingSound, preferences.sound}, std::pair{SettingPower, preferences.lowPower},
          std::pair{SettingRemember, preferences.remember},
          std::pair{SettingTopmost, preferences.topmost},
          std::pair{SettingAutostart, preferences.autostart}})
        SendMessageW(GetDlgItem(hwnd, item.first), BM_SETCHECK,
                     item.second ? BST_CHECKED : BST_UNCHECKED, 0);
    auto n = panelNote(*c);
    for (int id : {SettingReadonly, PanelDate, PanelTime, PanelSetReminder, PanelCancelReminder})
        EnableWindow(GetDlgItem(hwnd, id), n != nullptr);
    SendMessageW(GetDlgItem(hwnd, SettingReadonly), BM_SETCHECK,
                 n && n->locked ? BST_CHECKED : BST_UNCHECKED, 0);
    std::wstring status = n ? L"本页尚未设置提醒。" : L"暂无可用纸张，点击便签上的“添加”新增。";
    SYSTEMTIME st{};
    GetLocalTime(&st);
    if (n && n->due) {
        time_t t = n->due;
        tm local{};
        localtime_s(&local, &t);
        st.wYear = local.tm_year + 1900;
        st.wMonth = local.tm_mon + 1;
        st.wDay = local.tm_mday;
        st.wHour = local.tm_hour;
        st.wMinute = local.tm_min;
        wchar_t text[80];
        wcsftime(text, 80, L"本页提醒：%m/%d %H:%M", &local);
        status = text;
    } else {
        FILETIME f{};
        SystemTimeToFileTime(&st, &f);
        ULARGE_INTEGER v{};
        v.LowPart = f.dwLowDateTime;
        v.HighPart = f.dwHighDateTime;
        v.QuadPart += 5ULL * 60 * 10000000;
        f.dwLowDateTime = v.LowPart;
        f.dwHighDateTime = v.HighPart;
        FileTimeToSystemTime(&f, &st);
    }
    SendMessageW(GetDlgItem(hwnd, PanelDate), DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
    SendMessageW(GetDlgItem(hwnd, PanelTime), DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
    SetWindowTextW(GetDlgItem(hwnd, PanelStatus), status.c_str());
    bool recoverable = false;
    std::error_code ec;
    auto recycle = folder / L"Recycled";
    if (fs::exists(recycle, ec))
        for (auto &f : fs::directory_iterator(recycle, ec))
            if (f.path().extension() == L".note") {
                recoverable = true;
                break;
            }
    EnableWindow(GetDlgItem(hwnd, RecoverSetting), recoverable);
}
LRESULT CALLBACK settingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto c = (PanelContext *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        c = new PanelContext;
        c->style = ((CREATESTRUCTW *)lp)->lpCreateParams != nullptr;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
    }
    if (!c)
        return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_CREATE) {
        c->dpi = GetDpiForWindow(hwnd);
        auto scale = [&](int v) { return MulDiv(v, c->dpi, 96); };
        c->brush = CreateSolidBrush(RGB(248, 248, 246));
        c->font = CreateFontW(-scale(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        c->title = CreateFontW(-scale(22), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        auto control = [&](const wchar_t *cls, const wchar_t *title, DWORD style, int id, int x,
                           int y, int w, int h) {
            auto child = CreateWindowExW(0, cls, title,
                                         WS_CHILD | WS_VISIBLE |
                                             (wcscmp(cls, L"STATIC") ? WS_TABSTOP : 0) | style,
                                         scale(x), scale(y), scale(w), scale(h), hwnd,
                                         (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, (WPARAM)c->font, TRUE);
            return child;
        };
        auto title =
            control(L"STATIC", c->style ? L"外观风格" : L"便签设置", 0, 200, 20, 14, 376, 30);
        SendMessageW(title, WM_SETFONT, (WPARAM)c->title, TRUE);
        control(L"STATIC",
                c->style ? L"整本的材质、配色和阅读字号。" : L"桌面位置、日常行为与本页提醒。", 0,
                201, 20, 48, 376, 20);
        if (c->style) {
            control(L"STATIC", L"材质", 0, 203, 20, 96, 68, 20);
            control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, SettingMaterial, 92, 89, 304,
                    160);
            for (auto text : {L"纸张", L"磨砂玻璃", L"平滑玻璃"})
                SendMessageW(GetDlgItem(hwnd, SettingMaterial), CB_ADDSTRING, 0, (LPARAM)text);
            control(L"STATIC", L"磨砂强度", 0, 204, 20, 142, 68, 20);
            control(TRACKBAR_CLASSW, L"", TBS_NOTICKS, SettingBlur, 88, 132, 252, 32);
            control(L"STATIC", L"", 0, 220, 348, 142, 48, 20);
            control(L"STATIC", L"透明度", 0, 205, 20, 180, 68, 20);
            control(TRACKBAR_CLASSW, L"", TBS_NOTICKS, SettingTransparency, 88, 170, 252, 32);
            control(L"STATIC", L"", 0, 221, 348, 180, 48, 20);
            control(L"STATIC", L"字号", 0, 206, 20, 218, 68, 20);
            control(TRACKBAR_CLASSW, L"", TBS_NOTICKS, SettingFont, 88, 208, 252, 32);
            control(L"STATIC", L"", 0, 222, 348, 218, 48, 20);
            for (int id : {SettingBlur, SettingTransparency, SettingFont})
                SendMessageW(GetDlgItem(hwnd, id), TBM_SETRANGE, TRUE,
                             MAKELPARAM(id == SettingBlur           ? 0
                                        : id == SettingTransparency ? 10
                                                                    : 14,
                                        id == SettingBlur           ? 24
                                        : id == SettingTransparency ? 90
                                                                    : 30));
            control(L"STATIC", L"底色", 0, 207, 20, 254, 68, 20);
            control(L"STATIC", L"", 0, StyleColorName, 92, 254, 304, 20);
            control(L"COMBOBOX", L"", CBS_DROPDOWNLIST, SettingColor, 0, 0, 1, 1);
            for (auto text : colorNames)
                SendMessageW(GetDlgItem(hwnd, SettingColor), CB_ADDSTRING, 0, (LPARAM)text);
            ShowWindow(GetDlgItem(hwnd, SettingColor), SW_HIDE);
            for (int i = 0; i < 6; ++i)
                control(L"BUTTON", colorNames[i], BS_OWNERDRAW, 300 + i, 92 + i * 50, 280, 42, 28);
            control(L"STATIC", L"颜色与字号预览", 0, 208, 20, 315, 376, 20);
            control(L"BUTTON", L"应用风格", BS_DEFPUSHBUTTON, ApplySettings, 20, 404, 146, 32);
            control(L"BUTTON", L"恢复默认", BS_PUSHBUTTON, ResetStyle, 178, 404, 132, 32);
            control(L"BUTTON", L"关闭", BS_PUSHBUTTON, ClosePanel, 326, 404, 70, 32);
        } else {
            control(L"STATIC", L"桌面位置", 0, 202, 20, 96, 68, 20);
            control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, SettingCorner, 92, 89, 164,
                    160);
            control(L"BUTTON", L"位置与大小…", BS_PUSHBUTTON, GeometryButton, 266, 89, 130, 28);
            for (auto text : {L"左上角", L"右上角", L"左下角", L"右下角", L"自由定位"})
                SendMessageW(GetDlgItem(hwnd, SettingCorner), CB_ADDSTRING, 0, (LPARAM)text);
            control(L"STATIC", L"顶栏“固定”可锁定整本位置。", 0, 247, 92, 124, 304, 20);
            control(L"BUTTON", L"提醒时播放声音", BS_AUTOCHECKBOX, SettingSound, 20, 152, 178, 24);
            control(L"BUTTON", L"降低玻璃刷新频率", BS_AUTOCHECKBOX, SettingPower, 212, 152, 184,
                    24);
            control(L"BUTTON", L"窗口置顶（整本）", BS_AUTOCHECKBOX, SettingTopmost, 20, 178, 178,
                    24);
            control(L"BUTTON", L"开机自动启动", BS_AUTOCHECKBOX, SettingAutostart, 212, 178, 184,
                    24);
            control(L"BUTTON", L"记忆设置：启动时恢复未完成事件", BS_AUTOCHECKBOX, SettingRemember,
                    20, 204, 376, 24);
            control(L"STATIC", L"关闭记忆后启动显示空白页，原事项仍保留。", 0, RememberHint, 20,
                    230, 376, 20);
            control(L"STATIC", L"当前页", 0, 203, 20, 264, 92, 20);
            control(L"BUTTON", L"只读，避免误改内容", BS_AUTOCHECKBOX, SettingReadonly, 124, 261,
                    272, 24);
            control(DATETIMEPICK_CLASSW, L"", DTS_SHORTDATEFORMAT, PanelDate, 20, 289, 244, 28);
            control(DATETIMEPICK_CLASSW, L"", DTS_TIMEFORMAT, PanelTime, 276, 289, 120, 28);
            SendMessageW(GetDlgItem(hwnd, PanelTime), DTM_SETFORMATW, 0, (LPARAM)L"HH:mm");
            control(L"BUTTON", L"设置本页提醒", BS_PUSHBUTTON, PanelSetReminder, 20, 325, 182, 28);
            control(L"BUTTON", L"取消提醒", BS_PUSHBUTTON, PanelCancelReminder, 214, 325, 182, 28);
            control(L"STATIC", L"", 0, PanelStatus, 20, 357, 376, 20);
            control(L"BUTTON", L"恢复最近撕下的纸张", BS_PUSHBUTTON, RecoverSetting, 20, 380, 182,
                    24);
            control(L"BUTTON", L"打开数据文件夹", BS_PUSHBUTTON, OpenDataFolder, 214, 380, 182, 24);
            control(L"BUTTON", L"应用设置", BS_DEFPUSHBUTTON, ApplySettings, 20, 414, 178, 26);
            control(L"BUTTON", L"关闭", BS_PUSHBUTTON, ClosePanel, 310, 414, 86, 26);
        }
        syncPanel(hwnd);
        return 0;
    }
    if (msg == WM_HSCROLL && c->style) {
        updateSettingLabels(hwnd);
        return 0;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        auto checked = [&](int control) {
            return SendMessageW(GetDlgItem(hwnd, control), BM_GETCHECK, 0, 0) == BST_CHECKED;
        };
        auto select = [&](int control) {
            return (int)SendMessageW(GetDlgItem(hwnd, control), CB_GETCURSEL, 0, 0);
        };
        auto slider = [&](int control) {
            return (int)SendMessageW(GetDlgItem(hwnd, control), TBM_GETPOS, 0, 0);
        };
        if (id == ClosePanel || id == IDCANCEL) {
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        if (c->style) {
            if (id == SettingMaterial && HIWORD(wp) == CBN_SELCHANGE) {
                updateSettingLabels(hwnd);
                return 0;
            }
            if (id >= 300 && id < 306) {
                SendMessageW(GetDlgItem(hwnd, SettingColor), CB_SETCURSEL, id - 300, 0);
                updateSettingLabels(hwnd);
                return 0;
            }
            if (id == ResetStyle) {
                SendMessageW(GetDlgItem(hwnd, SettingMaterial), CB_SETCURSEL, 1, 0);
                SendMessageW(GetDlgItem(hwnd, SettingColor), CB_SETCURSEL, 0, 0);
                SendMessageW(GetDlgItem(hwnd, SettingBlur), TBM_SETPOS, TRUE, 12);
                SendMessageW(GetDlgItem(hwnd, SettingTransparency), TBM_SETPOS, TRUE, 50);
                SendMessageW(GetDlgItem(hwnd, SettingFont), TBM_SETPOS, TRUE, 18);
                updateSettingLabels(hwnd);
                return 0;
            }
        } else {
            if (id == GeometryButton) {
                openGeometry();
                return 0;
            }
            if (id == RecoverSetting) {
                restoreRecent();
                syncPanel(hwnd);
                return 0;
            }
            if (id == OpenDataFolder) {
                ShellExecuteW(hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (id == PanelSetReminder || id == PanelCancelReminder) {
                auto n = panelNote(*c);
                if (!n) {
                    syncPanel(hwnd);
                    return 0;
                }
                auto previous = n->due;
                if (id == PanelCancelReminder)
                    n->due = 0;
                else {
                    SYSTEMTIME date{}, time{};
                    SendMessageW(GetDlgItem(hwnd, PanelDate), DTM_GETSYSTEMTIME, 0, (LPARAM)&date);
                    SendMessageW(GetDlgItem(hwnd, PanelTime), DTM_GETSYSTEMTIME, 0, (LPARAM)&time);
                    tm local{};
                    local.tm_year = date.wYear - 1900;
                    local.tm_mon = date.wMonth - 1;
                    local.tm_mday = date.wDay;
                    local.tm_hour = time.wHour;
                    local.tm_min = time.wMinute;
                    local.tm_isdst = -1;
                    auto due = mktime(&local);
                    if (due <= std::time(nullptr)) {
                        SetWindowTextW(GetDlgItem(hwnd, PanelStatus), L"请选择未来的日期和时间。 ");
                        return 0;
                    }
                    n->due = due;
                }
                if (!save(*n)) {
                    n->due = previous;
                    SetWindowTextW(GetDlgItem(hwnd, PanelStatus), L"提醒保存失败，请检查磁盘。 ");
                    return 0;
                }
                syncPanel(hwnd);
                return 0;
            }
        }
        if (id == ApplySettings) {
            auto previous = preferences;
            if (c->style) {
                preferences.material = select(SettingMaterial);
                preferences.color = select(SettingColor);
                preferences.blur = slider(SettingBlur);
                preferences.transparency = slider(SettingTransparency);
                preferences.fontSize = slider(SettingFont);
            } else {
                if (!preferences.fixed)
                    preferences.corner = select(SettingCorner);
                preferences.sound = checked(SettingSound);
                preferences.lowPower = checked(SettingPower);
                preferences.remember = checked(SettingRemember);
                preferences.topmost = checked(SettingTopmost);
                preferences.autostart = checked(SettingAutostart);
            }
            if (!savePreferences()) {
                preferences = previous;
                MessageBoxW(hwnd, L"设置保存失败，请检查磁盘或权限。", L"轻笺",
                            MB_OK | MB_ICONERROR);
                return 0;
            }
            if (!c->style && preferences.autostart != previous.autostart) {
                auto error = setStartup(preferences.autostart);
                if (error != ERROR_SUCCESS) {
                    preferences = previous;
                    bool restored = savePreferences();
                    syncPanel(hwnd);
                    auto message =
                        L"开机启动设置失败（错误 " + std::to_wstring(error) + L"）。" +
                        (restored ? L"本次修改已撤回。" : L"设置文件恢复失败，请检查磁盘。") +
                        (error == ERROR_FILENAME_EXCED_RANGE ? L"请将软件移动到较短的路径后重试。"
                                                             : L"");
                    MessageBoxW(hwnd, message.c_str(), L"轻笺", MB_OK | MB_ICONERROR);
                    return 0;
                }
            }
            bool ok = true;
            for (auto &n : notes)
                if (n->win && n->ready && !n->animation && n->counted) {
                    if (c->style)
                        syncStackMaterial(*n);
                    if (!c->style && n->win == c->target)
                        n->locked = checked(SettingReadonly);
                    refresh(*n);
                    if (!save(*n))
                        ok = false;
                }
            SetTimer(controller, 1, preferences.lowPower ? 260 : 120, nullptr);
            showAll();
            if (ok)
                ShowWindow(hwnd, SW_HIDE);
            else
                MessageBoxW(hwnd, L"部分便签保存失败，请检查后再试。", L"轻笺",
                            MB_OK | MB_ICONERROR);
            return 0;
        }
    }
    if (msg == WM_DRAWITEM && c->style) {
        auto d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlID >= 300 && d->CtlID < 306) {
            int index = d->CtlID - 300,
                selected = (int)SendMessageW(GetDlgItem(hwnd, SettingColor), CB_GETCURSEL, 0, 0);
            FillRect(d->hDC, &d->rcItem, c->brush);
            auto brush = CreateSolidBrush(colors[index]);
            auto pen = CreatePen(PS_SOLID, index == selected ? 2 : 1,
                                 index == selected ? RGB(76, 102, 124) : RGB(208, 207, 199));
            auto oldB = SelectObject(d->hDC, brush), oldP = SelectObject(d->hDC, pen);
            RECT r = d->rcItem;
            InflateRect(&r, -2, -2);
            RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, 10, 10);
            if (index == selected) {
                SelectObject(d->hDC, c->font);
                SetBkMode(d->hDC, TRANSPARENT);
                SetTextColor(d->hDC, RGB(55, 71, 82));
                DrawTextW(d->hDC, L"✓", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(d->hDC, oldB);
            SelectObject(d->hDC, oldP);
            DeleteObject(brush);
            DeleteObject(pen);
            return TRUE;
        }
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        auto scale = [&](int v) { return MulDiv(v, c->dpi, 96); };
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(224, 225, 221));
        auto old = SelectObject(dc, pen);
        for (int y : {78, c->style ? 392 : 408}) {
            MoveToEx(dc, scale(20), scale(y), nullptr);
            LineTo(dc, scale(396), scale(y));
        }
        if (c->style) {
            int index = std::clamp(
                    (int)SendMessageW(GetDlgItem(hwnd, SettingColor), CB_GETCURSEL, 0, 0), 0, 5),
                size = (int)SendMessageW(GetDlgItem(hwnd, SettingFont), TBM_GETPOS, 0, 0);
            RECT card{scale(20), scale(340), scale(396), scale(382)};
            auto paper = CreateSolidBrush(colors[index]);
            auto oldBrush = SelectObject(dc, paper);
            auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            RoundRect(dc, card.left, card.top, card.right, card.bottom, scale(8), scale(8));
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            auto font = CreateFontW(-scale(size), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0,
                                    0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
            auto oldFont = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(45, 51, 57));
            card.left += scale(14);
            card.right -= scale(14);
            DrawTextW(dc, L"把今天的灵感，留在这一页。", -1, &card,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(dc, oldFont);
            DeleteObject(font);
            DeleteObject(paper);
        } else {
            MoveToEx(dc, scale(20), scale(256), nullptr);
            LineTo(dc, scale(396), scale(256));
        }
        SelectObject(dc, old);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        int id = GetDlgCtrlID((HWND)lp);
        SetTextColor((HDC)wp, id == 201 || id == 247 || id == 208 || id == RememberHint
                                  ? RGB(118, 122, 123)
                                  : RGB(40, 48, 55));
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
