#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <vector>

// Configuration
const int CHATTER_THRESHOLD_MS = 85;         // Fast press threshold
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode
const int PATTERN_ANALYSIS_WINDOW = 5;       // Analyze last 5 events
const int CLEAN_RELEASE_THRESHOLD_MS = 25;   // Minimum time key should be held for clean release

struct PressReleaseEvent {
    long long pressTime;
    long long releaseTime;
    long long pressToPressInterval;  // Time since last press
    long long holdDuration;          // How long key was held
};

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
    bool currentlyPressed = false;
    std::vector<PressReleaseEvent> recentEvents;  // Track recent press/release patterns
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool AnalyzePattern(const std::vector<PressReleaseEvent>& events) {
    if (events.size() < 2) {
        return true; // Not enough data, allow it
    }
    
    // Analyze the pattern:
    // CHATTER characteristics:
    //   - Erratic press-to-press intervals
    //   - Very short or inconsistent hold durations
    //   - Often has quick press without proper release in between
    //
    // MACRO characteristics:
    //   - Consistent press-to-press intervals
    //   - Clean press-release pairs (proper hold duration)
    //   - Predictable pattern
    
    int cleanPressReleasePairs = 0;
    int erraticEvents = 0;
    
    for (size_t i = 1; i < events.size(); i++) {
        const auto& event = events[i];
        
        // Check if this was a clean press-release pair
        if (event.holdDuration >= CLEAN_RELEASE_THRESHOLD_MS) {
            cleanPressReleasePairs++;
        }
        
        // Check for erratic behavior (very short hold, indicating bounce)
        if (event.holdDuration < 15 && event.pressToPressInterval < 100) {
            erraticEvents++;
        }
    }
    
    // If we see mostly clean press-release pairs, it's likely a macro
    if (cleanPressReleasePairs >= (int)events.size() - 1) {
        return true; // Allow - likely macro with clean patterns
    }
    
    // If we see erratic events, likely chatter
    if (erraticEvents >= 2) {
        return false; // Block - likely chatter
    }
    
    // Default: allow (benefit of the doubt)
    return true;
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        // Check if this is a repeat event (holding key)
        // Windows sends repeated keydown events when holding
        if (state.currentlyPressed) {
            // This is a key repeat (holding), not a new press
            long long timeSincePress = currentTime - state.lastPressTime;
            
            // Enter repeat mode after threshold
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
            
            if (state.inRepeatMode) {
                // In repeat mode, use lenient threshold
                if (timeSincePress >= REPEAT_CHATTER_THRESHOLD_MS) {
                    state.lastPressTime = currentTime;
                    return false; // Allow repeat
                } else {
                    return true; // Too fast even for repeat
                }
            } else {
                // Not in repeat mode yet, use normal threshold
                if (timeSincePress >= CHATTER_THRESHOLD_MS) {
                    state.lastPressTime = currentTime;
                    return false;
                }
                return true;
            }
        }
        
        // This is a new press (key was not currently pressed)
        state.currentlyPressed = true;
        
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;

        // Check if this is a fast press that might be chatter
        if (timeSincePress < CHATTER_THRESHOLD_MS) {
            // Analyze recent press/release patterns
            bool shouldAllow = AnalyzePattern(state.recentEvents);
            
            if (!shouldAllow) {
                // Pattern analysis suggests this is chatter
                state.blockedCount++;
                state.currentlyPressed = false; // Reset since we're blocking
                return true;
            }
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

        // Block if within threshold and not in repeat mode
        if (timeSincePress < threshold && !state.inRepeatMode) {
            // Do another pattern check
            bool shouldAllow = AnalyzePattern(state.recentEvents);
            if (!shouldAllow) {
                state.blockedCount++;
                state.currentlyPressed = false;
                return true;
            }
        }

        state.lastPressTime = currentTime;
        return false;
        
    } else {
        // Key release
        if (!state.currentlyPressed) {
            return false; // Already released or was blocked
        }
        
        state.currentlyPressed = false;
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        
        // Record this press-release event
        PressReleaseEvent event;
        event.pressTime = state.lastPressTime;
        event.releaseTime = currentTime;
        event.holdDuration = currentTime - state.lastPressTime;
        
        if (!state.recentEvents.empty()) {
            event.pressToPressInterval = state.lastPressTime - state.recentEvents.back().pressTime;
        } else {
            event.pressToPressInterval = 0;
        }
        
        state.recentEvents.push_back(event);
        
        // Keep only recent events
        if (state.recentEvents.size() > PATTERN_ANALYSIS_WINDOW) {
            state.recentEvents.erase(state.recentEvents.begin());
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
