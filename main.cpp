#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <unordered_set>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_TOGGLE 1002

#include "resource.h"

HHOOK hHook = NULL;
NOTIFYICONDATAW nid; // Используем Unicode версию
bool isBlocking = true;
std::unordered_set<std::string> blockedKeys;
std::unordered_set<int> pressedKeys;
HINSTANCE hInst;

// Функция для конвертации UTF-8 в UTF-16
std::wstring utf8ToUtf16(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), NULL, 0);
    std::wstring utf16(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8[0], (int)utf8.size(), &utf16[0], size_needed);
    return utf16;
}

int getKeyCode(const std::string& keyStr) {
    if (keyStr == "Ctrl") return VK_CONTROL;
    if (keyStr == "Alt") return VK_MENU;
    if (keyStr == "Shift") return VK_SHIFT;
    if (keyStr == "Win") return VK_LWIN;
    if (keyStr == "Tab") return VK_TAB;
    if (keyStr == "Delete") return VK_DELETE;
    if (keyStr == "C") return 'C';
    if (keyStr == "V") return 'V';
    return 0;
}

std::vector<int> parseKeyCombination(const std::string& combo) {
    std::vector<int> keys;
    std::istringstream ss(combo);
    std::string token;
    while (std::getline(ss, token, '+')) {
        int keyCode = getKeyCode(token);
        if (keyCode != 0) {
            keys.push_back(keyCode);
        }
    }
    return keys;
}

void loadBlockedKeys() {
    blockedKeys.clear();
    std::ifstream file("blocked_keys.ini");
    std::string line;
    bool inSection = false;

    while (std::getline(file, line)) {
        if (line == "[BlockedKeys]") {
            inSection = true;
            continue;
        }
        if (inSection && line.empty()) break;

        if (inSection) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                std::string keyCombo = line.substr(0, eqPos);
                blockedKeys.insert(keyCombo);
            }
        }
    }
}

bool isCombinationBlocked() {
    for (const auto& comboStr : blockedKeys) {
        auto keys = parseKeyCombination(comboStr);
        bool allPressed = true;
        for (int key : keys) {
            if (pressedKeys.find(key) == pressedKeys.end()) {
                allPressed = false;
                break;
            }
        }
        if (allPressed) return true;
    }
    return false;
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyInfo = (KBDLLHOOKSTRUCT*)lParam;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            pressedKeys.insert(pKeyInfo->vkCode);

            if (pressedKeys.find(VK_HOME) != pressedKeys.end() &&
                pressedKeys.find(VK_END) != pressedKeys.end()) {
                isBlocking = !isBlocking;
                pressedKeys.erase(VK_HOME);
                pressedKeys.erase(VK_END);
                return CallNextHookEx(hHook, nCode, wParam, lParam);
            }

            if (isBlocking && isCombinationBlocked()) {
                return 1;
            }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            pressedKeys.erase(pKeyInfo->vkCode);
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

void CreateTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW); // Unicode версия
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    
    // Используем Unicode строку
    wcscpy_s(nid.szTip, L"KeyBlocker");
    
    Shell_NotifyIconW(NIM_ADD, &nid); // Unicode версия
}

void UpdateTrayTip() {
    wcscpy_s(nid.szTip, isBlocking ? L"KeyBlocker (ВКЛ)" : L"KeyBlocker (ВЫКЛ)");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONDOWN) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                
                // Используем Unicode строки
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_TOGGLE, 
                    isBlocking ? L"Отключить блокировку" : L"Включить блокировку");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Выход");
                
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                PostMessage(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
            } else if (lParam == WM_LBUTTONDOWN) {
                isBlocking = !isBlocking;
                UpdateTrayTip();
            }
            break;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                PostQuitMessage(0);
            } else if (LOWORD(wParam) == ID_TRAY_TOGGLE) {
                isBlocking = !isBlocking;
                UpdateTrayTip();
            }
            break;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    hInst = hInstance;
    
    loadBlockedKeys();

    const wchar_t CLASS_NAME[] = L"KeyBlockerClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"KeyBlocker", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    CreateTrayIcon(hwnd);
    UpdateTrayTip();

    hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(NULL), 0);
    if (!hHook) {
        MessageBoxW(NULL, L"Не удалось установить хук!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(hHook);
    return 0;
}