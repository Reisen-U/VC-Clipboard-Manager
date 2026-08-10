#include <windows.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <algorithm>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <mmsystem.h>

using namespace Gdiplus;

// --- 常量与全局变量区 ---
#define WM_TRAYICON (WM_USER + 1)
#define WM_IMAGE_READY (WM_USER + 2)
#define ID_TRAY_OPEN_DIR 2001
#define ID_TRAY_CLEAR    2002
#define ID_TRAY_EXIT     2003
#define ID_TRAY_SETTINGS 2005
#define ID_TRAY_HOTKEY_1 2011 // Ctrl+Shift+V
#define ID_TRAY_HOTKEY_2 2012 // Alt+V
#define ID_TRAY_HOTKEY_3 2013 // Win+V
#define ID_SETTINGS_MAXCOUNT 3001
#define ID_SETTINGS_RETENTION 3002
#define ID_SETTINGS_WRAP 3003
#define ID_SETTINGS_HOTKEY_1 3004
#define ID_SETTINGS_HOTKEY_2 3005
#define ID_SETTINGS_HOTKEY_3 3006
#define ID_SETTINGS_OPEN_DIR 3007
#define ID_SETTINGS_CLEAR_CACHE 3008
#define ID_SETTINGS_SAVE 3009
#define ID_SETTINGS_SOUND_ENABLED 3011
#define ID_SETTINGS_SOUND_PATH 3012
#define ID_SETTINGS_SOUND_BROWSE 3013
#define ID_SETTINGS_CLOSE 3014
#define ID_SETTINGS_FONT_SIZE 3015
#define ID_SETTINGS_LINE_COUNT 3016
#define ID_SETTINGS_AUTOSTART 3017
#define MAX_HISTORY_COUNT 30

HWND g_hwndMain = NULL;
HWND g_hwndList = NULL;
HWND g_hwndSettings = NULL;
HWND g_hwndSettingsMaxCount = NULL;
HWND g_hwndSettingsRetention = NULL;
HWND g_hwndSettingsWrap = NULL;
HWND g_hwndSettingsHotkey1 = NULL;
HWND g_hwndSettingsHotkey2 = NULL;
HWND g_hwndSettingsHotkey3 = NULL;
HWND g_hwndSettingsFontSize = NULL;
HWND g_hwndSettingsLineCount = NULL;
HWND g_hwndSettingsSoundEnabled = NULL;
HWND g_hwndSettingsSoundPath = NULL;
HWND g_hwndSettingsAutoStart = NULL;
WNDPROC g_oldListProc = NULL;
HFONT g_hFontMain = NULL;
HFONT g_hFontTime = NULL;
HFONT g_hFontSettings = NULL;
NOTIFYICONDATAW g_nid = {};
bool g_isPasting = false;
bool g_ignoreNextClipboardUpdate = false;
bool g_settingsClassRegistered = false;
int g_historyMaxCount = MAX_HISTORY_COUNT;
int g_historyRetentionDays = 30;
int g_fontSize = 11;
int g_itemLines = 5;
bool g_soundEnabled = true;
bool g_hoverSettingsBtn = false;
WCHAR g_soundFilePath[MAX_PATH] = {};
std::wstring g_historyDirPath; 
ULONG_PTR gdiplusToken;
UINT g_taskbarRestartMsg = 0;
HANDLE g_imageWorkersDone = NULL;
volatile LONG g_imageWorkerCount = 0;
ULONGLONG g_nextClipItemId = 0;

HDC g_listBufferDC = NULL;
HBITMAP g_listBufferBitmap = NULL;
HBITMAP g_listBufferOldBitmap = NULL;
int g_listBufferWidth = 0;
int g_listBufferHeight = 0;
HBRUSH g_listBackgroundBrush = NULL;
HBRUSH g_cardBrush = NULL;
HBRUSH g_selectedCardBrush = NULL;
HPEN g_cardPen = NULL;
HPEN g_selectedCardPen = NULL;

// 用户自定义配置
bool g_wordWrap = true;
int g_hotkeyMode = 1;     // 1: Ctrl+Shift+V  |  2: Alt+V  |  3: Win+V

// --- 平滑滚动状态 ---
int g_scrollY = 0;            // 当前滚动像素偏移（顶部裁掉的像素数）
int g_scrollTargetY = 0;      // 目标滚动偏移，滚轮累加到这里，动画向其逼近
int g_selIndex = -1;          // 当前选中项，-1 表示无
UINT_PTR g_scrollTimer = 0;   // 缓动动画定时器 id
const UINT_PTR SCROLL_TIMER_ID = 0xC1A9;
const int SCROLL_FRAME_MS = 15;       // 约 66fps
const float SCROLL_EASING = 0.22f;    // 每帧逼近目标的比例，越大越快
const int SCROLL_WHEEL_STEP = 90;     // 滚轮每格滚动的像素量
const int THUMBNAIL_MAX_WIDTH = 220;
const int THUMBNAIL_MAX_HEIGHT = 160;

enum ClipType { TYPE_TEXT, TYPE_IMAGE, TYPE_FILE_LIST };

struct ClipItem {
    ULONGLONG id = 0;
    ClipType type;
    std::wstring text;      
    std::wstring filePath;  
    std::wstring timestamp;
    HBITMAP hThumbnail = NULL; // 列表只常驻小图，原图在粘贴时从磁盘按需加载
    bool imagePending = false;
};

struct ImageTask {
    HWND hwnd;
    ULONGLONG itemId;
    std::wstring filePath;
    HBITMAP hOriginal;
};

struct ImageResult {
    ULONGLONG itemId;
    std::wstring filePath;
    HBITMAP hThumbnail = NULL;
    bool success = false;
};

std::vector<ClipItem> g_history;

// --- 函数声明 ---
void InitEnvironment();
void LoadSettings();
void SaveSettings();
void RegisterGlobalHotkey(HWND hwnd);
void AddItemToHistory(ClipType type, const std::wstring& data, HBITMAP hBmp = NULL);
void EnforceHistoryLimit();
void EnforceHistoryRetention();
void DeleteHistoryAtIndex(int index);
void ClearAllHistory();
void OpenSettingsWindow(HWND hwnd);
int GetListItemHeight(HWND hwnd);
void UpdateMainFont(HWND hwnd);
LRESULT CALLBACK ClipListProc(HWND, UINT, WPARAM, LPARAM);
void DrawCard(HDC hdc, int index, RECT rcItem, bool isSelected);
void PasteHistoryItem(int index);
int GetClipListMaxScroll();
void ClampScroll();
void UpdateScrollBar();
void EnsureIndexVisible(int index);
LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
bool SaveBitmapToPNG(HBITMAP hBitmap, const std::wstring& filePath);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
std::wstring GetCurrentTimeStr();
std::wstring GetFileSafeTimeStr();
std::vector<std::wstring> GetClipboardFiles(); 
std::wstring GetClipboardText();
HBITMAP GetClipboardImage();
bool SetClipboardText(const std::wstring& text);
bool SetClipboardImage(HBITMAP hBmp);
bool SetClipboardFiles(const std::wstring& pathsData); 
void SimulatePaste();
void SetupTrayIcon(HWND hwnd);
void ShowManagerWindow(HWND hwnd);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void LoadHistoryFromDisk();
bool LoadTextFile(const std::wstring& filePath, std::wstring& outText);
HBITMAP LoadBitmapFromPNG(const std::wstring& filePath);
HBITMAP CreateThumbnail(Image& source);
HBITMAP LoadThumbnailFromPNG(const std::wstring& filePath);
DWORD WINAPI ImageWorkerProc(LPVOID param);
void CleanupImageResult(ImageResult* result, bool deleteFile);
bool EnsureListBackBuffer(HDC referenceDC, int width, int height);
void DestroyListDrawingResources();
std::wstring GetFileTimestamp(const std::wstring& filePath);
bool IsFileOlderThan(const std::wstring& filePath, int days);
void UpdateSettingsControls();
void ApplySettingsFromDialog();
void ClearCacheFiles();
bool IsAutoStartEnabled();
bool SetAutoStartEnabled(bool enabled);

// 在创建任何窗口前启用 Per-Monitor V2 DPI 感知。
// 如果系统不支持该 API，则回退到旧版 DPI aware 模式，避免 Windows
// 对整个窗口做位图缩放（这是非整数缩放下文字模糊的主要原因）。
void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetProcessDpiAwarenessContextProc = BOOL (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setContext = user32
        ? reinterpret_cast<SetProcessDpiAwarenessContextProc>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
        : nullptr;

    if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        return;
    }

    // Windows 8.1/更早系统或不支持 Per-Monitor V2 时的安全回退。
    SetProcessDPIAware();
}

// DPI 感知进程中的所有界面尺寸都以 96 DPI 设计稿为基准统一缩放。
// 动态获取 GetDpiForWindow，兼容没有该 API 的旧版 Windows。
UINT GetWindowDpi(HWND hwnd) {
    using GetDpiForWindowProc = UINT (WINAPI*)(HWND);
    static GetDpiForWindowProc getDpiForWindow = []() -> GetDpiForWindowProc {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32
            ? reinterpret_cast<GetDpiForWindowProc>(GetProcAddress(user32, "GetDpiForWindow"))
            : nullptr;
    }();
    if (getDpiForWindow && hwnd) {
        UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0) return dpi;
    }

    HDC hdc = GetDC(hwnd);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi > 0 ? (UINT)dpi : 96;
}

UINT GetSystemDpi() {
    HDC hdc = GetDC(NULL);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(NULL, hdc);
    return dpi > 0 ? (UINT)dpi : 96;
}

int DpiScale(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

// --- 核心入口 ---
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow) {
    // 必须在调用 User32 创建窗口之前设置，否则当前进程会被 DPI 虚拟化。
    EnableDpiAwareness();

    // 单实例检测：已有实例在运行则直接退出，避免重复托盘图标与热键冲突
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ClipManagerSingletonMutex");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    g_imageWorkersDone = CreateEventW(NULL, TRUE, TRUE, NULL);

    InitEnvironment(); 
    LoadSettings(); // 加载用户设置的换行与快捷键

    const wchar_t CLASS_NAME[] = L"ClipManagerClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    // 隐藏主窗口原生阴影，使用现代深灰色边框
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = CreateSolidBrush(RGB(243, 244, 246));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    if (!RegisterClassW(&wc)) return 0;

    // 注册自绘平滑滚动列表的窗口类
    WNDCLASSW wcList = {};
    wcList.style = CS_DBLCLKS; // 必须开启，否则系统不会派发 WM_LBUTTONDBLCLK，双击粘贴失效
    wcList.lpfnWndProc = ClipListProc;
    wcList.hInstance = hInstance;
    wcList.lpszClassName = L"ClipListClass";
    wcList.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcList.hbrBackground = NULL; // 全自绘，避免擦背景闪烁
    RegisterClassW(&wcList);

    UINT systemDpi = GetSystemDpi();
    g_hwndMain = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, CLASS_NAME, L"系统级剪贴板神器",
        WS_POPUP | WS_BORDER, 0, 0, DpiScale(400, systemDpi), DpiScale(600, systemDpi),
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_imageWorkersDone) CloseHandle(g_imageWorkersDone);
    GdiplusShutdown(gdiplusToken);
    return 0;
}

// --- 窗口消息处理 ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            UpdateMainFont(hwnd);
            UINT dpi = GetWindowDpi(hwnd);
            int headerHeight = DpiScale(35, dpi);

            g_hwndList = CreateWindowExW(0, L"ClipListClass", NULL,
                WS_CHILD | WS_VISIBLE | WS_VSCROLL,
                0, headerHeight, DpiScale(400, dpi), DpiScale(565, dpi), hwnd,
                (HMENU)1001, (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), NULL);
            UpdateMainFont(hwnd);

            RegisterGlobalHotkey(hwnd); // 注册你配置的快捷键！
            AddClipboardFormatListener(hwnd);
            SetupTrayIcon(hwnd);
            LoadHistoryFromDisk();
            g_taskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");
            return 0;
        }

        case WM_SIZE: {
            int headerHeight = DpiScale(35, GetWindowDpi(hwnd));
            MoveWindow(g_hwndList, 0, headerHeight, LOWORD(lParam),
                std::max(0, (int)HIWORD(lParam) - headerHeight), TRUE);
            return 0;
        }

        case WM_DPICHANGED: {
            // 窗口跨显示器移动时重新生成对应 DPI 的字体，并采用系统建议的窗口尺寸。
            UpdateMainFont(hwnd);
            RECT* suggested = reinterpret_cast<RECT*>(lParam);
            if (suggested) {
                SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                    suggested->right - suggested->left, suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (g_hwndList) {
                UpdateScrollBar();
                InvalidateRect(g_hwndList, NULL, FALSE);
            }
            return 0;
        }

        case WM_PAINT: { // 拖动标题栏自绘
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            UINT dpi = GetWindowDpi(hwnd);
            int headerHeight = DpiScale(35, dpi);
            RECT rcHeader = {0, 0, rcClient.right, headerHeight};
            HBRUSH br = CreateSolidBrush(RGB(55, 65, 81)); FillRect(hdc, &rcHeader, br); DeleteObject(br);
            SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(255, 255, 255)); SelectObject(hdc, g_hFontMain);
            rcHeader.left += DpiScale(15, dpi);
            DrawTextW(hdc, L"📋 剪贴板历史", -1, &rcHeader, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            // 右上角设置按钮（悬停高亮）
            RECT rcBtn = {rcClient.right - DpiScale(40, dpi), DpiScale(5, dpi),
                rcClient.right - DpiScale(10, dpi), DpiScale(30, dpi)};
            HBRUSH brBtn = CreateSolidBrush(g_hoverSettingsBtn ? RGB(100, 110, 125) : RGB(75, 85, 99));
            FillRect(hdc, &rcBtn, brBtn); DeleteObject(brBtn);
            SetTextColor(hdc, g_hoverSettingsBtn ? RGB(255, 255, 255) : RGB(220, 220, 230));
            DrawTextW(hdc, L"🔧", -1, &rcBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps); return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam);
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            UINT dpi = GetWindowDpi(hwnd);
            RECT rcBtn = {rcClient.right - DpiScale(40, dpi), DpiScale(5, dpi),
                rcClient.right - DpiScale(10, dpi), DpiScale(30, dpi)};
            if (x >= rcBtn.left && x <= rcBtn.right && y >= rcBtn.top && y <= rcBtn.bottom) {
                OpenSettingsWindow(hwnd);
                return 0;
            }
            break;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam);
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            UINT dpi = GetWindowDpi(hwnd);
            RECT rcBtn = {rcClient.right - DpiScale(40, dpi), DpiScale(5, dpi),
                rcClient.right - DpiScale(10, dpi), DpiScale(30, dpi)};
            bool inBtn = (x >= rcBtn.left && x <= rcBtn.right && y >= rcBtn.top && y <= rcBtn.bottom);
            if (inBtn != g_hoverSettingsBtn) {
                g_hoverSettingsBtn = inBtn;
                RECT rcHeader = {0, 0, rcClient.right, DpiScale(35, dpi)};
                InvalidateRect(hwnd, &rcHeader, FALSE);
                if (inBtn) {
                    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                    TrackMouseEvent(&tme);
                }
            }
            break;
        }

        case WM_MOUSELEAVE: {
            if (g_hoverSettingsBtn) {
                g_hoverSettingsBtn = false;
                RECT rcClient; GetClientRect(hwnd, &rcClient);
                RECT rcHeader = {0, 0, rcClient.right, DpiScale(35, GetWindowDpi(hwnd))};
                InvalidateRect(hwnd, &rcHeader, FALSE);
            }
            break;
        }

        case WM_NCHITTEST: { // 让系统误以为深色条是原生标题栏
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            // 设置按钮区域不参与拖动
            RECT rcClient; GetClientRect(hwnd, &rcClient);
            UINT dpi = GetWindowDpi(hwnd);
            if (pt.x >= rcClient.right - DpiScale(40, dpi) &&
                pt.x <= rcClient.right - DpiScale(10, dpi) &&
                pt.y >= DpiScale(5, dpi) && pt.y <= DpiScale(30, dpi))
                return HTCLIENT;
            if (pt.y < DpiScale(35, dpi)) return HTCAPTION;
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        case WM_TRAYICON: {
            if (lParam == WM_LBUTTONUP) {
                ShowManagerWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"设置");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_DIR, L"打开缓存目录 (AppData)");
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_CLEAR, L"一键清空所有缓存");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出程序");
                
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
            }
            return 0;
        }

        case WM_COMMAND: {
            // 处理设定的点击
            if (LOWORD(wParam) == ID_TRAY_OPEN_DIR) ShellExecuteW(NULL, L"open", g_historyDirPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
            else if (LOWORD(wParam) == ID_TRAY_CLEAR) { ClearAllHistory(); }
            else if (LOWORD(wParam) == ID_TRAY_SETTINGS) { OpenSettingsWindow(hwnd); }
            else if (LOWORD(wParam) == ID_TRAY_EXIT) DestroyWindow(hwnd);
            return 0;
        }

        case WM_CLIPBOARDUPDATE: {
            if (g_ignoreNextClipboardUpdate) {
                g_ignoreNextClipboardUpdate = false;
                return 0;
            }
            std::vector<std::wstring> files = GetClipboardFiles();
            if (!files.empty()) {
                std::wstring joinedStr = L""; for (auto& f : files) joinedStr += f + L"\n";
                // 检查是否已在历史中，避免粘贴时产生重复
                bool dup = false;
                for (auto& item : g_history) {
                    if (item.type == TYPE_FILE_LIST && item.text == joinedStr) { dup = true; break; }
                }
                if (!dup) AddItemToHistory(TYPE_FILE_LIST, joinedStr);
                return 0;
            }
            HBITMAP hBmp = GetClipboardImage();
            if (hBmp != NULL) { AddItemToHistory(TYPE_IMAGE, L"", hBmp); return 0; }
            std::wstring text = GetClipboardText();
            if (!text.empty()) {
                // 检查是否已在历史中，避免粘贴时产生重复
                bool dup = false;
                for (auto& item : g_history) {
                    if (item.type == TYPE_TEXT && item.text == text) { dup = true; break; }
                }
                if (!dup) AddItemToHistory(TYPE_TEXT, text, NULL);
            }
            return 0;
        }

        case WM_IMAGE_READY: {
            ImageResult* result = reinterpret_cast<ImageResult*>(lParam);
            if (!result) return 0;
            auto it = std::find_if(g_history.begin(), g_history.end(), [result](const ClipItem& item) {
                return item.id == result->itemId;
            });
            if (it == g_history.end()) {
                CleanupImageResult(result, true);
                return 0;
            }
            if (!result->success) {
                int index = (int)std::distance(g_history.begin(), it);
                CleanupImageResult(result, false);
                DeleteHistoryAtIndex(index);
                return 0;
            }
            it->hThumbnail = result->hThumbnail;
            it->imagePending = false;
            result->hThumbnail = NULL;
            CleanupImageResult(result, false);
            if (g_hwndList) InvalidateRect(g_hwndList, NULL, FALSE);
            return 0;
        }

        case WM_HOTKEY: {
            if (wParam == 1) {
                if (IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_HIDE);
                else ShowManagerWindow(hwnd);
            }
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        default: {
            if (msg == g_taskbarRestartMsg && g_taskbarRestartMsg != 0) {
                Shell_NotifyIconW(NIM_ADD, &g_nid);
                return 0;
            }
            break;
        }

        case WM_DESTROY: {
            RemoveClipboardFormatListener(hwnd); UnregisterHotKey(hwnd, 1);
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_imageWorkersDone) WaitForSingleObject(g_imageWorkersDone, INFINITE);
            MSG pendingMessage;
            while (PeekMessageW(&pendingMessage, hwnd, WM_IMAGE_READY, WM_IMAGE_READY, PM_REMOVE)) {
                CleanupImageResult(reinterpret_cast<ImageResult*>(pendingMessage.lParam), false);
            }
            for(auto& item : g_history) if(item.hThumbnail) DeleteObject(item.hThumbnail); 
            if (g_hFontMain) DeleteObject(g_hFontMain); if (g_hFontTime) DeleteObject(g_hFontTime);
            PostQuitMessage(0); return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- Ini 文件读写存大体系 ---

void InitEnvironment() {
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        g_historyDirPath = std::wstring(localAppData) + L"\\ClipManager";
        CreateDirectoryW(g_historyDirPath.c_str(), NULL);
    }
}

void LoadSettings() {
    std::wstring iniPath = g_historyDirPath + L"\\config.ini";
    g_wordWrap = GetPrivateProfileIntW(L"Settings", L"WordWrap", 1, iniPath.c_str()) != 0;
    g_hotkeyMode = GetPrivateProfileIntW(L"Settings", L"Hotkey", 1, iniPath.c_str());
    g_historyMaxCount = GetPrivateProfileIntW(L"Settings", L"MaxCount", MAX_HISTORY_COUNT, iniPath.c_str());
    if (g_historyMaxCount < 1) g_historyMaxCount = 1;
    if (g_historyMaxCount > 100) g_historyMaxCount = 100;
    g_historyRetentionDays = GetPrivateProfileIntW(L"Settings", L"RetentionDays", 30, iniPath.c_str());
    if (g_historyRetentionDays < 1) g_historyRetentionDays = 1;
    if (g_historyRetentionDays > 365) g_historyRetentionDays = 365;
    g_fontSize = GetPrivateProfileIntW(L"Settings", L"FontSize", 16, iniPath.c_str());
    if (g_fontSize < 10) g_fontSize = 10;
    if (g_fontSize > 28) g_fontSize = 28;
    g_itemLines = GetPrivateProfileIntW(L"Settings", L"ItemLines", 2, iniPath.c_str());
    if (g_itemLines < 1) g_itemLines = 1;
    if (g_itemLines > 6) g_itemLines = 6;
    g_soundEnabled = GetPrivateProfileIntW(L"Settings", L"SoundEnabled", 1, iniPath.c_str()) != 0;
    GetPrivateProfileStringW(L"Settings", L"SoundFilePath", L"", g_soundFilePath, MAX_PATH, iniPath.c_str());
}

void SaveSettings() {
    std::wstring iniPath = g_historyDirPath + L"\\config.ini";
    WritePrivateProfileStringW(L"Settings", L"WordWrap", g_wordWrap ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"Hotkey", std::to_wstring(g_hotkeyMode).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"MaxCount", std::to_wstring(g_historyMaxCount).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"RetentionDays", std::to_wstring(g_historyRetentionDays).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"FontSize", std::to_wstring(g_fontSize).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"ItemLines", std::to_wstring(g_itemLines).c_str(), iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"SoundEnabled", g_soundEnabled ? L"1" : L"0", iniPath.c_str());
    WritePrivateProfileStringW(L"Settings", L"SoundFilePath", g_soundFilePath, iniPath.c_str());
}

namespace {
const wchar_t* kAutoStartSubkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kAutoStartValueName = L"VCClipboardManager";
}

bool IsAutoStartEnabled() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAutoStartSubkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD byteCount = 0;
    LONG result = RegQueryValueExW(hKey, kAutoStartValueName, NULL, &type, NULL, &byteCount);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && byteCount > sizeof(wchar_t);
}

bool SetAutoStartEnabled(bool enabled) {
    HKEY hKey = NULL;
    LONG result = ERROR_SUCCESS;

    if (enabled) {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath));
        if (length == 0 || length >= ARRAYSIZE(modulePath)) return false;

        result = RegCreateKeyExW(HKEY_CURRENT_USER, kAutoStartSubkey, 0, NULL, 0,
            KEY_SET_VALUE, NULL, &hKey, NULL);
        if (result == ERROR_SUCCESS) {
            std::wstring commandLine = L"\"" + std::wstring(modulePath, length) + L"\"";
            result = RegSetValueExW(hKey, kAutoStartValueName, 0, REG_SZ,
                reinterpret_cast<const BYTE*>(commandLine.c_str()),
                static_cast<DWORD>((commandLine.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        result = RegOpenKeyExW(HKEY_CURRENT_USER, kAutoStartSubkey, 0, KEY_SET_VALUE, &hKey);
        if (result == ERROR_FILE_NOT_FOUND) return true;
        if (result == ERROR_SUCCESS) {
            result = RegDeleteValueW(hKey, kAutoStartValueName);
            if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
        }
    }

    if (hKey) RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

int GetListItemHeight(HWND hwnd) {
    UINT dpi = GetWindowDpi(hwnd);
    int lineHeight = abs(MulDiv(g_fontSize, (int)dpi, 72));
    int minLineHeight = DpiScale(18, dpi);
    if (lineHeight < minLineHeight) lineHeight = minLineHeight;
    return (lineHeight + DpiScale(4, dpi)) * g_itemLines + DpiScale(18, dpi);
}

void UpdateMainFont(HWND hwnd) {
    if (g_hFontMain) {
        DeleteObject(g_hFontMain);
        g_hFontMain = NULL;
    }
    if (g_hFontTime) {
        DeleteObject(g_hFontTime);
        g_hFontTime = NULL;
    }
    UINT dpi = GetWindowDpi(hwnd);
    int mainHeight = -MulDiv(g_fontSize, (int)dpi, 72);
    int timeHeight = -MulDiv(std::max(g_fontSize - 4, 10), (int)dpi, 72);
    g_hFontMain = CreateFontW(mainHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_hFontTime = CreateFontW(timeHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (g_hwndList) {
        InvalidateRect(g_hwndList, NULL, FALSE);
    }
}

// --- 平滑滚动列表的支撑函数 ---
// 内容总高度减去可视高度，得到最大可滚动像素；不足一屏则为 0
int GetClipListMaxScroll() {
    if (!g_hwndList) return 0;
    RECT rc; GetClientRect(g_hwndList, &rc);
    int viewH = rc.bottom - rc.top;
    int itemH = GetListItemHeight(g_hwndList);
    int total = (int)g_history.size() * itemH;
    int maxScroll = total - viewH;
    return (maxScroll > 0) ? maxScroll : 0;
}

void ClampScroll() {
    int maxS = GetClipListMaxScroll();
    if (g_scrollTargetY < 0) g_scrollTargetY = 0;
    if (g_scrollTargetY > maxS) g_scrollTargetY = maxS;
    if (g_scrollY < 0) g_scrollY = 0;
    if (g_scrollY > maxS) g_scrollY = maxS;
}

void UpdateScrollBar() {
    if (!g_hwndList) return;
    RECT rc; GetClientRect(g_hwndList, &rc);
    int viewH = rc.bottom - rc.top;
    int itemH = GetListItemHeight(g_hwndList);
    int total = (int)g_history.size() * itemH;
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = (total > 0) ? total - 1 : 0;
    si.nPage = viewH;
    si.nPos = g_scrollY;
    SetScrollInfo(g_hwndList, SB_VERT, &si, TRUE);
}

// 让指定项完整出现在可视区内（用于键盘上下移动选中）
void EnsureIndexVisible(int index) {
    if (!g_hwndList || index < 0 || index >= (int)g_history.size()) return;
    int itemH = GetListItemHeight(g_hwndList);
    RECT rc; GetClientRect(g_hwndList, &rc);
    int viewH = rc.bottom - rc.top;
    int top = index * itemH;
    int bottom = top + itemH;
    if (top < g_scrollTargetY) g_scrollTargetY = top;
    else if (bottom > g_scrollTargetY + viewH) g_scrollTargetY = bottom - viewH;
    ClampScroll();
}

// 把单张卡片画到 rcItem 指定的矩形内（从原 WM_DRAWITEM 抽出，供自绘列表复用）
void DrawCard(HDC hdc, int index, RECT rcItem, bool isSelected) {
    if (index < 0 || index >= (int)g_history.size()) return;
    UINT dpi = GetWindowDpi(g_hwndList);
    int cardInsetX = DpiScale(10, dpi);
    int cardInsetY = DpiScale(5, dpi);
    int cardRadius = DpiScale(10, dpi);
    RECT rcCard = rcItem;
    rcCard.left += cardInsetX; rcCard.right -= cardInsetX;
    rcCard.top += cardInsetY; rcCard.bottom -= cardInsetY;
    HBRUSH cardBrush = isSelected ? g_selectedCardBrush : g_cardBrush;
    HPEN borderPen = isSelected ? g_selectedCardPen : g_cardPen;
    HGDIOBJ oldBrush = SelectObject(hdc, cardBrush); HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(hdc, rcCard.left, rcCard.top, rcCard.right, rcCard.bottom, cardRadius, cardRadius);
    SelectObject(hdc, oldBrush); SelectObject(hdc, oldPen);

    ClipItem& item = g_history[index];
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_hFontTime); SetTextColor(hdc, RGB(150, 150, 150));
    RECT rcTime = rcCard;
    rcTime.left += DpiScale(12, dpi);
    rcTime.top += DpiScale(8, dpi);
    rcTime.bottom = rcTime.top + DpiScale(15, dpi);
    DrawTextW(hdc, item.timestamp.c_str(), -1, &rcTime, DT_LEFT | DT_TOP | DT_SINGLELINE);

    RECT rcContent = rcCard;
    rcContent.left += DpiScale(12, dpi);
    rcContent.right -= DpiScale(12, dpi);
    rcContent.top += DpiScale(28, dpi);
    rcContent.bottom -= DpiScale(10, dpi);
    SelectObject(hdc, g_hFontMain); SetTextColor(hdc, RGB(40, 40, 40));

    if (item.type == TYPE_TEXT) {
        std::wstring displayMsg = item.text.substr(0, 300);
        if (!g_wordWrap) {
            for (auto& c : displayMsg) if (c == L'\r' || c == L'\n' || c == L'\t') c = L' ';
            DrawTextW(hdc, displayMsg.c_str(), -1, &rcContent, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            for (auto& c : displayMsg) if (c == L'\r' || c == L'\n') c = L' ';
            DrawTextW(hdc, displayMsg.c_str(), -1, &rcContent, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
        }
    } else if (item.type == TYPE_IMAGE) {
        SelectObject(hdc, g_hFontTime);
        DrawTextW(hdc, item.imagePending ? L"[图片处理中]" : L"[图片]", -1, &rcContent, DT_LEFT | DT_TOP);
        if (!item.hThumbnail) return;

        Graphics graphics(hdc);
        graphics.SetInterpolationMode(InterpolationModeBilinear);

        Bitmap bmp(item.hThumbnail, NULL);
        UINT w = bmp.GetWidth(); UINT h = bmp.GetHeight();
        if (w == 0) w = 1; if (h == 0) h = 1;

        int cellInnerH = rcContent.bottom - rcContent.top;
        int minImageSize = DpiScale(8, dpi);
        int maxH = (cellInnerH > minImageSize) ? cellInnerH : minImageSize;
        int maxW = (rcCard.right - rcCard.left) * 55 / 100;
        if (maxW < minImageSize) maxW = minImageSize;
        float scaleX = (float)maxW / w; float scaleY = (float)maxH / h;
        float scale = (scaleX < scaleY) ? scaleX : scaleY;

        int drawW = (w * scale > 1) ? (int)(w * scale) : 1;
        int drawH = (h * scale > 1) ? (int)(h * scale) : 1;
        int drawX = rcCard.right - DpiScale(10, dpi) - drawW;
        int drawY = rcContent.top + (cellInnerH - drawH) / 2;

        graphics.DrawImage(&bmp, drawX, drawY, drawW, drawH);
    } else if (item.type == TYPE_FILE_LIST) {
        SetTextColor(hdc, RGB(220, 38, 38));
        DrawTextW(hdc, L"[磁盘文件]", -1, &rcContent, DT_LEFT | DT_TOP | DT_SINGLELINE);
        SetTextColor(hdc, RGB(40, 40, 40));
        RECT rcPaths = rcContent;
        rcPaths.top += DpiScale(22, dpi);
        std::wstring displayMsg = item.text;
        for (auto& c : displayMsg) if (c == L'\r' || c == L'\n') c = L' ';
        DrawTextW(hdc, displayMsg.c_str(), -1, &rcPaths, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_END_ELLIPSIS);
    }
}

bool EnsureListBackBuffer(HDC referenceDC, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (!g_listBufferDC) {
        g_listBufferDC = CreateCompatibleDC(referenceDC);
        if (!g_listBufferDC) return false;
    }
    if (!g_listBufferBitmap || width != g_listBufferWidth || height != g_listBufferHeight) {
        if (g_listBufferBitmap) {
            SelectObject(g_listBufferDC, g_listBufferOldBitmap);
            DeleteObject(g_listBufferBitmap);
        }
        g_listBufferBitmap = CreateCompatibleBitmap(referenceDC, width, height);
        if (!g_listBufferBitmap) return false;
        g_listBufferOldBitmap = (HBITMAP)SelectObject(g_listBufferDC, g_listBufferBitmap);
        g_listBufferWidth = width;
        g_listBufferHeight = height;
    }
    if (!g_listBackgroundBrush) g_listBackgroundBrush = CreateSolidBrush(RGB(243, 244, 246));
    if (!g_cardBrush) g_cardBrush = CreateSolidBrush(RGB(255, 255, 255));
    if (!g_selectedCardBrush) g_selectedCardBrush = CreateSolidBrush(RGB(230, 244, 255));
    if (!g_cardPen) g_cardPen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
    if (!g_selectedCardPen) g_selectedCardPen = CreatePen(PS_SOLID, 1, RGB(102, 178, 255));
    return g_listBackgroundBrush && g_cardBrush && g_selectedCardBrush && g_cardPen && g_selectedCardPen;
}

void DestroyListDrawingResources() {
    if (g_listBufferDC && g_listBufferBitmap) {
        SelectObject(g_listBufferDC, g_listBufferOldBitmap);
        DeleteObject(g_listBufferBitmap);
    }
    if (g_listBufferDC) DeleteDC(g_listBufferDC);
    if (g_listBackgroundBrush) DeleteObject(g_listBackgroundBrush);
    if (g_cardBrush) DeleteObject(g_cardBrush);
    if (g_selectedCardBrush) DeleteObject(g_selectedCardBrush);
    if (g_cardPen) DeleteObject(g_cardPen);
    if (g_selectedCardPen) DeleteObject(g_selectedCardPen);
    g_listBufferDC = NULL;
    g_listBufferBitmap = NULL;
    g_listBufferOldBitmap = NULL;
    g_listBufferWidth = g_listBufferHeight = 0;
    g_listBackgroundBrush = g_cardBrush = g_selectedCardBrush = NULL;
    g_cardPen = g_selectedCardPen = NULL;
}

// 把第 index 项写入剪贴板并模拟粘贴（从原 LBN_DBLCLK 抽出）
void PasteHistoryItem(int index) {
    if (index < 0 || index >= (int)g_history.size()) return;
    if (g_history[index].imagePending) return;
    ShowWindow(g_hwndMain, SW_HIDE);
    g_isPasting = true;
    ClipItem& selItem = g_history[index];
    bool clipboardReady = false;
    if (selItem.type == TYPE_TEXT) clipboardReady = SetClipboardText(selItem.text);
    else if (selItem.type == TYPE_IMAGE) {
        HBITMAP hOriginal = LoadBitmapFromPNG(selItem.filePath);
        if (!hOriginal) {
            g_isPasting = false;
            return;
        }
        clipboardReady = SetClipboardImage(hOriginal);
        DeleteObject(hOriginal);
    }
    else if (selItem.type == TYPE_FILE_LIST) clipboardReady = SetClipboardFiles(selItem.text);
    if (!clipboardReady) {
        g_isPasting = false;
        return;
    }
    g_ignoreNextClipboardUpdate = true;
    SimulatePaste();
    g_isPasting = false;
}

// 自绘平滑滚动列表的窗口过程
LRESULT CALLBACK ClipListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // 全自绘，吞掉擦背景以消除闪烁

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left, height = rc.bottom - rc.top;
            if (!EnsureListBackBuffer(hdc, width, height)) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            FillRect(g_listBufferDC, &rc, g_listBackgroundBrush);

            int itemH = GetListItemHeight(hwnd);
            if (itemH > 0) {
                int first = g_scrollY / itemH;
                if (first < 0) first = 0;
                for (int i = first; i < (int)g_history.size(); ++i) {
                    int top = i * itemH - g_scrollY;
                    if (top >= height) break;
                    RECT rcItem = { 0, top, width, top + itemH };
                    DrawCard(g_listBufferDC, i, rcItem, i == g_selIndex);
                }
            }

            BitBlt(hdc, 0, 0, width, height, g_listBufferDC, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_scrollTargetY -= (delta / WHEEL_DELTA) * SCROLL_WHEEL_STEP;
            ClampScroll();
            if (g_scrollTimer == 0) {
                g_scrollTimer = SetTimer(hwnd, SCROLL_TIMER_ID, SCROLL_FRAME_MS, NULL);
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == SCROLL_TIMER_ID) {
                int diff = g_scrollTargetY - g_scrollY;
                if (diff == 0) {
                    KillTimer(hwnd, SCROLL_TIMER_ID); g_scrollTimer = 0;
                    return 0;
                }
                int step = (int)(diff * SCROLL_EASING);
                if (step == 0) step = (diff > 0) ? 1 : -1; // 保证最终收敛到目标
                g_scrollY += step;
                if ((step > 0 && g_scrollY > g_scrollTargetY) ||
                    (step < 0 && g_scrollY < g_scrollTargetY)) {
                    g_scrollY = g_scrollTargetY;
                }
                UpdateScrollBar();
                InvalidateRect(hwnd, NULL, FALSE);
                if (g_scrollY == g_scrollTargetY) {
                    KillTimer(hwnd, SCROLL_TIMER_ID); g_scrollTimer = 0;
                }
            }
            return 0;
        }

        case WM_VSCROLL: {
            int code = LOWORD(wParam);
            int itemH = GetListItemHeight(hwnd);
            RECT rc; GetClientRect(hwnd, &rc); int viewH = rc.bottom - rc.top;
            switch (code) {
                case SB_LINEUP:   g_scrollTargetY -= itemH; break;
                case SB_LINEDOWN: g_scrollTargetY += itemH; break;
                case SB_PAGEUP:   g_scrollTargetY -= viewH; break;
                case SB_PAGEDOWN: g_scrollTargetY += viewH; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    // 拖动滚动条直接定位，无动画（手感更跟手）
                    SCROLLINFO si = {}; si.cbSize = sizeof(si); si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(hwnd, SB_VERT, &si);
                    g_scrollY = g_scrollTargetY = si.nTrackPos;
                    ClampScroll(); UpdateScrollBar(); InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            ClampScroll();
            if (g_scrollTimer == 0) g_scrollTimer = SetTimer(hwnd, SCROLL_TIMER_ID, SCROLL_FRAME_MS, NULL);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int y = GET_Y_LPARAM(lParam);
            int itemH = GetListItemHeight(hwnd);
            if (itemH > 0) {
                int idx = (y + g_scrollY) / itemH;
                if (idx >= 0 && idx < (int)g_history.size()) g_selIndex = idx;
                else g_selIndex = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            SetFocus(hwnd);
            return 0;
        }

        case WM_LBUTTONDBLCLK: {
            int y = GET_Y_LPARAM(lParam);
            int itemH = GetListItemHeight(hwnd);
            if (itemH > 0) {
                int idx = (y + g_scrollY) / itemH;
                if (idx >= 0 && idx < (int)g_history.size()) PasteHistoryItem(idx);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                ShowWindow(g_hwndMain, SW_HIDE);
                return 0;
            }
            if (wParam == VK_RETURN) {
                if (g_selIndex >= 0) PasteHistoryItem(g_selIndex);
                return 0;
            }
            if (wParam == VK_DOWN || wParam == VK_UP) {
                if (g_history.empty()) return 0;
                if (g_selIndex < 0) g_selIndex = 0;
                else g_selIndex += (wParam == VK_DOWN) ? 1 : -1;
                if (g_selIndex < 0) g_selIndex = 0;
                if (g_selIndex >= (int)g_history.size()) g_selIndex = (int)g_history.size() - 1;
                EnsureIndexVisible(g_selIndex);
                if (g_scrollTimer == 0) g_scrollTimer = SetTimer(hwnd, SCROLL_TIMER_ID, SCROLL_FRAME_MS, NULL);
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            return 0;
        }

        case WM_SIZE: {
            ClampScroll();
            UpdateScrollBar();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_DESTROY:
            DestroyListDrawingResources();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 动态注册防冲突按键
void RegisterGlobalHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, 1);
    UINT modifiers = MOD_NOREPEAT; UINT vk = 'V';
    
    if (g_hotkeyMode == 1) modifiers |= MOD_CONTROL | MOD_SHIFT;
    else if (g_hotkeyMode == 2) modifiers |= MOD_ALT;
    else if (g_hotkeyMode == 3) modifiers |= MOD_WIN;
    
    if (!RegisterHotKey(hwnd, 1, modifiers, vk)) {
        // 如果热键被占用抛出友情弹窗！
        if (g_hotkeyMode == 3) {
            MessageBoxW(hwnd, L"接管 Win+V 失败！\n\n请按键盘Win键，输入'剪贴板设置'搜索，将系统自带的【剪贴板历史记录】开关关闭后方可接管使用。\n\n目前暂且自动为您回退至: Ctrl + Shift + V", L"快捷键高能冲突拦截", MB_OK | MB_ICONWARNING);
            g_hotkeyMode = 1; SaveSettings();
            RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, vk);
        }
    }
}

// （其他辅助底层函数保持一致原样保留，因字符字数要求不罗列，复制进去即可编译）
std::wstring GetCurrentTimeStr() { SYSTEMTIME st; GetLocalTime(&st); wchar_t buff[128]; wsprintfW(buff, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond); return buff; }
std::wstring GetFileSafeTimeStr() { SYSTEMTIME st; GetLocalTime(&st); wchar_t buff[128]; wsprintfW(buff, L"%04d%02d%02d_%02d%02d%02d_%03d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds); return buff; }
void AddItemToHistory(ClipType type, const std::wstring& text, HBITMAP hBmp) {
    if (!g_isPasting) {
        if (g_soundEnabled && wcslen(g_soundFilePath) > 0) {
            PlaySoundW(g_soundFilePath, NULL, SND_FILENAME | SND_ASYNC);
        } else {
            MessageBeep(MB_OK);
        }
    }
    ClipItem item; item.id = ++g_nextClipItemId; item.type = type; item.timestamp = GetCurrentTimeStr();
    ImageTask* imageTask = NULL;
    if (type == TYPE_TEXT || type == TYPE_FILE_LIST) {
        item.text = text; item.filePath = g_historyDirPath + L"\\" + GetFileSafeTimeStr() + L"_" + std::to_wstring(item.id) + L".txt";
        HANDLE hFile = CreateFileW(item.filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            WORD bom = 0xFEFF; DWORD bw = 0;
            WriteFile(hFile, &bom, 2, &bw, NULL);
            if (type == TYPE_FILE_LIST) {
                const std::wstring header = L"CLIPTYPE=FILE_LIST\n";
                WriteFile(hFile, header.c_str(), header.length() * 2, &bw, NULL);
            }
            WriteFile(hFile, text.c_str(), text.length() * 2, &bw, NULL);
            CloseHandle(hFile);
        }
    } else if (type == TYPE_IMAGE) {
        HBITMAP hOriginal = (HBITMAP)CopyImage(hBmp, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);
        if (!hOriginal) return;
        item.filePath = g_historyDirPath + L"\\" + GetFileSafeTimeStr() + L"_" + std::to_wstring(item.id) + L".png";
        item.imagePending = true;
        imageTask = new ImageTask{ g_hwndMain, item.id, item.filePath, hOriginal };
    }
    g_history.insert(g_history.begin(), item);
    if (g_selIndex >= 0) g_selIndex++;       // 选中项随之下移，保持指向原来那条
    g_scrollY = g_scrollTargetY = 0;          // 新内容置顶，滚回顶部展示最新
    if (g_hwndList) { UpdateScrollBar(); InvalidateRect(g_hwndList, NULL, FALSE); }
    EnforceHistoryRetention();
    EnforceHistoryLimit();
    if (imageTask) {
        InterlockedIncrement(&g_imageWorkerCount);
        if (g_imageWorkersDone) ResetEvent(g_imageWorkersDone);
        HANDLE thread = CreateThread(NULL, 0, ImageWorkerProc, imageTask, 0, NULL);
        if (thread) CloseHandle(thread);
        else ImageWorkerProc(imageTask);
    }
}
void EnforceHistoryLimit() { while (g_history.size() > g_historyMaxCount) { DeleteHistoryAtIndex((int)g_history.size() - 1); } }
void ClearAllHistory() {
    for (auto& item : g_history) {
        if (item.hThumbnail) DeleteObject(item.hThumbnail);
    }
    g_history.clear();
    g_selIndex = -1;
    g_scrollY = g_scrollTargetY = 0;
    if (g_hwndList) { UpdateScrollBar(); InvalidateRect(g_hwndList, NULL, FALSE); }

    std::wstring searchPath = g_historyDirPath + L"\\*.*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = findData.cFileName;
            if (name == L"." || name == L".." || _wcsicmp(name.c_str(), L"config.ini") == 0) continue;
            std::wstring fullPath = g_historyDirPath + L"\\" + name;
            DeleteFileW(fullPath.c_str());
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) { UINT num = 0, size = 0; GetImageEncodersSize(&num, &size); if(size == 0) return -1; ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)malloc(size); if(pImageCodecInfo == NULL) return -1; GetImageEncoders(num, size, pImageCodecInfo); for(UINT j = 0; j < num; ++j) { if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 ) { *pClsid = pImageCodecInfo[j].Clsid; free(pImageCodecInfo); return j; } } free(pImageCodecInfo); return -1; }
bool SaveBitmapToPNG(HBITMAP hBitmap, const std::wstring& filePath) { Bitmap bmp(hBitmap, NULL); CLSID pngClsid; if (GetEncoderClsid(L"image/png", &pngClsid) != -1) { bmp.Save(filePath.c_str(), &pngClsid, NULL); return true; } return false; }
void SetupTrayIcon(HWND hwnd) { memset(&g_nid, 0, sizeof(g_nid)); g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = hwnd; g_nid.uID = 1; g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAYICON; g_nid.hIcon = LoadIcon((HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE), MAKEINTRESOURCE(1)); lstrcpyW(g_nid.szTip, L"剪贴板管理器"); Shell_NotifyIconW(NIM_ADD, &g_nid); }

bool LoadTextFile(const std::wstring& filePath, std::wstring& outText) {
    outText.clear();
    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize == INVALID_FILE_SIZE || fileSize < 2) { CloseHandle(hFile); return false; }
    std::vector<BYTE> buffer(fileSize);
    DWORD bytesRead = 0;
    bool ok = ReadFile(hFile, buffer.data(), fileSize, &bytesRead, NULL) && bytesRead >= 2;
    CloseHandle(hFile);
    if (!ok) return false;
    size_t charCount = bytesRead / 2;
    const wchar_t* data = reinterpret_cast<const wchar_t*>(buffer.data());
    outText.assign(data, charCount);
    if (!outText.empty() && outText[0] == 0xFEFF) {
        outText.erase(0, 1);
    }
    return true;
}

HBITMAP LoadBitmapFromPNG(const std::wstring& filePath) {
    Bitmap* bitmap = Bitmap::FromFile(filePath.c_str(), FALSE);
    if (!bitmap || bitmap->GetLastStatus() != Ok) {
        delete bitmap;
        return NULL;
    }
    HBITMAP hBmp = NULL;
    if (bitmap->GetHBITMAP(Color::White, &hBmp) != Ok) {
        hBmp = NULL;
    }
    delete bitmap;
    return hBmp;
}

HBITMAP CreateThumbnail(Image& source) {
    UINT sourceW = source.GetWidth();
    UINT sourceH = source.GetHeight();
    if (sourceW == 0 || sourceH == 0) return NULL;

    double scaleW = (double)THUMBNAIL_MAX_WIDTH / sourceW;
    double scaleH = (double)THUMBNAIL_MAX_HEIGHT / sourceH;
    double scale = std::min(1.0, std::min(scaleW, scaleH));
    int thumbW = std::max(1, (int)(sourceW * scale));
    int thumbH = std::max(1, (int)(sourceH * scale));

    Bitmap thumbnail(thumbW, thumbH, PixelFormat32bppARGB);
    if (thumbnail.GetLastStatus() != Ok) return NULL;
    Graphics graphics(&thumbnail);
    graphics.Clear(Color::White);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    if (graphics.DrawImage(&source, 0, 0, thumbW, thumbH) != Ok) return NULL;

    HBITMAP hThumbnail = NULL;
    if (thumbnail.GetHBITMAP(Color::White, &hThumbnail) != Ok) return NULL;
    return hThumbnail;
}

HBITMAP LoadThumbnailFromPNG(const std::wstring& filePath) {
    Bitmap source(filePath.c_str(), FALSE);
    if (source.GetLastStatus() != Ok) return NULL;
    return CreateThumbnail(source);
}

void CleanupImageResult(ImageResult* result, bool deleteFile) {
    if (!result) return;
    if (result->hThumbnail) DeleteObject(result->hThumbnail);
    if (deleteFile && !result->filePath.empty()) DeleteFileW(result->filePath.c_str());
    delete result;
}

DWORD WINAPI ImageWorkerProc(LPVOID param) {
    ImageTask* task = reinterpret_cast<ImageTask*>(param);
    HWND targetWindow = task->hwnd;
    ImageResult* result = new ImageResult();
    result->itemId = task->itemId;
    result->filePath = task->filePath;

    if (SaveBitmapToPNG(task->hOriginal, task->filePath)) {
        Bitmap source(task->hOriginal, NULL);
        result->hThumbnail = CreateThumbnail(source);
        result->success = result->hThumbnail != NULL;
    }
    DeleteObject(task->hOriginal);
    delete task;

    if (!result->success && !result->filePath.empty()) DeleteFileW(result->filePath.c_str());
    if (!PostMessageW(targetWindow, WM_IMAGE_READY, 0, reinterpret_cast<LPARAM>(result))) {
        CleanupImageResult(result, false);
    }
    if (InterlockedDecrement(&g_imageWorkerCount) == 0 && g_imageWorkersDone) {
        SetEvent(g_imageWorkersDone);
    }
    return 0;
}

std::wstring GetFileTimestamp(const std::wstring& filePath) {
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &attr)) {
        return GetCurrentTimeStr();
    }
    FILETIME localTime;
    FileTimeToLocalFileTime(&attr.ftLastWriteTime, &localTime);
    SYSTEMTIME st = {};
    FileTimeToSystemTime(&localTime, &st);
    wchar_t buffer[64];
    wsprintfW(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

void LoadHistoryFromDisk() {
    std::wstring searchPath = g_historyDirPath + L"\\*.*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE) return;
    struct HistoryEntry { std::wstring path; std::wstring name; };
    std::vector<HistoryEntry> files;
    do {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = findData.cFileName;
        if (name == L"." || name == L".." || _wcsicmp(name.c_str(), L"config.ini") == 0) continue;
        std::wstring ext;
        size_t dotPos = name.find_last_of(L'.');
        if (dotPos != std::wstring::npos) ext = name.substr(dotPos);
        if (_wcsicmp(ext.c_str(), L".txt") == 0 || _wcsicmp(ext.c_str(), L".png") == 0) {
            files.push_back({ g_historyDirPath + L"\\" + name, name });
        }
    } while (FindNextFileW(hFind, &findData));
    FindClose(hFind);
    if (files.empty()) return;
    std::sort(files.begin(), files.end(), [](const HistoryEntry& a, const HistoryEntry& b) {
        return a.name > b.name;
    });
    files.erase(std::remove_if(files.begin(), files.end(), [](const HistoryEntry& entry) {
        if (!IsFileOlderThan(entry.path, g_historyRetentionDays)) return false;
        DeleteFileW(entry.path.c_str());
        return true;
    }), files.end());
    if (files.size() > (size_t)g_historyMaxCount) {
        for (size_t i = g_historyMaxCount; i < files.size(); ++i) {
            DeleteFileW(files[i].path.c_str());
        }
        files.resize(g_historyMaxCount);
    }
    g_history.clear();
    g_selIndex = -1;
    g_scrollY = g_scrollTargetY = 0;
    for (const auto& entry : files) {
        if (g_history.size() >= g_historyMaxCount) break;
        std::wstring ext;
        size_t dotPos = entry.name.find_last_of(L'.');
        if (dotPos != std::wstring::npos) ext = entry.name.substr(dotPos);
        ClipItem item;
        item.id = ++g_nextClipItemId;
        item.filePath = entry.path;
        item.timestamp = GetFileTimestamp(entry.path);
        item.hThumbnail = NULL;
        if (_wcsicmp(ext.c_str(), L".png") == 0) {
            HBITMAP hThumbnail = LoadThumbnailFromPNG(entry.path);
            if (!hThumbnail) continue;
            item.type = TYPE_IMAGE;
            item.hThumbnail = hThumbnail;
        } else {
            std::wstring text;
            if (!LoadTextFile(entry.path, text)) continue;
            const std::wstring fileListHeader = L"CLIPTYPE=FILE_LIST\n";
            if (text.rfind(fileListHeader, 0) == 0) {
                item.type = TYPE_FILE_LIST;
                item.text = text.substr(fileListHeader.size());
            } else {
                item.type = TYPE_TEXT;
                item.text = text;
            }
        }
        g_history.push_back(item);
    }
    EnforceHistoryRetention();
    EnforceHistoryLimit();
    if (g_hwndList) { UpdateScrollBar(); InvalidateRect(g_hwndList, NULL, FALSE); }
}

void DeleteHistoryAtIndex(int index) {
    if (index < 0 || index >= (int)g_history.size()) return;
    ClipItem& item = g_history[index];
    if (!item.filePath.empty()) DeleteFileW(item.filePath.c_str());
    if (item.hThumbnail) DeleteObject(item.hThumbnail);
    g_history.erase(g_history.begin() + index);
    // 修正选中项与滚动量，避免越界
    if (g_selIndex == index) g_selIndex = -1;
    else if (g_selIndex > index) g_selIndex--;
    if (g_hwndList) {
        ClampScroll();
        UpdateScrollBar();
        InvalidateRect(g_hwndList, NULL, FALSE);
    }
}

bool IsFileOlderThan(const std::wstring& filePath, int days) {
    if (days < 1) return false;
    WIN32_FILE_ATTRIBUTE_DATA attr = {};
    if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &attr)) return false;
    FILETIME nowUtc;
    GetSystemTimeAsFileTime(&nowUtc);
    ULARGE_INTEGER fileTime = { attr.ftLastWriteTime.dwLowDateTime, attr.ftLastWriteTime.dwHighDateTime };
    ULARGE_INTEGER nowTime = { nowUtc.dwLowDateTime, nowUtc.dwHighDateTime };
    if (fileTime.QuadPart >= nowTime.QuadPart) return false;
    unsigned long long diff = nowTime.QuadPart - fileTime.QuadPart;
    const unsigned long long interval = (unsigned long long)days * 24ull * 60ull * 60ull * 10000000ull;
    return diff > interval;
}

void EnforceHistoryRetention() {
    if (g_historyRetentionDays < 1) return;
    for (int i = (int)g_history.size() - 1; i >= 0; --i) {
        if (IsFileOlderThan(g_history[i].filePath, g_historyRetentionDays)) {
            DeleteHistoryAtIndex(i);
        }
    }
}

void UpdateSettingsControls() {
    if (g_hwndSettingsMaxCount) {
        SetWindowTextW(g_hwndSettingsMaxCount, std::to_wstring(g_historyMaxCount).c_str());
    }
    if (g_hwndSettingsRetention) {
        SetWindowTextW(g_hwndSettingsRetention, std::to_wstring(g_historyRetentionDays).c_str());
    }
    if (g_hwndSettingsWrap) {
        SendMessageW(g_hwndSettingsWrap, BM_SETCHECK, g_wordWrap ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsFontSize) {
        SetWindowTextW(g_hwndSettingsFontSize, std::to_wstring(g_fontSize).c_str());
    }
    if (g_hwndSettingsLineCount) {
        SetWindowTextW(g_hwndSettingsLineCount, std::to_wstring(g_itemLines).c_str());
    }
    if (g_hwndSettingsHotkey1) {
        SendMessageW(g_hwndSettingsHotkey1, BM_SETCHECK, g_hotkeyMode == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsHotkey2) {
        SendMessageW(g_hwndSettingsHotkey2, BM_SETCHECK, g_hotkeyMode == 2 ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsHotkey3) {
        SendMessageW(g_hwndSettingsHotkey3, BM_SETCHECK, g_hotkeyMode == 3 ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsSoundEnabled) {
        SendMessageW(g_hwndSettingsSoundEnabled, BM_SETCHECK, g_soundEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsAutoStart) {
        SendMessageW(g_hwndSettingsAutoStart, BM_SETCHECK,
            IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    if (g_hwndSettingsSoundPath) {
        SetWindowTextW(g_hwndSettingsSoundPath, g_soundFilePath);
    }
}

void ApplySettingsFromDialog() {
    if (!g_hwndSettingsMaxCount || !g_hwndSettingsRetention || !g_hwndSettingsWrap) return;
    wchar_t buffer[32] = {};
    GetWindowTextW(g_hwndSettingsMaxCount, buffer, sizeof(buffer) / sizeof(buffer[0]));
    int maxCount = _wtoi(buffer);
    if (maxCount < 1) maxCount = 1;
    if (maxCount > 100) maxCount = 100;
    g_historyMaxCount = maxCount;
    GetWindowTextW(g_hwndSettingsRetention, buffer, sizeof(buffer) / sizeof(buffer[0]));
    int retention = _wtoi(buffer);
    if (retention < 1) retention = 1;
    if (retention > 365) retention = 365;
    g_historyRetentionDays = retention;
    g_wordWrap = SendMessageW(g_hwndSettingsWrap, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (g_hwndSettingsHotkey1 && SendMessageW(g_hwndSettingsHotkey1, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        g_hotkeyMode = 1;
    } else if (g_hwndSettingsHotkey2 && SendMessageW(g_hwndSettingsHotkey2, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        g_hotkeyMode = 2;
    } else {
        g_hotkeyMode = 3;
    }
    if (g_hwndSettingsFontSize) {
        GetWindowTextW(g_hwndSettingsFontSize, buffer, sizeof(buffer) / sizeof(buffer[0]));
        int fontSize = _wtoi(buffer);
        if (fontSize < 10) fontSize = 10;
        if (fontSize > 28) fontSize = 28;
        g_fontSize = fontSize;
    }
    if (g_hwndSettingsLineCount) {
        GetWindowTextW(g_hwndSettingsLineCount, buffer, sizeof(buffer) / sizeof(buffer[0]));
        int lines = _wtoi(buffer);
        if (lines < 1) lines = 1;
        if (lines > 6) lines = 6;
        g_itemLines = lines;
    }
    g_soundEnabled = g_hwndSettingsSoundEnabled &&
        SendMessageW(g_hwndSettingsSoundEnabled, BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(g_hwndSettingsSoundPath, g_soundFilePath, MAX_PATH);

    bool autoStartUpdateFailed = false;
    if (g_hwndSettingsAutoStart) {
        bool autoStartEnabled = SendMessageW(g_hwndSettingsAutoStart, BM_GETCHECK, 0, 0) == BST_CHECKED;
        autoStartUpdateFailed = !SetAutoStartEnabled(autoStartEnabled);
    }

    SaveSettings();
    UpdateMainFont(g_hwndMain);
    RegisterGlobalHotkey(g_hwndMain);
    EnforceHistoryRetention();
    EnforceHistoryLimit();
    UpdateSettingsControls();
    if (g_hwndList) InvalidateRect(g_hwndList, NULL, TRUE);
    if (autoStartUpdateFailed) {
        MessageBoxW(g_hwndSettings,
            L"无法更新开机启动设置，请检查当前用户注册表权限。",
            L"剪贴板管理器", MB_OK | MB_ICONWARNING);
    }
}

void ClearCacheFiles() {
    ClearAllHistory();
}

void OpenSettingsWindow(HWND hwnd) {
    if (g_hwndSettings && IsWindow(g_hwndSettings)) {
        ShowWindow(g_hwndSettings, SW_RESTORE);
        BringWindowToTop(g_hwndSettings);
        SetForegroundWindow(g_hwndSettings);
        return;
    }
    if (!g_settingsClassRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"ClipSettingsClass";
        wc.hbrBackground = CreateSolidBrush(RGB(241, 243, 245)); // 浅灰基底，卡片浮于其上
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            MessageBoxW(hwnd, L"无法注册设置窗口。", L"剪贴板管理器", MB_OK | MB_ICONERROR);
            return;
        }
        g_settingsClassRegistered = true;
    }

    ShowWindow(hwnd, SW_HIDE);
    UINT dpi = GetWindowDpi(hwnd);
    const int settingsWidth = DpiScale(520, dpi);
    const int settingsHeight = DpiScale(450, dpi);
    POINT cursor = {};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    GetMonitorInfoW(monitor, &monitorInfo);
    int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - settingsWidth) / 2;
    int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - settingsHeight) / 2;

    g_hwndSettings = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST,
        L"ClipSettingsClass", L"剪贴板设置",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        x, y, settingsWidth, settingsHeight,
        NULL, NULL, GetModuleHandle(NULL), NULL);
    if (g_hwndSettings) {
        ShowWindow(g_hwndSettings, SW_SHOWNORMAL);
        UpdateWindow(g_hwndSettings);
        BringWindowToTop(g_hwndSettings);
        SetForegroundWindow(g_hwndSettings);
    } else {
        DWORD error = GetLastError();
        wchar_t message[128] = {};
        wsprintfW(message, L"无法创建设置窗口，错误代码：%lu", error);
        MessageBoxW(hwnd, message, L"剪贴板管理器", MB_OK | MB_ICONERROR);
    }
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            UINT dpi = GetWindowDpi(hwnd);
            auto S = [dpi](int value) { return DpiScale(value, dpi); };
            g_hFontSettings = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

            // ── 卡片1：缓存管理 (y=60‥118) ──
            HWND hLabelCount = CreateWindowExW(0, L"STATIC", L"最大保存条数", WS_CHILD | WS_VISIBLE,
                S(26), S(90), S(112), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsMaxCount = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                S(145), S(88), S(68), S(24), hwnd, (HMENU)ID_SETTINGS_MAXCOUNT, GetModuleHandle(NULL), NULL);
            HWND hLabelRetention = CreateWindowExW(0, L"STATIC", L"历史保留天数", WS_CHILD | WS_VISIBLE,
                S(280), S(90), S(112), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsRetention = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                S(400), S(88), S(68), S(24), hwnd, (HMENU)ID_SETTINGS_RETENTION, GetModuleHandle(NULL), NULL);

            // ── 卡片2：显示设置 (y=128‥200) ──
            HWND hLabelFontSize = CreateWindowExW(0, L"STATIC", L"字号 (10–28)", WS_CHILD | WS_VISIBLE,
                S(26), S(158), S(100), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsFontSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                S(124), S(156), S(68), S(24), hwnd, (HMENU)ID_SETTINGS_FONT_SIZE, GetModuleHandle(NULL), NULL);
            HWND hLabelLineCount = CreateWindowExW(0, L"STATIC", L"显示行数 (1–6)", WS_CHILD | WS_VISIBLE,
                S(280), S(158), S(100), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsLineCount = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                S(378), S(156), S(68), S(24), hwnd, (HMENU)ID_SETTINGS_LINE_COUNT, GetModuleHandle(NULL), NULL);
            g_hwndSettingsWrap = CreateWindowExW(WS_EX_TRANSPARENT, L"BUTTON", L"长文本自动换行",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                S(26), S(186), S(160), S(22), hwnd, (HMENU)ID_SETTINGS_WRAP, GetModuleHandle(NULL), NULL);

            // ── 卡片3：快捷键 (y=210‥268) ──
            HWND hLabelHotkey = CreateWindowExW(0, L"STATIC", L"唤出快捷键", WS_CHILD | WS_VISIBLE,
                S(26), S(240), S(90), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsHotkey1 = CreateWindowExW(0, L"BUTTON", L"Ctrl + Shift + V",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                S(130), S(238), S(150), S(22), hwnd, (HMENU)ID_SETTINGS_HOTKEY_1, GetModuleHandle(NULL), NULL);
            g_hwndSettingsHotkey2 = CreateWindowExW(0, L"BUTTON", L"Alt + V",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                S(290), S(238), S(90), S(22), hwnd, (HMENU)ID_SETTINGS_HOTKEY_2, GetModuleHandle(NULL), NULL);
            g_hwndSettingsHotkey3 = CreateWindowExW(0, L"BUTTON", L"Win + V",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                S(390), S(238), S(80), S(22), hwnd, (HMENU)ID_SETTINGS_HOTKEY_3, GetModuleHandle(NULL), NULL);

            // ── 卡片4：音效 (y=278‥354) ──
            g_hwndSettingsSoundEnabled = CreateWindowExW(0, L"BUTTON", L"复制时播放音效",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                S(26), S(308), S(150), S(22), hwnd, (HMENU)ID_SETTINGS_SOUND_ENABLED, GetModuleHandle(NULL), NULL);
            g_hwndSettingsAutoStart = CreateWindowExW(0, L"BUTTON", L"开机自动启动",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                S(210), S(308), S(180), S(22), hwnd, (HMENU)ID_SETTINGS_AUTOSTART, GetModuleHandle(NULL), NULL);
            HWND hLabelSound = CreateWindowExW(0, L"STATIC", L"音效文件", WS_CHILD | WS_VISIBLE,
                S(26), S(334), S(60), S(22), hwnd, NULL, GetModuleHandle(NULL), NULL);
            g_hwndSettingsSoundPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                S(90), S(332), S(280), S(24), hwnd, (HMENU)ID_SETTINGS_SOUND_PATH, GetModuleHandle(NULL), NULL);
            HWND hButtonBrowseSound = CreateWindowExW(0, L"BUTTON", L"浏览",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(380), S(332), S(68), S(25), hwnd, (HMENU)ID_SETTINGS_SOUND_BROWSE, GetModuleHandle(NULL), NULL);

            // ── 底部按钮栏 ──
            HWND hButtonSave = CreateWindowExW(0, L"BUTTON", L"保存并应用",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(26), S(370), S(112), S(32), hwnd, (HMENU)ID_SETTINGS_SAVE, GetModuleHandle(NULL), NULL);
            HWND hButtonOpenDir = CreateWindowExW(0, L"BUTTON", L"打开缓存目录",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(146), S(370), S(112), S(32), hwnd, (HMENU)ID_SETTINGS_OPEN_DIR, GetModuleHandle(NULL), NULL);
            HWND hButtonClearCache = CreateWindowExW(0, L"BUTTON", L"清除缓存",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(266), S(370), S(112), S(32), hwnd, (HMENU)ID_SETTINGS_CLEAR_CACHE, GetModuleHandle(NULL), NULL);
            HWND hButtonClose = CreateWindowExW(0, L"BUTTON", L"关闭",
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                S(386), S(370), S(108), S(32), hwnd, (HMENU)ID_SETTINGS_CLOSE, GetModuleHandle(NULL), NULL);

            HWND hControls[] = { hLabelCount, g_hwndSettingsMaxCount, hLabelRetention, g_hwndSettingsRetention,
                hLabelFontSize, g_hwndSettingsFontSize, hLabelLineCount, g_hwndSettingsLineCount,
                g_hwndSettingsWrap, hLabelHotkey, g_hwndSettingsHotkey1, g_hwndSettingsHotkey2,
                g_hwndSettingsHotkey3, g_hwndSettingsSoundEnabled, g_hwndSettingsAutoStart,
                hLabelSound, g_hwndSettingsSoundPath };
            for (HWND hCtrl : hControls) {
                if (hCtrl) SendMessageW(hCtrl, WM_SETFONT, (WPARAM)g_hFontSettings, TRUE);
            }
            // owner-draw 按钮单独设字体
            HWND hOwnerBtns[] = { hButtonSave, hButtonOpenDir, hButtonClearCache, hButtonClose, hButtonBrowseSound };
            for (HWND hBtn : hOwnerBtns) {
                if (hBtn) SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontSettings, TRUE);
            }

            UpdateSettingsControls();
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc; GetClientRect(hwnd, &rc);
            UINT dpi = GetWindowDpi(hwnd);
            auto S = [dpi](int value) { return DpiScale(value, dpi); };

            // 深色标题栏
            RECT rcHdr = {0, 0, rc.right, S(48)};
            HBRUSH hdrBr = CreateSolidBrush(RGB(55, 65, 81)); FillRect(hdc, &rcHdr, hdrBr); DeleteObject(hdrBr);
            SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, RGB(255, 255, 255));
            HFONT hdrFont = CreateFontW(-S(20), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HGDIOBJ oldF = SelectObject(hdc, hdrFont);
            RECT rcT = {S(18), 0, rc.right - S(18), S(48)};
            DrawTextW(hdc, L"⚙  设置", -1, &rcT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldF); DeleteObject(hdrFont);

            // 四张白色卡片
            struct { int top, h; const wchar_t* title; } cards[] = {
                {60, 62,  L"📦  缓存管理"}, {132, 74, L"🎨  显示设置"},
                {216, 60,  L"⌨  快捷键"},   {286, 74, L"🔊  音效"}
            };
            HFONT secF = CreateFontW(-S(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            for (int i = 0; i < 4; i++) {
                RECT cr = {S(12), S(cards[i].top), rc.right - S(12), S(cards[i].top + cards[i].h)};
                HBRUSH cBr = CreateSolidBrush(RGB(255, 255, 255));
                HPEN cPn = CreatePen(PS_SOLID, 1, RGB(222, 226, 230));
                HGDIOBJ oBr = SelectObject(hdc, cBr), oPn = SelectObject(hdc, cPn);
                RoundRect(hdc, cr.left, cr.top, cr.right, cr.bottom, S(8), S(8));
                SelectObject(hdc, oBr); SelectObject(hdc, oPn); DeleteObject(cBr); DeleteObject(cPn);
                SetTextColor(hdc, RGB(107, 114, 128));
                SelectObject(hdc, secF);
                RECT rt = {cr.left + S(14), cr.top + S(6), cr.right - S(14), cr.top + S(24)};
                DrawTextW(hdc, cards[i].title, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }
            SelectObject(hdc, oldF); DeleteObject(secF);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType != ODT_BUTTON) break;
            RECT rc = dis->rcItem;
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool focused = (dis->itemState & ODS_FOCUS) != 0;
            int id = GetDlgCtrlID(dis->hwndItem);

            COLORREF fill, textClr, border;
            if (id == ID_SETTINGS_SAVE) {
                fill = pressed ? RGB(37, 99, 235) : RGB(59, 130, 246);   // 主色调蓝色
                textClr = RGB(255, 255, 255);
                border = fill;
            } else {
                fill = pressed ? RGB(219, 225, 233) : RGB(237, 240, 244); // 浅灰底
                textClr = RGB(55, 65, 81);
                border = RGB(201, 208, 217);
            }
            HBRUSH br = CreateSolidBrush(fill);
            HPEN pn = CreatePen(PS_SOLID, 1, border);
            HDC hdc = dis->hDC;
            HGDIOBJ oBr = SelectObject(hdc, br), oPn = SelectObject(hdc, pn);
            UINT dpi = GetWindowDpi(hwnd);
            auto S = [dpi](int value) { return DpiScale(value, dpi); };
            RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, S(6), S(6));
            SelectObject(hdc, oBr); SelectObject(hdc, oPn); DeleteObject(br); DeleteObject(pn);

            wchar_t txt[128]; GetWindowTextW(dis->hwndItem, txt, 128);
            SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, textClr);
            HFONT bf = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
            if (!bf) bf = g_hFontSettings;
            HGDIOBJ oF = SelectObject(hdc, bf);
            DrawTextW(hdc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oF);

            if (focused) { RECT rf = rc; InflateRect(&rf, -S(3), -S(3)); DrawFocusRect(hdc, &rf); }
            return TRUE;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            // 若控件落在白色卡片内 → 返回白刷，否则返回窗口灰刷
            HWND hChild = (HWND)lParam;
            RECT rcChild; GetWindowRect(hChild, &rcChild);
            POINT pt = {rcChild.left, rcChild.top}; ScreenToClient(hwnd, &pt);
            UINT dpi = GetWindowDpi(hwnd);
            return (pt.y >= DpiScale(60, dpi) && pt.y <= DpiScale(360, dpi))
                                               ? (LRESULT)GetStockObject(WHITE_BRUSH)
                                               : (LRESULT)GetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND);
        }

        case WM_CTLCOLOREDIT: {
            // 输入框白底黑字，跟卡片融为一体
            HDC hdcEdit = (HDC)wParam;
            SetBkColor(hdcEdit, RGB(255, 255, 255));
            SetTextColor(hdcEdit, RGB(55, 65, 81));
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }

        case WM_CTLCOLORBTN: {
            // 复选框和单选框：文字背景透明，返回空刷不填底色，让卡片自然透出
            HDC hdcBtn = (HDC)wParam;
            SetBkMode(hdcBtn, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SETTINGS_SAVE:
                    ApplySettingsFromDialog();
                    break;
                case ID_SETTINGS_OPEN_DIR:
                    ShellExecuteW(NULL, L"open", g_historyDirPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    break;
                case ID_SETTINGS_CLEAR_CACHE:
                    ClearCacheFiles();
                    break;
                case ID_SETTINGS_SOUND_BROWSE: {
                    OPENFILENAMEW ofn = {};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Wave Files (*.wav)\0*.wav\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = g_soundFilePath;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(g_hwndSettingsSoundPath, g_soundFilePath);
                    }
                    break;
                }
                case ID_SETTINGS_CLOSE:
                    DestroyWindow(hwnd);
                    break;
            }
            return 0;

        case WM_DESTROY:
            if (g_hFontSettings) {
                DeleteObject(g_hFontSettings);
                g_hFontSettings = NULL;
            }
            g_hwndSettings = NULL;
            g_hwndSettingsMaxCount = NULL;
            g_hwndSettingsRetention = NULL;
            g_hwndSettingsWrap = NULL;
            g_hwndSettingsHotkey1 = NULL;
            g_hwndSettingsHotkey2 = NULL;
            g_hwndSettingsHotkey3 = NULL;
            g_hwndSettingsFontSize = NULL;
            g_hwndSettingsLineCount = NULL;
            g_hwndSettingsSoundEnabled = NULL;
            g_hwndSettingsSoundPath = NULL;
            g_hwndSettingsAutoStart = NULL;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowManagerWindow(HWND hwnd) {
    UINT dpi = GetWindowDpi(hwnd);
    int w = DpiScale(400, dpi), h = DpiScale(600, dpi);
    POINT pt = {0, 0}; HWND hwndForeground = GetForegroundWindow(); DWORD threadId = GetWindowThreadProcessId(hwndForeground, NULL); GUITHREADINFO guiInfo = { sizeof(GUITHREADINFO) };
    if (GetGUIThreadInfo(threadId, &guiInfo) && guiInfo.hwndCaret != NULL) { pt.x = guiInfo.rcCaret.left; pt.y = guiInfo.rcCaret.bottom; ClientToScreen(guiInfo.hwndCaret, &pt); pt.y += DpiScale(5, dpi); } else { GetCursorPos(&pt); pt.y += DpiScale(10, dpi); }
    int screenW = GetSystemMetrics(SM_CXSCREEN), screenH = GetSystemMetrics(SM_CYSCREEN); if (pt.x + w > screenW) pt.x = screenW - w; if (pt.y + h > screenH) pt.y = screenH - h; if (pt.x < 0) pt.x = 0;
    SetWindowPos(hwnd, HWND_TOPMOST, pt.x, pt.y, w, h, SWP_SHOWWINDOW); SetForegroundWindow(hwnd); SetFocus(g_hwndList);
}
std::vector<std::wstring> GetClipboardFiles() { std::vector<std::wstring> files; for(int i=0; i<3; ++i) { if (OpenClipboard(NULL)) { if (IsClipboardFormatAvailable(CF_HDROP)) { HDROP hDrop = (HDROP)GetClipboardData(CF_HDROP); if (hDrop) { UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0); for (UINT j = 0; j < count; ++j) { wchar_t path[MAX_PATH]; if (DragQueryFileW(hDrop, j, path, MAX_PATH)) { files.push_back(path); } } } } CloseClipboard(); break; } Sleep(10); } return files; }
std::wstring GetClipboardText() { std::wstring result = L""; for(int i=0; i<3; i++) { if (OpenClipboard(NULL)) { if (IsClipboardFormatAvailable(CF_UNICODETEXT)) { HANDLE hData = GetClipboardData(CF_UNICODETEXT); if (hData) { wchar_t* pText = (wchar_t*)GlobalLock(hData); if (pText) result = pText; GlobalUnlock(hData); } } CloseClipboard(); break; } Sleep(10); } return result; }
HBITMAP GetClipboardImage() { HBITMAP result = NULL; for(int i=0; i<3; i++) { if (OpenClipboard(NULL)) { if (IsClipboardFormatAvailable(CF_BITMAP)) { HANDLE hData = GetClipboardData(CF_BITMAP); if (hData) result = (HBITMAP)hData; } CloseClipboard(); break; } Sleep(10); } return result; }
bool SetClipboardText(const std::wstring& text) {
    size_t size = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return false;
    void* target = GlobalLock(hMem);
    if (!target) { GlobalFree(hMem); return false; }
    memcpy(target, text.c_str(), size);
    GlobalUnlock(hMem);
    if (!OpenClipboard(NULL)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    bool success = SetClipboardData(CF_UNICODETEXT, hMem) != NULL;
    CloseClipboard();
    if (!success) GlobalFree(hMem);
    return success;
}

bool SetClipboardImage(HBITMAP hBmp) {
    HBITMAP hCopy = (HBITMAP)CopyImage(hBmp, IMAGE_BITMAP, 0, 0, LR_COPYRETURNORG);
    if (!hCopy) return false;
    if (!OpenClipboard(NULL)) { DeleteObject(hCopy); return false; }
    EmptyClipboard();
    bool success = SetClipboardData(CF_BITMAP, hCopy) != NULL;
    CloseClipboard();
    if (!success) DeleteObject(hCopy);
    return success;
}

bool SetClipboardFiles(const std::wstring& pathsData) {
    std::vector<std::wstring> paths;
    size_t pos = 0, last = 0;
    while ((pos = pathsData.find(L'\n', last)) != std::wstring::npos) {
        if (pos > last) paths.push_back(pathsData.substr(last, pos - last));
        last = pos + 1;
    }
    size_t bytesReq = sizeof(DROPFILES) + sizeof(wchar_t);
    for (const auto& path : paths) bytesReq += (path.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytesReq);
    if (!hMem) return false;
    DROPFILES* pDrop = (DROPFILES*)GlobalLock(hMem);
    if (!pDrop) { GlobalFree(hMem); return false; }
    pDrop->pFiles = sizeof(DROPFILES);
    pDrop->fWide = TRUE;
    wchar_t* pData = (wchar_t*)((BYTE*)pDrop + sizeof(DROPFILES));
    for (const auto& path : paths) {
        lstrcpyW(pData, path.c_str());
        pData += path.length() + 1;
    }
    *pData = L'\0';
    GlobalUnlock(hMem);
    if (!OpenClipboard(NULL)) { GlobalFree(hMem); return false; }
    EmptyClipboard();
    bool success = SetClipboardData(CF_HDROP, hMem) != NULL;
    CloseClipboard();
    if (!success) GlobalFree(hMem);
    return success;
}
void SimulatePaste() { INPUT inputs[4] = {}; inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL; inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = 'V'; inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = 'V'; inputs[2].ki.dwFlags = KEYEVENTF_KEYUP; inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = VK_CONTROL; inputs[3].ki.dwFlags = KEYEVENTF_KEYUP; SendInput(4, inputs, sizeof(INPUT)); }
