#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "can_manager.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

// 全局变量
HINSTANCE g_hInstance;
CANManager* g_canManager;
HWND g_hMainWindow;
HWND g_hStatusLog;
HWND g_hProgressBar;
HWND g_hConnectButton;
HWND g_hFlashButton;
HWND g_hGetVersionButton;
HWND g_hRebootButton;
HWND g_hBrowseButton;
HWND g_hFirmwareEdit;
HWND g_hChannelCombo;
HWND g_hBaudRateCombo;
HWND g_hTestModeCheck;
HWND g_hVersionLabel;
HWND g_hClearLogButton;
UINT g_dpi = 96;  // 系统DPI，用于控件缩放
HFONT g_hAppFont = NULL;  // 全局字体

static const int BAUD_RATES[] = {10000, 20000, 50000, 80000, 125000, 250000, 500000, 1000000};

// Refresh device list and populate combo box
void RefreshDeviceList() {
    char channelNames[16][16];
    int count = CAN_EnumDevices(channelNames, 16);

    SendMessage(g_hChannelCombo, CB_RESETCONTENT, 0, 0);

    if (count > 0) {
        for (int i = 0; i < count; i++) {
            wchar_t displayName[16];
            char tempChannel[16];
            strcpy(tempChannel, channelNames[i]);
            tempChannel[0] = toupper(tempChannel[0]);
            tempChannel[1] = toupper(tempChannel[1]);
            tempChannel[2] = toupper(tempChannel[2]);
            MultiByteToWideChar(CP_ACP, 0, tempChannel, -1, displayName, 16);
            SendMessageW(g_hChannelCombo, CB_ADDSTRING, 0, (LPARAM)displayName);
        }
        SendMessage(g_hChannelCombo, CB_SETCURSEL, 0, 0);
    }
}

// DPI 缩放辅助函数（需在 WndProc 之前定义）
int ScaleValue(int value, UINT dpi) {
    return MulDiv(value, dpi, 96);
}

// 日志函数
void AppendLog(const wchar_t* msg) {
    int len = GetWindowTextLength(g_hStatusLog);
    SendMessage(g_hStatusLog, EM_SETSEL, len, len);
    SendMessage(g_hStatusLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
    SendMessage(g_hStatusLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessage(g_hStatusLog, EM_SETSEL, -1, -1);
}

void ClearLog() {
    SetWindowTextW(g_hStatusLog, L"");
}

// CAN 回调
void OnStatusCallback(const char* msg, void*) {
    wchar_t wmsg[256];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 256);
    AppendLog(wmsg);
}

void OnErrorCallback(const char* msg, void*) {
    wchar_t wmsg[256];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 256);
    AppendLog(wmsg);
}

void OnProgressCallback(int pct, void*) {
    SendMessage(g_hProgressBar, PBM_SETPOS, pct, 0);
}

void OnVersionCallback(const char* ver, void*) {
    wchar_t wver[64];
    MultiByteToWideChar(CP_UTF8, 0, ver, -1, wver, 64);
    SetWindowTextW(g_hVersionLabel, wver);
}

void OnConnectedCallback(bool connected, void*) {
    EnableWindow(g_hFlashButton, connected);
    EnableWindow(g_hGetVersionButton, connected);
    EnableWindow(g_hRebootButton, connected);
    EnableWindow(g_hChannelCombo, !connected);
    EnableWindow(g_hBaudRateCombo, !connected);
    SetWindowTextW(g_hConnectButton, connected ? L"断开连接" : L"连接");
    if (!connected) {
        SendMessage(g_hProgressBar, PBM_SETPOS, 0, 0);
        SetWindowTextW(g_hVersionLabel, L"未连接");
    }
}

// 后台线程参数
struct FlashParams {
    char fileName[MAX_PATH];
    bool testMode;
};

DWORD WINAPI FlashThread(LPVOID param) {
    FlashParams* p = (FlashParams*)param;
    bool success = CAN_FirmwareUpgrade(g_canManager, p->fileName, p->testMode);
    delete p;
    PostMessage(g_hMainWindow, WM_COMMAND, IDM_FLASH_COMPLETE, success ? 1 : 0);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(0, 0, 0));
            SetBkMode(hdcStatic, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDC_BUTTON_CONNECT: {
                    if (!g_canManager) {
                        g_canManager = CAN_Create();
                        CAN_SetStatusCallback(g_canManager, OnStatusCallback, NULL);
                        CAN_SetErrorCallback(g_canManager, OnErrorCallback, NULL);
                        CAN_SetProgressCallback(g_canManager, OnProgressCallback, NULL);
                        CAN_SetVersionCallback(g_canManager, OnVersionCallback, NULL);
                        CAN_SetConnectedCallback(g_canManager, OnConnectedCallback, NULL);
                    }

                    if (CAN_IsConnected(g_canManager)) {
                        CAN_Disconnect(g_canManager);
                    } else {
                        wchar_t channel[32];
                        GetWindowTextW(g_hChannelCombo, channel, 32);
                        int baudRate = BAUD_RATES[SendMessage(g_hBaudRateCombo, CB_GETCURSEL, 0, 0)];

                        // 固定使用 PCAN 接口
                        const char* ifaceA = "PCAN";
                        char channelA[32];
                        WideCharToMultiByte(CP_ACP, 0, channel, -1, channelA, 32, NULL, NULL);

                        CAN_RESULT result = CAN_Connect(g_canManager, ifaceA, channelA, baudRate);
                        if (result != CAN_OK) {
                            wchar_t msg[64];
                            wsprintfW(msg, L"连接失败: 错误码 %d", result);
                            AppendLog(msg);
                        }
                    }
                    break;
                }

                case IDC_BUTTON_BROWSE: {
                    wchar_t fileName[MAX_PATH] = L"";
                    OPENFILENAMEW ofn = {sizeof(OPENFILENAMEW)};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"固件文件 (*.bin)\0*.bin\0所有文件 (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                    ofn.lpstrTitle = L"选择固件文件";
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(g_hFirmwareEdit, fileName);
                    }
                    break;
                }

                case IDC_BUTTON_FLASH: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        break;
                    }
                    wchar_t fileName[MAX_PATH];
                    GetWindowTextW(g_hFirmwareEdit, fileName, MAX_PATH);
                    if (fileName[0] == 0) {
                        AppendLog(L"错误: 请先选择固件文件");
                        break;
                    }
                    char fileNameA[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, fileName, -1, fileNameA, MAX_PATH, NULL, NULL);
                    bool testMode = SendMessage(g_hTestModeCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;

                    AppendLog(L"开始升级固件...");
                    if (testMode) AppendLog(L"模式: 测试模式");
                    EnableWindow(g_hFlashButton, FALSE);

                    FlashParams* p = new FlashParams;
                    strcpy(p->fileName, fileNameA);
                    p->testMode = testMode;
                    CreateThread(NULL, 0, FlashThread, p, 0, NULL);
                    break;
                }

                case IDC_BUTTON_GETVERSION: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        break;
                    }
                    AppendLog(L"正在查询固件版本...");
                    if (!CAN_FirmwareVersion(g_canManager)) {
                        AppendLog(L"查询固件版本失败");
                    }
                    break;
                }

                case IDC_BUTTON_REBOOT: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        break;
                    }
                    if (MessageBoxW(hwnd, L"确认要重启板卡吗?", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        if (CAN_BoardReboot(g_canManager)) {
                            AppendLog(L"重启命令已发送");
                        } else {
                            AppendLog(L"发送重启命令失败");
                        }
                    }
                    break;
                }

                case IDC_BUTTON_REFRESH: {
                    AppendLog(L"正在扫描PCAN设备...");
                    int prevCount = SendMessage(g_hChannelCombo, CB_GETCOUNT, 0, 0);
                    RefreshDeviceList();
                    int newCount = SendMessage(g_hChannelCombo, CB_GETCOUNT, 0, 0);
                    wchar_t msg[64];
                    if (newCount == 0)
                        wsprintfW(msg, L"设备列表已刷新，未发现CAN设备");
                    else
                        wsprintfW(msg, L"设备列表已刷新，发现 %d 个CAN设备", newCount);
                    AppendLog(msg);
                    break;
                }

                case IDM_FLASH_COMPLETE:
                    EnableWindow(g_hFlashButton, TRUE);
                    AppendLog(wParam ? L"固件升级成功!" : L"固件升级失败");
                    break;

                case IDC_BUTTON_CLEAR_LOG: {
                    ShowWindow(g_hStatusLog, SW_HIDE);
                    SetWindowTextW(g_hStatusLog, L"");
                    RedrawWindow(g_hStatusLog, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
                    ShowWindow(g_hStatusLog, SW_SHOW);
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY:
            if (g_canManager) CAN_Destroy(g_canManager);
            PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (g_hStatusLog) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int x = ScaleValue(20, g_dpi);
                int y = ScaleValue(390, g_dpi);
                int w = rc.right - ScaleValue(40, g_dpi);
                int h = rc.bottom - ScaleValue(405, g_dpi);
                SetWindowPos(g_hStatusLog, NULL, x, y, w, h, SWP_NOZORDER);
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// DPI 缩放宏（在 WinMain 中使用）
#define SCALE(v) ScaleValue(v, g_dpi)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // 启用 DPI 感知，解决高 DPI 下字体模糊问题
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_hInstance = hInstance;
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CANFirmwareApp";
    RegisterClassExW(&wc);

    // 获取系统 DPI（使用默认显示器）
    HDC hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);

    // 根据 DPI 缩放窗口尺寸
    int scaledWidth = ScaleValue(WINDOW_WIDTH, g_dpi);
    int scaledHeight = ScaleValue(WINDOW_HEIGHT, g_dpi);

    g_hMainWindow = CreateWindowExW(0, L"CANFirmwareApp", L"CAN固件升级工具",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, scaledWidth, scaledHeight,
        NULL, NULL, hInstance, NULL);

    // 连接设置
    CreateWindowExW(0, L"BUTTON", L"连接设置", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        SCALE(10), SCALE(10), SCALE(700), SCALE(60), g_hMainWindow, (HMENU)ID_GROUP_CONNECTION, hInstance, NULL);

    CreateWindowExW(0, L"STATIC", L"设备:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(30), SCALE(30), SCALE(50), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hChannelCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        SCALE(80), SCALE(28), SCALE(100), 200, g_hMainWindow, (HMENU)IDC_COMBO_CHANNEL, hInstance, NULL);
    // Auto-detect and populate devices on startup
    RefreshDeviceList();

    CreateWindowExW(0, L"STATIC", L"波特率:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(200), SCALE(30), SCALE(60), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hBaudRateCombo = CreateWindowExW(0, L"COMBOBOX", NULL,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        SCALE(265), SCALE(28), SCALE(100), 200, g_hMainWindow, (HMENU)IDC_COMBO_BAUDRATE, hInstance, NULL);
    const wchar_t* baudNames[] = {L"10K", L"20K", L"50K", L"83K", L"125K", L"250K", L"500K", L"1000K"};
    for (int i = 0; i < 8; i++) SendMessageW(g_hBaudRateCombo, CB_ADDSTRING, 0, (LPARAM)baudNames[i]);
    SendMessageW(g_hBaudRateCombo, CB_SETCURSEL, 5, 0);

    CreateWindowExW(0, L"BUTTON", L"刷新设备列表",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(385), SCALE(25), SCALE(100), SCALE(30), g_hMainWindow, (HMENU)IDC_BUTTON_REFRESH, hInstance, NULL);
    g_hConnectButton = CreateWindowExW(0, L"BUTTON", L"连接",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(520), SCALE(25), SCALE(80), SCALE(30), g_hMainWindow, (HMENU)IDC_BUTTON_CONNECT, hInstance, NULL);

    // 固件升级
    CreateWindowExW(0, L"BUTTON", L"固件升级", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        SCALE(10), SCALE(80), SCALE(700), SCALE(150), g_hMainWindow, (HMENU)ID_GROUP_FIRMWARE, hInstance, NULL);
    CreateWindowExW(0, L"STATIC", L"固件文件:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(30), SCALE(110), SCALE(85), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hFirmwareEdit = CreateWindowExW(0, L"EDIT", NULL,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER | ES_READONLY,
        SCALE(120), SCALE(107), SCALE(435), SCALE(25), g_hMainWindow, (HMENU)IDC_EDIT_FIRMWARE, hInstance, NULL);
    g_hBrowseButton = CreateWindowExW(0, L"BUTTON", L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(565), SCALE(105), SCALE(80), SCALE(28), g_hMainWindow, (HMENU)IDC_BUTTON_BROWSE, hInstance, NULL);
    g_hTestModeCheck = CreateWindowExW(0, L"BUTTON", L"测试模式(第二次重启后恢复原固件)",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        SCALE(30), SCALE(145), SCALE(300), SCALE(20), g_hMainWindow, (HMENU)IDC_CHECK_TESTMODE, hInstance, NULL);
    CreateWindowExW(0, L"STATIC", L"进度:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(30), SCALE(175), SCALE(50), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hProgressBar = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        SCALE(80), SCALE(175), SCALE(400), SCALE(20), g_hMainWindow, (HMENU)IDC_PROGRESS, hInstance, NULL);
    SendMessage(g_hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    g_hFlashButton = CreateWindowExW(0, L"BUTTON", L"开始升级",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(520), SCALE(160), SCALE(100), SCALE(40), g_hMainWindow, (HMENU)IDC_BUTTON_FLASH, hInstance, NULL);
    EnableWindow(g_hFlashButton, FALSE);

    // 板卡命令
    CreateWindowExW(0, L"BUTTON", L"板卡命令", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        SCALE(10), SCALE(240), SCALE(700), SCALE(100), g_hMainWindow, (HMENU)ID_GROUP_COMMANDS, hInstance, NULL);
    CreateWindowExW(0, L"STATIC", L"固件版本:", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(30), SCALE(270), SCALE(80), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hVersionLabel = CreateWindowExW(0, L"STATIC", L"未连接", WS_CHILD | WS_VISIBLE | SS_LEFT,
        SCALE(120), SCALE(270), SCALE(150), SCALE(20), g_hMainWindow, NULL, hInstance, NULL);
    g_hGetVersionButton = CreateWindowExW(0, L"BUTTON", L"获取版本",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(300), SCALE(260), SCALE(100), SCALE(35), g_hMainWindow, (HMENU)IDC_BUTTON_GETVERSION, hInstance, NULL);
    g_hRebootButton = CreateWindowExW(0, L"BUTTON", L"重启板卡",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(420), SCALE(260), SCALE(100), SCALE(35), g_hMainWindow, (HMENU)IDC_BUTTON_REBOOT, hInstance, NULL);
    EnableWindow(g_hGetVersionButton, FALSE);
    EnableWindow(g_hRebootButton, FALSE);

    // 日志
    CreateWindowExW(0, L"BUTTON", L"状态日志", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        SCALE(10), SCALE(375), SCALE(700), SCALE(210), g_hMainWindow, (HMENU)ID_GROUP_LOG, hInstance, NULL);
    g_hStatusLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        SCALE(20), SCALE(390), SCALE(530), SCALE(180), g_hMainWindow, (HMENU)IDC_EDIT_LOG, hInstance, NULL);
    // 清空按钮放在日志框右下角
    g_hClearLogButton = CreateWindowExW(0, L"BUTTON", L"清空日志",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        SCALE(560), SCALE(540), SCALE(90), SCALE(28), g_hMainWindow, (HMENU)IDC_BUTTON_CLEAR_LOG, hInstance, NULL);

    // 创建全局字体（用于所有控件）
    int appFontSize = ScaleValue(14, g_dpi);
    g_hAppFont = CreateFontW(-appFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");

    // 日志框字体（更小）
    int logFontSize = ScaleValue(11, g_dpi);
    HFONT hLogFont = CreateFontW(-logFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    SendMessage(g_hStatusLog, WM_SETFONT, (WPARAM)hLogFont, TRUE);

    // 为所有子窗口设置全局字体
    EnumChildWindows(g_hMainWindow, [](HWND hwnd, LPARAM lParam) -> BOOL {
        if (hwnd != g_hStatusLog) {  // 跳过日志框
            SendMessage(hwnd, WM_SETFONT, (WPARAM)g_hAppFont, TRUE);
        }
        return TRUE;
    }, 0);

    // 确保清空按钮在最前面
    SetWindowPos(g_hClearLogButton, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    AppendLog(L"CAN固件升级工具已启动");
    AppendLog(L"请选择CAN设备和波特率，然后点击连接");

    ShowWindow(g_hMainWindow, nCmdShow);
    UpdateWindow(g_hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理字体资源
    if (g_hAppFont) DeleteObject(g_hAppFont);
    return 0;
}
