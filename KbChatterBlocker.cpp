#include <windows.h>
#include <unordered_map>
#include <chrono>

// Configuration
const int CHATTER_THRESHOLD_MS = 85;         // Fast press threshold
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode
const int MACRO_TIMING_TOLERANCE_MS = 15;    // If hold time ≈ gap time, it's a macro

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    long long currentPressStartTime = 0;
    bool inRepeatMode = false;
    bool currentlyPressed = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            state.currentPressStartTime = currentTime;
            state.currentlyPressed = true;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;
        long long timeSinceRelease = currentTime - state.lastReleaseTime;

        // Check if we're in repeat/hold mode (holding key down)
        if (state.currentlyPressed) {
            // Key is still held - this is auto-repeat
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
            
            int threshold = state.inRepeatMode ? REPEAT_CHATTER_THRESHOLD_MS : CHATTER_THRESHOLD_MS;
            
            if (timeSincePress >= threshold) {
                state.lastPressTime = currentTime;
                return false; // Allow repeat
            }
            return true; // Too fast
        }

        // This is a new press after release
        state.currentlyPressed = true;
        state.currentPressStartTime = currentTime;

        // MACRO DETECTION:
        // If macro was detected in previous cycle, bypass filtering
        if (state.macroDetected) {
            state.lastPressTime = currentTime;
            return false;
        }

        // Not a macro (yet) - apply chatter filtering
        if (timeSincePress < CHATTER_THRESHOLD_MS) {
            // Fast press detected - likely chatter, block it
            state.blockedCount++;
            state.currentlyPressed = false;
            return true;
        }

        state.lastPressTime = currentTime;
        return false;
        
    } else {
        // Key release
        if (!state.currentlyPressed) {
            return false; // Already released
        }
        
        state.currentlyPressed = false;
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        
        return false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        
        if (ShouldBlockKey(vkCode, isKeyDown)) {
            return 1; // Block the key
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Create mutex to prevent multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Install keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
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
