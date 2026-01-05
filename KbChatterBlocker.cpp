#include <windows.h>
#include <unordered_map>
#include <chrono>
#include <string>

// Configuration
const int CHATTER_THRESHOLD_MS = 80;         // Block presses in 60-80ms range
const int MACRO_SPEED_THRESHOLD_MS = 60;     // Anything faster than this = macro, allow through
const int REPEAT_CHATTER_THRESHOLD_MS = 30;  // Threshold when holding key
const int REPEAT_TRANSITION_DELAY_MS = 150;  // Time to enter repeat mode

struct KeyState {
    long long lastPressTime = 0;
    long long lastReleaseTime = 0;
    bool inRepeatMode = false;
    int blockedCount = 0;
};

std::unordered_map<DWORD, KeyState> keyStates;
HHOOK hHook = NULL;

long long GetCurrentTimeMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool IsGameWindow() {
    // Get the foreground window
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    // Get window class name
    wchar_t className[256];
    GetClassName(hwnd, className, 256);
    
    // Get window title
    wchar_t windowTitle[256];
    GetWindowText(hwnd, windowTitle, 256);
    
    std::wstring classStr(className);
    std::wstring titleStr(windowTitle);
    
    // Check for common game engine window classes
    if (classStr.find(L"UnrealWindow") != std::wstring::npos ||
        classStr.find(L"SDL_app") != std::wstring::npos ||
        classStr.find(L"GLFW") != std::wstring::npos ||
        classStr.find(L"UnityWndClass") != std::wstring::npos) {
        return true;
    }
    
    // Check if window is fullscreen (common for games)
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    bool isFullscreen = (windowRect.left == 0 && 
                        windowRect.top == 0 && 
                        (windowRect.right - windowRect.left) == screenWidth && 
                        (windowRect.bottom - windowRect.top) == screenHeight);
    
    // Check window style for borderless fullscreen
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    bool isBorderless = !(style & WS_CAPTION) && !(style & WS_THICKFRAME);
    
    // If window is fullscreen or borderless fullscreen, likely a game
    if ((isFullscreen || isBorderless) && 
        (windowRect.right - windowRect.left) >= 800 && 
        (windowRect.bottom - windowRect.top) >= 600) {
        return true;
    }
    
    return false;
}

bool ShouldBlockKey(DWORD vkCode, bool isKeyDown) {
    // If we're in a game, disable all filtering
    if (IsGameWindow()) {
        return false;
    }
    
    KeyState& state = keyStates[vkCode];
    long long currentTime = GetCurrentTimeMs();

    if (isKeyDown) {
        if (state.lastPressTime == 0) {
            state.lastPressTime = currentTime;
            return false;
        }

        long long timeSincePress = currentTime - state.lastPressTime;

        // MACRO DETECTION: If faster than 60ms, it's likely a macro - allow through
        if (timeSincePress < MACRO_SPEED_THRESHOLD_MS) {
            state.lastPressTime = currentTime;
            return false;
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

        // Block if within chatter range (60-85ms)
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
