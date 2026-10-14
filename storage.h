#pragma once
#include <windows.h>
#include <filesystem>

// Callers validate backups before atomically replacing the committed file.
inline bool writeTemporary(const std::filesystem::path &file, const void *data, DWORD size) {
    HANDLE out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    bool ok = WriteFile(out, data, size, &written, nullptr) && written == size;
    if (ok)
        ok = FlushFileBuffers(out) != FALSE;
    CloseHandle(out);
    return ok;
}
