#include <windows.h>
#include <unordered_map>
#include <chrono>

const int CHATTER_THRESHOLD_MS = 85;
const int REPEAT_CHATTER_THRESHOLD_MS = 30;
const int REPEAT_TRANSITION_DELAY_MS = 150;
const int MACRO_TIMING_TOLERANCE_MS = 15;

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

        if (state.currentlyPressed) {
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
            
            int threshold = state.inRepeatMode ? REPEAT_CHATTER_THRESHOLD_MS : CHATTER_THRESHOLD_MS;
            
            if (timeSincePress >= threshold) {
                state.lastPressTime = currentTime;
                return false;
            }
            return true;
        }

        state.currentlyPressed = true;

        if (state.lastReleaseTime > 0) {
            long long holdDuration = state.lastReleaseTime - state.lastPressTime;
            long long gapDuration = currentTime - state.lastReleaseTime;
            
            long long difference = (holdDuration > gapDuration) ? 
                                  (holdDuration - gapDuration) : 
                                  (gapDuration - holdDuration);
            
            if (difference <= MACRO_TIMING_TOLERANCE_MS) {
                state.lastPressTime = currentTime;
                state.currentPressStartTime = currentTime;
                return false;
            }
        }

        if (timeSincePress < CHATTER_THRESHOLD_MS) {
            state.blockedCount++;
            state.currentlyPressed = false;
            return true;
        }

        state.lastPressTime = currentTime;
        state.currentPressStartTime = currentTime;
        return false;
        
    } else {
        if (!state.currentlyPressed) {
            return false;
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
            return 1;
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
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
