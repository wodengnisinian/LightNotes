#pragma once

// Shared by queued keyboard input and directly sent native EDIT messages.
bool noteShortcut(Note &note, WPARAM key) {
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (ctrl) {
        switch (key) {
        case VK_TAB:
            switchStack((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
            return true;
        case 'L':
            SendMessageW(note.win, WM_COMMAND, Lock, 0);
            return true;
        case 'N':
            createNote();
            return true;
        case 'S':
            save(note);
            return true;
        case 'P':
            SendMessageW(note.win, WM_COMMAND, Pin, 0);
            return true;
        }
    }
    if (key == VK_ESCAPE) {
        SendMessageW(note.win, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}
