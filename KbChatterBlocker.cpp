#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <fstream>

// Configuration
const int INITIAL_CHATTER_THRESHOLD_MS = 81;  // Strict threshold for first repeat
const int REPEAT_CHATTER_THRESHOLD_MS = 15;   // Lenient threshold for subsequent repeats
const int REPEAT_TRANSITION_DELAY_MS = 200;   // Time to switch to repeat mode
const int MIN_RELEASE_DURATION_MS = 20;       // Minimum time key must be released to be intentional

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

void LogError(const char* message, DWORD errorCode = 0) {
    std::ofstream log("C:\\KbChatterBlocker_log.txt", std::ios::app);
    if (log.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << "[" << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "] ";
        log << message;
        if (errorCode != 0) {
            log << " (Error: " << errorCode << ")";
        }
        log << "\n";
        log.close();
    }
}

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        // Handle key press
        if (state.lastPressTime == 0) {
            // First press ever - always allow
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;
        long long timeSinceRelease = currentTime - state.lastReleaseTime;

        // Check if this is an intentional double-tap
        // If the key was released for a reasonable duration, it's intentional
        if (state.lastReleaseTime > state.lastPressTime && 
            timeSinceRelease >= MIN_RELEASE_DURATION_MS) {
            // This is an intentional double-tap, allow it
            state.lastPressTime = currentTime;
            state.inRepeatMode = false;  // Reset repeat mode
            return false;
        }

        // Determine which threshold to use
        int threshold;
        if (state.inRepeatMode) {
            threshold = REPEAT_CHATTER_THRESHOLD_MS;
        } else {
            threshold = INITIAL_CHATTER_THRESHOLD_MS;
            
            // Check if we should enter repeat mode
            if (timeSincePress > REPEAT_TRANSITION_DELAY_MS) {
                state.inRepeatMode = true;
            }
        }

        // Block if within threshold
        if (timeSincePress < threshold) {
            state.blockedCount++;
            return true;
        }

        // Update press time and allow
        state.lastPressTime = currentTime;
        return false;
    } else {
        // Handle key release
        state.lastReleaseTime = currentTime;
        state.inRepeatMode = false;
        return false;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKbdStruct = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vkCode = pKbdStruct->vkCode;

        // Allow ESC to exit
        if (vkCode == VK_ESCAPE && wParam == WM_KEYDOWN) {
            PostQuitMessage(0);
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
    LogError("Starting KbChatterBlocker...");

    // Create a named mutex so we can verify the app is running
    // Also prevents multiple instances
    HANDLE hMutex = CreateMutex(NULL, TRUE, L"KbChatterBlockerMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is already running
        LogError("Another instance already running");
        CloseHandle(hMutex);
        return 0;
    }
    LogError("Mutex created successfully");

    // Install keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    
    if (hHook == NULL) {
        DWORD error = GetLastError();
        LogError("Failed to install hook", error);
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
        return 1;
    }
    LogError("Hook installed successfully");

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    LogError("Shutting down...");
    UnhookWindowsHookEx(hHook);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    return 0;
}
