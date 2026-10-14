#pragma once

RECT placement(Note &n) {
    RECT current{};
    GetWindowRect(n.win, &current);
    int w = preferences.width ? px(n, preferences.width) : current.right - current.left;
    int h = preferences.height ? px(n, preferences.height) : current.bottom - current.top;
    int x = preferences.left == -100001 ? current.left : preferences.left;
    int y = preferences.top == -100001 ? current.top : preferences.top;
    RECT proposed{x, y, x + w, y + h};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW((preferences.corner == 4 || preferences.fixed)
                        ? MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST)
                        : MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY),
                    &monitor);
    auto work = monitor.rcWork;
    int maxW = std::min(2400L, work.right - work.left),
        maxH = std::min(2400L, work.bottom - work.top);
    if (preferences.fixed) {
        x = std::clamp<int>(x, work.left,
                            std::max<int>(work.left, work.right - std::min(px(n, 370), maxW)));
        y = std::clamp<int>(y, work.top,
                            std::max<int>(work.top, work.bottom - std::min(px(n, 340), maxH)));
        maxW = std::min<int>(maxW, work.right - x);
        maxH = std::min<int>(maxH, work.bottom - y);
    }
    w = std::clamp(w, std::min(px(n, 370), maxW), maxW);
    h = std::clamp(h, std::min(px(n, 340), maxH), maxH);
    if (preferences.corner != 4 && !preferences.fixed) {
        int margin = px(n, 16);
        x = (preferences.corner == 1 || preferences.corner == 3) ? work.right - margin - w
                                                                 : work.left + margin;
        y = preferences.corner >= 2 ? work.bottom - margin - h : work.top + margin;
    }
    x = std::clamp<int>(x, work.left, std::max<int>(work.left, work.right - w));
    y = std::clamp<int>(y, work.top, std::max<int>(work.top, work.bottom - h));
    return RECT{x, y, x + w, y + h};
}

void syncStackMaterial(Note &n) {
    bool brushChanged = n.material != preferences.material || n.blur != preferences.blur ||
                        n.transparency != preferences.transparency || n.color != preferences.color;
    bool fontChanged = n.fontSize != preferences.fontSize;
    if (!brushChanged && !fontChanged)
        return;
    n.material = preferences.material;
    n.blur = preferences.blur;
    n.transparency = preferences.transparency;
    n.color = preferences.color;
    n.fontSize = preferences.fontSize;
    if (n.win) {
        if (brushChanged)
            paperBrush(n);
        if (fontChanged)
            fonts(n);
        refresh(n);
        RedrawWindow(n.win, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void inheritGlassSurface(Note &source, Note &target) {
    if (!target.material) {
        if (target.surface || target.glassReady)
            paperBrush(target);
        return;
    }
    if (!source.glassReady || !source.surface || source.material != target.material)
        return;
    BITMAP size{};
    GetObjectW(source.surface, sizeof(size), &size);
    RECT client{};
    GetClientRect(target.win, &client);
    if (size.bmWidth != client.right || size.bmHeight != client.bottom)
        return;
    if (target.glassReady && target.glassRevision == source.glassRevision &&
        EqualRect(&target.glassRect, &source.glassRect))
        return;
    HBITMAP bitmap = (HBITMAP)CopyImage(source.surface, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!bitmap)
        return;
    HBRUSH brush = CreatePatternBrush(bitmap);
    if (!brush) {
        DeleteObject(bitmap);
        return;
    }
    if (target.brush)
        DeleteObject(target.brush);
    if (target.surface)
        DeleteObject(target.surface);
    target.surface = bitmap;
    target.brush = brush;
    target.glassReady = true;
    target.glassRevision = source.glassRevision;
    target.glassRect = source.glassRect;
    bool excluded = capturePauseUntil <= GetTickCount64();
    bool affinityOK = SetWindowDisplayAffinity(target.win, excluded ? 0x11 : WDA_NONE) != FALSE;
    target.excluded = affinityOK && excluded;
    testProperty(target.win, L"GlassReady", (HANDLE)1);
    clearTestProperty(target.win, L"GlassError");
    refresh(target);
}

void placePage(Note &n, const RECT &target, bool headerOnly = false) {
    RECT old{};
    GetWindowRect(n.win, &old);
    if (!EqualRect(&old, &target)) {
        n.glassRevision = 0;
        SetWindowPos(n.win, nullptr, target.left, target.top, target.right - target.left,
                     target.bottom - target.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    book::roundShape(n.win, n.dpi, headerOnly);
}

void anchorAll() {
    std::vector<Note *> live;
    Note *previous = nullptr;
    for (auto &n : notes) {
        if (n->win == displayedPage && n->win)
            previous = n.get();
        if (n->win && !n->animation && n->counted)
            live.push_back(n.get());
    }
    if (live.empty()) {
        stackTop = nullptr;
        clearTestProperty(controller, L"StackTop");
        for (auto &n : notes)
            if (n->win && !n->animation) {
                syncStackMaterial(*n);
                placePage(*n, placement(*n), true);
                updatePageControls(*n);
                break;
            }
        syncPadTopmost();
        return;
    }
    auto selected =
        std::find_if(live.begin(), live.end(), [](auto n) { return n->win == stackTop; });
    Note &front = **(selected == live.end() ? live.begin() : selected);
    stackTop = front.win;
    RECT target = placement(front);
    for (auto n : live) {
        syncStackMaterial(*n);
        placePage(*n, target);
    }
    if (previous && previous != &front)
        inheritGlassSurface(*previous, front);
    updatePageControls(front);
    if (!initialSettings) {
        bool replace = previous && previous != &front && IsWindowVisible(previous->win);
        if (!IsWindowVisible(front.win)) {
            // Paint the target behind the old sheet, then reveal a complete composition.
            SetWindowPos(front.win, replace ? previous->win : HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(front.win, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            if (replace)
                testCount(controller, L"SwitchPrepared");
            SetWindowPos(front.win, preferences.topmost ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (replace)
                DwmFlush();
        }
        for (auto n : live)
            if (n != &front && IsWindowVisible(n->win))
                ShowWindow(n->win, SW_HIDE);
    } else
        for (auto n : live)
            if (IsWindowVisible(n->win))
                ShowWindow(n->win, SW_HIDE);
    displayedPage = stackTop;
    syncPadTopmost();
    testProperty(controller, L"StackTop", (HANDLE)stackTop);
    if (!initialSettings && preferences.remember)
        SetTimer(controller, 2, 500, nullptr);
}

void switchStack(int direction) {
    std::vector<HWND> live;
    for (auto &n : notes)
        if (n->win && !n->animation && n->counted)
            live.push_back(n->win);
    if (live.size() < 2)
        return;
    auto it = std::find(live.begin(), live.end(), stackTop);
    size_t index = it == live.end() ? 0 : (size_t)(it - live.begin());
    stackTop = live[(index + (direction < 0 ? live.size() - 1 : 1)) % live.size()];
    anchorAll();
    SetForegroundWindow(stackTop);
    SetFocus(GetDlgItem(stackTop, Edit));
    syncPadTopmost();
}
