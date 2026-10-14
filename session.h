#pragma once

// Text stays in .note files; this small atomic snapshot remembers only page order and selection.
bool pendingEvents(const std::wstring &text) {
    return text.find_first_not_of(L" \t\r\n") != std::wstring::npos;
}
bool sessionPage(const Note &n) {
    return n.win && n.ready && n.counted && !n.animation && n.file.parent_path() == folder;
}
bool saveSession() {
    if (!preferences.remember)
        return true;
    std::wstring active, data = L"\xfeff[session]\r\n";
    int count = 0;
    for (auto &n : notes)
        if (sessionPage(*n)) {
            auto name = n->file.filename().wstring();
            data += L"page" + std::to_wstring(count++) + L"=" + name + L"\r\n";
            if (n->win == stackTop)
                active = name;
        }
    data += L"count=" + std::to_wstring(count) + L"\r\nactive=" + active + L"\r\n";
    auto file = folder / L"session.ini", tmp = folder / L"session.ini.tmp";
    DWORD size = (DWORD)(data.size() * sizeof(wchar_t));
    return writeTemporary(tmp, data.data(), size) &&
           MoveFileExW(tmp.c_str(), file.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
void restoreSession() {
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto &entry : fs::directory_iterator(folder, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == L".note")
            files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    if (!preferences.remember) {
        // Reuse a genuinely empty sheet; turning memory off never removes previous events.
        for (auto &file : files) {
            Note stored;
            if (readFile(file, stored) && stored.text.empty() && !stored.due && !stored.locked) {
                createNote(file);
                stackTop = notes.back()->win;
                return;
            }
        }
        createNote();
        stackTop = notes.back()->win;
        return;
    }
    auto session = folder / L"session.ini";
    auto value = [&](const std::wstring &key) {
        wchar_t result[1024]{};
        GetPrivateProfileStringW(L"session", key.c_str(), L"", result, 1024, session.c_str());
        return std::wstring(result);
    };
    std::vector<fs::path> ordered;
    int count =
        std::clamp((int)GetPrivateProfileIntW(L"session", L"count", 0, session.c_str()), 0, 10000);
    for (int i = 0; i < count; ++i) {
        auto name = value(L"page" + std::to_wstring(i));
        auto found = std::find_if(files.begin(), files.end(),
                                  [&](auto &file) { return file.filename().wstring() == name; });
        if (found != files.end()) {
            ordered.push_back(*found);
            files.erase(found);
        }
    }
    ordered.insert(ordered.end(), files.begin(), files.end());
    for (auto &file : ordered)
        createNote(file);
    if (notes.empty())
        createNote();
    auto active = value(L"active");
    Note *selected = nullptr;
    Note *unfinished = nullptr;
    for (auto &n : notes)
        if (sessionPage(*n)) {
            if (!unfinished && pendingEvents(n->text))
                unfinished = n.get();
            if (n->file.filename().wstring() == active)
                selected = n.get();
        }
    if (!selected || (!pendingEvents(selected->text) && unfinished))
        selected = unfinished;
    if (!selected)
        for (auto &n : notes)
            if (sessionPage(*n)) {
                selected = n.get();
                break;
            }
    stackTop = selected ? selected->win : nullptr;
}
