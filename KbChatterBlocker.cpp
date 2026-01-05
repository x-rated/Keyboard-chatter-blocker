#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <vector>

// Configuration
const int CHATTER_THRESHOLD_MS = 85;         // Block everything faster than this
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode
const int PATTERN_HISTORY_SIZE = 4;          // How many recent intervals to analyze
const int PATTERN_CONSISTENCY_THRESHOLD = 15; // Max deviation (ms) for macro detection

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
    std::vector<long long> recentIntervals; // Track recent press-to-press intervals
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool IsMacroPattern(const std::vector<long long>& intervals) {
    if (intervals.size() < 3) {
        return false; // Not enough data
    }
    
    // Calculate average interval
    long long sum = 0;
    for (long long interval : intervals) {
        sum += interval;
    }
    long long avg = sum / intervals.size();
    
    // Check consistency - macros have very consistent timing
    // Chatter is more erratic
    for (long long interval : intervals) {
        long long deviation = (interval > avg) ? (interval - avg) : (avg - interval);
        if (deviation > PATTERN_CONSISTENCY_THRESHOLD) {
            return false; // Too much variation, likely chatter
        }
    }
    
    // All intervals are very consistent - likely a macro
    return true;
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;

        // Track this interval for pattern analysis
        state.recentIntervals.push_back(timeSincePress);
        if (state.recentIntervals.size() > PATTERN_HISTORY_SIZE) {
            state.recentIntervals.erase(state.recentIntervals.begin());
        }

        // Check if this is a fast press that might be chatter OR macro
        if (timeSincePress < CHATTER_THRESHOLD_MS) {
            // Analyze pattern - if it's consistent, it's likely a macro
            if (IsMacroPattern(state.recentIntervals)) {
                // Consistent pattern detected - allow (macro)
                state.lastPressTime = currentTime;
                return false;
            }
            
            // Erratic pattern - block (chatter)
            state.blockedCount++;
            return true;
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
        
        // Clear interval history on key release
        // This helps distinguish single chatters from macro sequences
        if (currentTime - state.lastPressTime > 100) {
            state.recentIntervals.clear();
        }
        
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
