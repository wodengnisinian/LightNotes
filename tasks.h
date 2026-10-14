// A ruled native text editor with one completion box per newline-delimited event.
// EDIT handles wrapping; its viewport is fixed and the gutter shares its line positions.
void invalidateTasks(Note &n, bool changed) {
    if (changed) {
        n.tasksDirty = true;
        ++n.taskRevision;
    }
    if (n.edit)
        InvalidateRect(n.edit, nullptr, FALSE);
    if (n.gutter)
        InvalidateRect(n.gutter, nullptr, FALSE);
}
void syncTasks(Note &n) {
    if (!n.tasksDirty)
        return;
    n.taskSnapshot = readText(n.edit);
    n.tasks.clear();
    int length = (int)n.taskSnapshot.size(), begin = 0;
    for (int i = 0; i < length;) {
        if (n.taskSnapshot[i] != L'\r' && n.taskSnapshot[i] != L'\n') {
            ++i;
            continue;
        }
        int after = i + 1;
        if (n.taskSnapshot[i] == L'\r' && after < length && n.taskSnapshot[after] == L'\n')
            ++after;
        n.tasks.push_back({begin, i, after});
        begin = after;
        i = after;
    }
    n.tasks.push_back({begin, length, length});
    n.tasksDirty = false;
}
bool taskHasText(const Note &n, const TaskRange &task) {
    for (int i = task.begin; i < task.end; ++i)
        if (n.taskSnapshot[i] != L' ' && n.taskSnapshot[i] != L'\t')
            return true;
    return false;
}
struct TaskLayout {
    int first, lineHeight, top;
    RECT editor;
};
TaskLayout taskLayout(Note &n) {
    TaskLayout l{};
    l.first = (int)SendMessageW(n.edit, EM_GETFIRSTVISIBLELINE, 0, 0);
    SendMessageW(n.edit, EM_GETRECT, 0, (LPARAM)&l.editor);
    HDC dc = GetDC(n.edit);
    auto old = SelectObject(dc, n.font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, old);
    ReleaseDC(n.edit, dc);
    l.lineHeight = std::max(1L, metrics.tmHeight);
    l.top = l.editor.top;
    auto position = [&](int line) {
        int index = (int)SendMessageW(n.edit, EM_LINEINDEX, line, 0);
        return index < 0 ? (LRESULT)-1 : SendMessageW(n.edit, EM_POSFROMCHAR, index, 0);
    };
    auto a = position(l.first), b = position(l.first + 1);
    if (a != -1)
        l.top = GET_Y_LPARAM(a);
    if (a != -1 && b != -1 && GET_Y_LPARAM(b) > l.top)
        l.lineHeight = GET_Y_LPARAM(b) - l.top;
    return l;
}
COLORREF taskRuleColor(Note &n) {
    auto color = colors[n.color];
    return RGB(GetRValue(color) * 80 / 100, GetGValue(color) * 80 / 100,
               GetBValue(color) * 80 / 100);
}
void drawTaskRules(Note &n, HDC dc, int width, int height, const TaskLayout &l) {
    auto pen = CreatePen(PS_SOLID, 1, taskRuleColor(n));
    auto old = SelectObject(dc, pen);
    for (int y = l.top + l.lineHeight - 1; y < height; y += l.lineHeight)
        if (y >= 0) {
            MoveToEx(dc, 0, y, nullptr);
            LineTo(dc, width, y);
        }
    SelectObject(dc, old);
    DeleteObject(pen);
}
void paintTaskLines(Note &n, HDC dc) {
    if (!dc || !n.edit)
        return;
    RECT r{};
    GetClientRect(n.edit, &r);
    auto l = taskLayout(n);
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    drawTaskRules(n, dc, r.right, r.bottom, l);
    RestoreDC(dc, saved);
}
std::vector<std::pair<int, RECT>> visibleTaskBoxes(Note &n) {
    syncTasks(n);
    auto l = taskLayout(n);
    RECT r{};
    GetClientRect(n.gutter, &r);
    std::vector<std::pair<int, RECT>> boxes;
    int size = px(n, 16), x = (r.right - size) / 2;
    int firstChar = (int)SendMessageW(n.edit, EM_LINEINDEX, l.first, 0);
    if (firstChar < 0)
        firstChar = 0;
    auto first =
        std::lower_bound(n.tasks.begin(), n.tasks.end(), firstChar,
                         [](const TaskRange &task, int offset) { return task.after < offset; });
    for (auto it = first; it != n.tasks.end(); ++it) {
        int line = (int)SendMessageW(n.edit, EM_LINEFROMCHAR, it->begin, 0),
            y = l.top + (line - l.first) * l.lineHeight + (l.lineHeight - size) / 2;
        if (y >= r.bottom)
            break;
        if (y + size <= 0 || !taskHasText(n, *it))
            continue;
        boxes.push_back({(int)(it - n.tasks.begin()), RECT{x, y, x + size, y + size}});
    }
    return boxes;
}
int taskHit(Note &n, POINT point) {
    if (n.locked || !n.ready || n.animation)
        return -1;
    for (auto &box : visibleTaskBoxes(n))
        if (PtInRect(&box.second, point))
            return box.first;
    return -1;
}
void drawTasks(Note &n, HDC dc) {
    RECT r{};
    GetClientRect(n.gutter, &r);
    alignBrush(dc, n.gutter, n);
    FillRect(dc, &r, n.brush);
    drawTaskRules(n, dc, r.right, r.bottom, taskLayout(n));
    auto pen = CreatePen(PS_SOLID, 1, n.locked ? RGB(160, 162, 159) : RGB(91, 99, 99));
    auto oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    for (auto &item : visibleTaskBoxes(n)) {
        auto box = item.second;
        Rectangle(dc, box.left, box.top, box.right, box.bottom);
        if (n.taskPressed == item.first && n.taskInside) {
            auto tick = CreatePen(PS_SOLID, std::max(1, px(n, 2)), RGB(48, 113, 83));
            auto previous = SelectObject(dc, tick);
            MoveToEx(dc, box.left + px(n, 3), box.top + px(n, 8), nullptr);
            LineTo(dc, box.left + px(n, 7), box.top + px(n, 12));
            LineTo(dc, box.right - px(n, 2), box.top + px(n, 4));
            SelectObject(dc, previous);
            DeleteObject(tick);
        }
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}
void completeTask(Note &n, int index) {
    syncTasks(n);
    if (n.locked || !n.ready || n.animation || index < 0 || index >= (int)n.tasks.size())
        return;
    auto original = n.taskSnapshot;
    auto task = n.tasks[index];
    if (!taskHasText(n, task))
        return;
    int begin = task.begin, end = task.after;
    if (end == (int)original.size() && task.end == task.after && begin > 0) {
        if (original[begin - 1] == L'\n')
            --begin;
        if (begin > 0 && original[begin - 1] == L'\r')
            --begin;
    }
    DWORD selectionStart = 0, selectionEnd = 0;
    SendMessageW(n.edit, EM_GETSEL, (WPARAM)&selectionStart, (LPARAM)&selectionEnd);
    SendMessageW(n.edit, EM_SETSEL, begin, end);
    SendMessageW(n.edit, EM_REPLACESEL, TRUE, (LPARAM)L"");
    KillTimer(n.win, 2);
    if (!save(n)) {
        SendMessageW(n.edit, EM_UNDO, 0, 0);
        if (readText(n.edit) != original)
            SetWindowTextW(n.edit, original.c_str());
        SendMessageW(n.edit, EM_SETSEL, selectionStart, selectionEnd);
        n.text = original;
        n.message = L"保存失败，事项已保留";
        refresh(n);
        MessageBoxW(n.win, L"完成状态未能保存，该事项已保留。请检查磁盘或权限后重试。", L"轻笺",
                    MB_OK | MB_ICONERROR);
    }
    invalidateTasks(n, true);
    SetFocus(n.edit);
}
LRESULT CALLBACK tasksProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto n = (Note *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
        n = (Note *)((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)n);
    }
    if (!n)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        PaintBuffer buffer(dc, r);
        drawTasks(*n, buffer.dc());
        buffer.present();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINT:
    case WM_PRINTCLIENT:
        drawTasks(*n, (HDC)wp);
        return 0;
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_HSCROLL:
        return 0;
    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd, &p);
        SetCursor(LoadCursorW(nullptr, taskHit(*n, p) >= 0 ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        int hit = taskHit(*n, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        if (hit >= 0) {
            n->taskPressed = hit;
            n->taskPressRevision = n->taskRevision;
            n->taskInside = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (n->taskPressed >= 0) {
            n->taskInside =
                taskHit(*n, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == n->taskPressed;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        int pressed = n->taskPressed;
        bool apply = pressed >= 0 && n->taskPressRevision == n->taskRevision &&
                     taskHit(*n, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}) == pressed;
        n->taskPressed = -1;
        n->taskInside = false;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        if (apply)
            completeTask(*n, pressed);
        return 0;
    }
    case WM_CAPTURECHANGED:
        n->taskPressed = -1;
        n->taskInside = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
