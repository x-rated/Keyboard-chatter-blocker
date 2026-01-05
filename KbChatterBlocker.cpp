#include <windows.h>
#include <unordered_map>
#include <chrono>

// ================= CONFIG =================

const int CHATTER_THRESHOLD_MS = 85;         // Block very fast double presses
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold while holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode

// ================= DATA ===================

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

// ================= TIME ===================

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// ================= LOGIC ==================

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long now = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = now;
            return false;
        }

        long long delta = now - state.lastPressTime;

        int threshold = CHATTER_THRESHOLD_MS;

        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else if (delta > REPEAT_TRANSITION_DELAY_MS) {
            state.inRepeatMode = true;
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        }

        if (delta < threshold) {
            state.blockedCount++;
            return true; // chatter detected
        }

        state.lastPressTime = now;
        return false;
    } else {
        // key up
        state.lastReleaseTime = now;
        state.inRepeatMode = false;
        return false;
    }
}

// ================= HOOK ===================

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

        // 👉 IGNORE INJECTED INPUT (macros, AHK, G Hub, etc.)
        if (kb->flags & LLKHF_INJECTED) {
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

        if (isKeyDown || isKeyUp) {
            if (ShouldBlockKey(kb->vkCode, isKeyDown)) {
                return 1; // block physical chatter
            }
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// ================= ENTRY ==================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Prevent multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    hHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        NULL,
        0
    );

    if (!hHook) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hHook);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
