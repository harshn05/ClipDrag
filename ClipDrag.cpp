#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <algorithm>

namespace {
constexpr int HOTKEY_ID = 1;
constexpr UINT HOTKEY_MODIFIERS = MOD_CONTROL | MOD_SHIFT;
constexpr UINT HOTKEY_KEY = 'X';
constexpr BYTE OVERLAY_ALPHA = 95;

using SetDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);

HINSTANCE g_instance = nullptr;
HWND g_overlay = nullptr;
HBITMAP g_desktopBitmap = nullptr;
int g_virtualX = 0;
int g_virtualY = 0;
int g_virtualWidth = 0;
int g_virtualHeight = 0;
bool g_dragging = false;
POINT g_start{ 0, 0 };
POINT g_current{ 0, 0 };

RECT SelectionRect()
{
    return RECT{
        (std::min)(g_start.x, g_current.x),
        (std::min)(g_start.y, g_current.y),
        (std::max)(g_start.x, g_current.x),
        (std::max)(g_start.y, g_current.y)
    };
}

void SetBestAvailableDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        FARPROC proc = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        SetDpiAwarenessContextFn setDpiContext = nullptr;
        static_assert(sizeof(setDpiContext) == sizeof(proc),
                      "Function pointer size differs from FARPROC");
        // GetProcAddress returns the Windows API function address. Copy the
        // pointer representation to avoid MinGW's incompatible-function-cast warning.
        if (proc)
            __builtin_memcpy(&setDpiContext, &proc, sizeof(setDpiContext));

        HANDLE perMonitorV2 = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4));
        if (setDpiContext && setDpiContext(perMonitorV2))
            return;
    }
    SetProcessDPIAware();
}

void ReleaseDesktopBitmap()
{
    if (g_desktopBitmap) {
        DeleteObject(g_desktopBitmap);
        g_desktopBitmap = nullptr;
    }
}

bool CaptureDesktop()
{
    g_virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (g_virtualWidth <= 0 || g_virtualHeight <= 0)
        return false;

    HDC screenDC = GetDC(nullptr);
    if (!screenDC)
        return false;

    HDC memoryDC = CreateCompatibleDC(screenDC);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, g_virtualWidth, g_virtualHeight);
    bool ok = false;

    if (memoryDC && bitmap) {
        HGDIOBJ old = SelectObject(memoryDC, bitmap);
        ok = BitBlt(memoryDC, 0, 0, g_virtualWidth, g_virtualHeight,
                    screenDC, g_virtualX, g_virtualY, SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memoryDC, old);
    }

    if (memoryDC)
        DeleteDC(memoryDC);
    ReleaseDC(nullptr, screenDC);

    if (!ok) {
        if (bitmap)
            DeleteObject(bitmap);
        return false;
    }

    ReleaseDesktopBitmap();
    g_desktopBitmap = bitmap;
    return true;
}

bool CopySelectionToClipboard(const RECT& r)
{
    const int width = r.right - r.left;
    const int height = r.bottom - r.top;
    if (!g_desktopBitmap || width < 2 || height < 2)
        return false;

    HDC screenDC = GetDC(nullptr);
    if (!screenDC)
        return false;

    HDC sourceDC = CreateCompatibleDC(screenDC);
    HDC targetDC = CreateCompatibleDC(screenDC);
    HBITMAP cropped = CreateCompatibleBitmap(screenDC, width, height);
    bool copied = false;

    if (sourceDC && targetDC && cropped) {
        HGDIOBJ oldSource = SelectObject(sourceDC, g_desktopBitmap);
        HGDIOBJ oldTarget = SelectObject(targetDC, cropped);
        copied = BitBlt(targetDC, 0, 0, width, height,
                        sourceDC, r.left, r.top, SRCCOPY) != FALSE;
        SelectObject(sourceDC, oldSource);
        SelectObject(targetDC, oldTarget);
    }

    if (sourceDC)
        DeleteDC(sourceDC);
    if (targetDC)
        DeleteDC(targetDC);
    ReleaseDC(nullptr, screenDC);

    if (!copied) {
        if (cropped)
            DeleteObject(cropped);
        return false;
    }

    bool clipboardOwnsBitmap = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(g_overlay)) {
            if (EmptyClipboard() && SetClipboardData(CF_BITMAP, cropped))
                clipboardOwnsBitmap = true;
            CloseClipboard();
            break;
        }
        Sleep(20);
    }

    if (!clipboardOwnsBitmap)
        DeleteObject(cropped);

    return clipboardOwnsBitmap;
}

void CloseOverlay()
{
    if (g_overlay)
        DestroyWindow(g_overlay);
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HGDIOBJ oldFont = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));

        if (!g_dragging) {
            const wchar_t* text = L"ClipDrag  |  Drag to select  |  Esc to cancel";
            TextOutW(dc, 18, 16, text, lstrlenW(text));
        } else {
            RECT r = SelectionRect();
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ oldPen = SelectObject(dc, pen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, r.left, r.top, r.right + 1, r.bottom + 1);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(pen);

            wchar_t dimensions[80]{};
            wsprintfW(dimensions, L"%d x %d", r.right - r.left, r.bottom - r.top);
            TextOutW(dc, r.left + 8, r.top + 8, dimensions, lstrlenW(dimensions));
        }

        SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
        g_start = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        g_current = g_start;
        g_dragging = true;
        SetCapture(hwnd);
        SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragging) {
            g_current = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            InvalidateRect(hwnd, nullptr, TRUE);
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragging) {
            g_current = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            g_dragging = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();

            RECT selection = SelectionRect();
            bool valid = (selection.right - selection.left >= 2) &&
                         (selection.bottom - selection.top >= 2);
            bool copied = valid && CopySelectionToClipboard(selection);
            CloseOverlay();

            if (!copied && valid) {
                MessageBoxW(nullptr, L"Could not copy the selection to the clipboard.",
                            L"ClipDrag", MB_OK | MB_ICONWARNING | MB_TOPMOST);
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (g_dragging && GetCapture() == hwnd)
                ReleaseCapture();
            g_dragging = false;
            CloseOverlay();
        }
        return 0;

    case WM_DESTROY:
        if (GetCapture() == hwnd)
            ReleaseCapture();
        g_overlay = nullptr;
        g_dragging = false;
        ReleaseDesktopBitmap();
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool StartCapture()
{
    if (g_overlay)
        return true;

    if (!CaptureDesktop()) {
        MessageBoxW(nullptr, L"Could not capture the desktop.", L"ClipDrag",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }

    g_dragging = false;
    g_start = { 0, 0 };
    g_current = { 0, 0 };

    g_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"ClipDragOverlay", L"ClipDrag", WS_POPUP,
        g_virtualX, g_virtualY, g_virtualWidth, g_virtualHeight,
        nullptr, nullptr, g_instance, nullptr);

    if (!g_overlay) {
        ReleaseDesktopBitmap();
        MessageBoxW(nullptr, L"Could not create the selection overlay.", L"ClipDrag",
                    MB_OK | MB_ICONERROR | MB_TOPMOST);
        return false;
    }

    SetLayeredWindowAttributes(g_overlay, 0, OVERLAY_ALPHA, LWA_ALPHA);
    ShowWindow(g_overlay, SW_SHOW);
    UpdateWindow(g_overlay);
    SetForegroundWindow(g_overlay);
    SetFocus(g_overlay);
    SetCursor(LoadCursorW(nullptr, IDC_CROSS));
    return true;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    g_instance = instance;
    SetBestAvailableDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = OverlayProc;
    wc.lpszClassName = L"ClipDragOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Could not register the overlay window class.",
                    L"ClipDrag", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!RegisterHotKey(nullptr, HOTKEY_ID, HOTKEY_MODIFIERS, HOTKEY_KEY)) {
        MessageBoxW(nullptr,
            L"Could not register Ctrl+Shift+X. Another app may already use this hotkey.",
            L"ClipDrag", MB_OK | MB_ICONWARNING);
        UnregisterClassW(L"ClipDragOverlay", instance);
        return 1;
    }

    MSG msg{};
    while (true) {
        BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0)
            break;

        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            StartCapture();
            continue;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(nullptr, HOTKEY_ID);
    if (g_overlay)
        DestroyWindow(g_overlay);
    ReleaseDesktopBitmap();
    UnregisterClassW(L"ClipDragOverlay", instance);
    return static_cast<int>(msg.wParam);
}
