#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace
{
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr UINT ID_TRAY_TOGGLE = 1002;
constexpr int ACTIVITY_INTERVAL_SECONDS = 60;

std::atomic_bool g_running{ true };
std::atomic_bool g_enabled{ true };
HHOOK g_keyboardHook = nullptr;
NOTIFYICONDATAW g_trayIcon{};

void SimulateActivity()
{
    // Move the mouse by one pixel and immediately move it back.
    // This generates user input without clicking anything.
    INPUT input[2]{};

    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_MOVE;
    input[0].mi.dx = 1;

    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    input[1].mi.dx = -1;

    SendInput(2, input, sizeof(INPUT));
}

void UpdateTrayTip()
{
    wcscpy_s(
        g_trayIcon.szTip,
        g_enabled ? L"Teams KeepAlive - ON" : L"Teams KeepAlive - OFF");

    Shell_NotifyIconW(NIM_MODIFY, &g_trayIcon);
}

void Toggle()
{
    g_enabled = !g_enabled;
    UpdateTrayTip();
}

LRESULT CALLBACK KeyboardHook(
    int code,
    WPARAM wParam,
    LPARAM lParam)
{
    if (code == HC_ACTION)
    {
        const auto* event = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (wParam == WM_KEYDOWN && event->vkCode == VK_F12)
        {
            Toggle();
            return 1; // Consume F12.
        }

        if (wParam == WM_KEYDOWN && event->vkCode == VK_ESCAPE)
        {
            g_running = false;
            PostQuitMessage(0);
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

void ShowTrayMenu(HWND hwnd)
{
    POINT point{};
    GetCursorPos(&point);

    HMENU menu = CreatePopupMenu();

    AppendMenuW(
        menu,
        MF_STRING | (g_enabled ? MF_CHECKED : 0),
        ID_TRAY_TOGGLE,
        L"Keep Teams Available");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON,
        point.x,
        point.y,
        0,
        hwnd,
        nullptr);

    DestroyMenu(menu);
}

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_TRAY:
        if (lParam == WM_RBUTTONUP)
            ShowTrayMenu(hwnd);
        else if (lParam == WM_LBUTTONDBLCLK)
            Toggle();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_TOGGLE:
            Toggle();
            return 0;

        case ID_TRAY_EXIT:
            g_running = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_trayIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ActivityThread()
{
    using namespace std::chrono_literals;

    while (g_running)
    {
        for (int i = 0; i < ACTIVITY_INTERVAL_SECONDS && g_running; ++i)
            std::this_thread::sleep_for(1s);

        if (g_running && g_enabled)
            SimulateActivity();
    }
}
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int)
{
    const wchar_t* className = L"TeamsKeepAliveWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        className,
        L"Teams KeepAlive",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        instance,
        nullptr);

    if (!hwnd)
        return 1;

    g_trayIcon.cbSize = sizeof(g_trayIcon);
    g_trayIcon.hWnd = hwnd;
    g_trayIcon.uID = 1;
    g_trayIcon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_trayIcon.uCallbackMessage = WM_TRAY;
    g_trayIcon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    wcscpy_s(g_trayIcon.szTip, L"Teams KeepAlive - ON");

    Shell_NotifyIconW(NIM_ADD, &g_trayIcon);

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardHook,
        nullptr,
        0);

    if (!g_keyboardHook)
    {
        DestroyWindow(hwnd);
        return 1;
    }

    std::thread activityThread(ActivityThread);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
        DispatchMessageW(&message);

    g_running = false;

    if (activityThread.joinable())
        activityThread.join();

    if (g_keyboardHook)
        UnhookWindowsHookEx(g_keyboardHook);

    return 0;
}
