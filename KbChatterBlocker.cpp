#include <windows.h>
#include <unordered_map>
#include <chrono>

// Configuration
const int CHATTER_THRESHOLD_MS = 85;         // Threshold for suspected chatter
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode
const int CHATTER_SUSPICION_COUNT = 2;       // How many fast presses before we start blocking
const int CHATTER_HISTORY_WINDOW_MS = 2000;  // Time window to track chatter history (2 seconds)

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
    int suspectedChatterCount = 0;      // How many times we've seen fast presses in the window
    long long chatterHistoryStartTime = 0; // When we started tracking this key's chatter
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool IsSoftwareInput(KBDLLHOOKSTRUCT* pKbdStruct) {
    // Check multiple indicators that this might be software-generated input
    
    // 1. LLKHF_INJECTED flag - set by SendInput and some injection methods
    if (pKbdStruct->flags & LLKHF_INJECTED) {
        return true;
    }
    
    // 2. dwExtraInfo - many macro tools set this to non-zero
    // Hardware keyboards typically leave it as 0
    if (pKbdStruct->dwExtraInfo != 0) {
        return true;
    }
    
    // 3. LLKHF_LOWER_IL_INJECTED - injected from lower integrity level process
    if (pKbdStruct->flags & LLKHF_LOWER_IL_INJECTED) {
        return true;
    }
    
    // 4. Check scanCode - some software sends 0 or unusual values
    // Real keyboards have proper scancodes
    if (pKbdStruct->scanCode == 0 && pKbdStruct->vkCode != VK_PAUSE) {
        return true;
    }
    
    return false;
}
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            state.chatterHistoryStartTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;
        long long timeSinceHistoryStart = currentTime - state.chatterHistoryStartTime;

        // Reset chatter history if enough time has passed
        if (timeSinceHistoryStart > CHATTER_HISTORY_WINDOW_MS) {
            state.suspectedChatterCount = 0;
            state.chatterHistoryStartTime = currentTime;
        }

        // Check if this press is in the suspected chatter range (0-85ms)
        if (timeSincePress < CHATTER_THRESHOLD_MS) {
            // Increment suspected chatter count
            state.suspectedChatterCount++;
            
            // SMART DETECTION:
            // One-off fast press is fine (could be intentional double-tap or macro)
            // But if we've seen this key repeatedly show fast presses, start blocking
            if (state.suspectedChatterCount < CHATTER_SUSPICION_COUNT) {
                // First fast press in the window - allow it (benefit of the doubt)
                state.lastPressTime = currentTime;
                return false;
            }
            
            // This key has repeatedly shown fast presses - likely chatter, block it
            state.blockedCount++;
            return true;
        }

        // Reset suspicion count for normal-speed presses
        if (timeSincePress >= CHATTER_THRESHOLD_MS) {
            state.suspectedChatterCount = 0;
            state.chatterHistoryStartTime = currentTime;
        }

        // Check if we're in repeat/hold mode
        int threshold;
        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else {
            threshold = CHATTER_THRESHOLD_MS;
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
        }

        // Block if within threshold
        if (timeSincePress < threshold) {
            state.blockedCount++;
            return true;
        }

        state.lastPressTime = currentTime;
        return false;
    } else {
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        return false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        // Check if this is software-generated input (macro)
        if (IsSoftwareInput(pKbdStruct)) {
            // This is from macro software - allow it through without filtering
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

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
