#pragma once

// Native regression scripts use these command IDs; no hidden style commands
// are accepted by a normal app instance. The visible style panel is authoritative.
bool testStyleCommand(Note &note, int command) {
    if (!testMode)
        return false;
    constexpr int CycleColor = 103, Paper = 140, Frost = 141, Smooth = 142;
    if (command != CycleColor && command != Paper && command != Frost && command != Smooth)
        return false;
    auto previous = preferences;
    if (command == CycleColor)
        preferences.color = (preferences.color + 1) % 6;
    else
        preferences.material = command - Paper;
    if (!savePreferences()) {
        preferences = previous;
        note.message = L"风格保存失败";
        refresh(note);
        return true;
    }
    for (auto &page : notes)
        if (page->win && page->ready && page->counted && !page->animation) {
            syncStackMaterial(*page);
            save(*page);
        }
    return true;
}
