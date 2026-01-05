#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <string>
#include <psapi.h>  // for GetModuleBaseName

// Configuration
const int CHATTER_THRESHOLD_MS = 85;         // Block normal chatter
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode
const int FORCE_ALLOW_MS = 60;               // Any press faster than this is always allowed in system apps

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

// Returns current time in milliseconds
long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// Checks if the foreground window is a system app (not a game)
bool IsSystemAppActive() {
    HWND fg = GetForegroundWindow();
    if (!fg) return true; // no window = system

    DWORD pid;
    GetWindowThreadProcessId(fg, &pid);
    HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return true;

    char exeName[MAX_PATH] = {0};
    if (GetModuleBaseNameA(hProc, NULL, exeName, MAX_PATH) == 0) {
        CloseHandle(hProc);
        return true;
    }
    CloseHandle(hProc);

    std::string nameStr = exeName;
    // blacklist of games
    const char* gameExes[] = { "csgo.exe", "valorant.exe", "fortnite.exe", "dota2.exe" };
    for (auto g : gameExes) {
        if (_stricmp(nameStr.c_str(), g) == 0) return false; // game detected, bypass filtering
    }
    return true; // system application
}

// Determines if the key should be blocked
bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;

        // In system apps, allow ultra-fast presses for macros
        if (IsSystemAppActive() && timeSincePress < FORCE_ALLOW_MS) {
            state.lastPressTime = currentTime;
            return false; // allow very fast presses
        }

        // Normal chatter logic
        int threshold;
        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else {
            threshold = CHATTER_THRESHOLD_MS;
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
        }

        if (timeSincePress < threshold) {
            state.blockedCount++;
            return true; // block chatter
        }

        state.lastPressTime = currentTime;
        return false;
    } else {
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        return false;
    }
}

// Low-level keyboard hook callback
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = kb->vkCode;

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        if (ShouldBlockKey(vkCode, isKeyDown)) {
            return 1; // block the key
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Create mutex to prevent multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Install low-level keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (!hHook) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(hHook);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
