#pragma once

// Test instances use an inert registry key, never the user's real logon list.
std::wstring startupKey() {
    return testMode ? L"Software\\LightNotes\\TestStartup\\" + folder.filename().wstring()
                    : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}
std::wstring startupCommand() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768)
        return {};
    std::wstring command = L"\"" + std::wstring(path) + L"\"";
    if (testMode)
        command += L" --test-mode --data-dir \"" + folder.wstring() + L"\"";
    return command;
}
bool startupEnabled() {
    auto key = startupKey();
    DWORD type = 0, bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"LightNotes", RRF_RT_REG_SZ, &type, nullptr,
                     &bytes) != ERROR_SUCCESS ||
        bytes < 2 || bytes > 65536)
        return false;
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"LightNotes", RRF_RT_REG_SZ, &type,
                     value.data(), &bytes) != ERROR_SUCCESS)
        return false;
    value.resize(wcsnlen(value.data(), value.size()));
    return value == startupCommand();
}
LSTATUS setStartup(bool enabled) {
    auto path = startupKey();
    auto command = startupCommand();
    HKEY key{};
    LSTATUS status;
    if (enabled) {
        if (command.empty() || command.size() > 260)
            return ERROR_FILENAME_EXCED_RANGE;
        status = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                                 nullptr, &key, nullptr);
    } else {
        status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND)
            return ERROR_SUCCESS;
    }
    if (status != ERROR_SUCCESS)
        return status;
    status = enabled ? RegSetValueExW(key, L"LightNotes", 0, REG_SZ, (const BYTE *)command.c_str(),
                                      (DWORD)((command.size() + 1) * sizeof(wchar_t)))
                     : RegDeleteValueW(key, L"LightNotes");
    RegCloseKey(key);
    return !enabled && status == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : status;
}
// Only update the band when it changed, preserving page-switch repaint ordering.
void syncTopmost(HWND window) {
    if (!window)
        return;
    bool current = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    if (current == preferences.topmost)
        return;
    SetWindowPos(window, preferences.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void syncPadTopmost() {
    for (auto &note : notes) {
        syncTopmost(note->win);
        syncTopmost(note->tearWindow);
    }
}
