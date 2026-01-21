#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "can_manager.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

// 全局变量
CANManager* g_canManager = NULL;
HWND g_hDialog = NULL;

static const int BAUD_RATES[] = {10000, 20000, 50000, 80000, 125000, 250000, 500000, 1000000};

// 日志函数
void AppendLog(const wchar_t* msg) {
    HWND hLog = GetDlgItem(g_hDialog, IDC_EDIT_LOG);
    int len = GetWindowTextLength(hLog);
    SendMessage(hLog, EM_SETSEL, len, len);
    SendMessage(hLog, EM_REPLACESEL, FALSE, (LPARAM)msg);
    SendMessage(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessage(hLog, EM_SETSEL, -1, -1);
}

void ClearLog() {
    HWND hLog = GetDlgItem(g_hDialog, IDC_EDIT_LOG);
    SetWindowTextW(hLog, L"");
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
    HWND hProgress = GetDlgItem(g_hDialog, IDC_PROGRESS);
    SendMessage(hProgress, PBM_SETPOS, pct, 0);
}

void OnVersionCallback(const char* ver, void*) {
    HWND hVersion = GetDlgItem(g_hDialog, IDC_LABEL_VERSION);
    wchar_t wver[64];
    MultiByteToWideChar(CP_UTF8, 0, ver, -1, wver, 64);
    SetWindowTextW(hVersion, wver);
}

void OnConnectedCallback(bool connected, void*) {
    EnableWindow(GetDlgItem(g_hDialog, IDC_BUTTON_FLASH), connected);
    EnableWindow(GetDlgItem(g_hDialog, IDC_BUTTON_GETVERSION), connected);
    EnableWindow(GetDlgItem(g_hDialog, IDC_BUTTON_REBOOT), connected);
    EnableWindow(GetDlgItem(g_hDialog, IDC_COMBO_CHANNEL), !connected);
    EnableWindow(GetDlgItem(g_hDialog, IDC_COMBO_BAUDRATE), !connected);
    SetWindowTextW(GetDlgItem(g_hDialog, IDC_BUTTON_CONNECT), connected ? L"断开连接" : L"连接");
    if (!connected) {
        HWND hProgress = GetDlgItem(g_hDialog, IDC_PROGRESS);
        SendMessage(hProgress, PBM_SETPOS, 0, 0);
        SetWindowTextW(GetDlgItem(g_hDialog, IDC_LABEL_VERSION), L"未连接");
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
    PostMessage(g_hDialog, WM_COMMAND, IDM_FLASH_COMPLETE, success ? 1 : 0);
    return 0;
}

INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            g_hDialog = hwnd;

            // 设置图标
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // 初始化设备下拉框
            HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"USB0");
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"USB1");
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"USB2");
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"USB3");
            SendMessage(hChannel, CB_SETCURSEL, 0, 0);

            // 初始化波特率下拉框
            HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
            const wchar_t* baudNames[] = {L"10K", L"20K", L"50K", L"83K", L"125K", L"250K", L"500K", L"1000K"};
            for (int i = 0; i < 8; i++) {
                SendMessage(hBaudRate, CB_ADDSTRING, 0, (LPARAM)baudNames[i]);
            }
            SendMessage(hBaudRate, CB_SETCURSEL, 5, 0);  // 默认 250K

            // 初始化进度条
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
            SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hProgress, PBM_SETPOS, 0, 0);

            // 初始化按钮状态
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);

            AppendLog(L"CAN固件升级工具已启动");
            AppendLog(L"请选择CAN设备和波特率，然后点击连接");
            return TRUE;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDCANCEL:
                case IDOK:
                    DestroyWindow(hwnd);
                    PostQuitMessage(0);
                    return TRUE;

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
                        HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);
                        HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
                        wchar_t channel[32];
                        GetWindowTextW(hChannel, channel, 32);
                        int baudRate = BAUD_RATES[SendMessageW(hBaudRate, CB_GETCURSEL, 0, 0)];

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
                    return TRUE;
                }

                case IDM_FILE_OPEN:
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
                        SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_FIRMWARE), fileName);
                    }
                    return TRUE;
                }

                case IDC_BUTTON_FLASH: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        return TRUE;
                    }
                    HWND hFirmware = GetDlgItem(hwnd, IDC_EDIT_FIRMWARE);
                    HWND hTestMode = GetDlgItem(hwnd, IDC_CHECK_TESTMODE);
                    wchar_t fileName[MAX_PATH];
                    GetWindowTextW(hFirmware, fileName, MAX_PATH);
                    if (fileName[0] == 0) {
                        AppendLog(L"错误: 请先选择固件文件");
                        return TRUE;
                    }
                    char fileNameA[MAX_PATH];
                    WideCharToMultiByte(CP_ACP, 0, fileName, -1, fileNameA, MAX_PATH, NULL, NULL);
                    bool testMode = SendMessage(hTestMode, BM_GETCHECK, 0, 0) == BST_CHECKED;

                    AppendLog(L"开始升级固件...");
                    if (testMode) AppendLog(L"模式: 测试模式");
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);

                    FlashParams* p = new FlashParams;
                    strcpy(p->fileName, fileNameA);
                    p->testMode = testMode;
                    CreateThread(NULL, 0, FlashThread, p, 0, NULL);
                    return TRUE;
                }

                case IDC_BUTTON_GETVERSION: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        return TRUE;
                    }
                    AppendLog(L"正在查询固件版本...");
                    if (!CAN_FirmwareVersion(g_canManager)) {
                        AppendLog(L"查询固件版本失败");
                    }
                    return TRUE;
                }

                case IDC_BUTTON_REBOOT: {
                    if (!g_canManager || !CAN_IsConnected(g_canManager)) {
                        AppendLog(L"错误: CAN未连接");
                        return TRUE;
                    }
                    if (MessageBoxW(hwnd, L"确认要重启板卡吗?", L"确认", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        if (CAN_BoardReboot(g_canManager)) {
                            AppendLog(L"重启命令已发送");
                        } else {
                            AppendLog(L"发送重启命令失败");
                        }
                    }
                    return TRUE;
                }

                case IDC_BUTTON_REFRESH: {
                    AppendLog(L"正在扫描PCAN设备...");
                    const wchar_t* channels[] = {L"USB0", L"USB1", L"USB2", L"USB3"};
                    const char* channelsA[] = {"usb0", "usb1", "usb2", "usb3"};
                    bool found = false;

                    for (int i = 0; i < 4; i++) {
                        if (CAN_DetectDevice(channelsA[i])) {
                            wchar_t msg[64];
                            wsprintfW(msg, L"发现设备: %s", channels[i]);
                            AppendLog(msg);
                            found = true;
                        }
                    }

                    if (!found) {
                        AppendLog(L"未发现PCAN设备，请检查连接");
                    }
                    return TRUE;
                }

                case IDM_FLASH_COMPLETE:
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), TRUE);
                    AppendLog(wParam ? L"固件升级成功!" : L"固件升级失败");
                    return TRUE;

                case IDM_FILE_EXIT:
                    DestroyWindow(hwnd);
                    PostQuitMessage(0);
                    return TRUE;

                case IDM_EDIT_CLEARLOG:
                case IDC_BUTTON_CLEAR_LOG: {
                    HWND hLog = GetDlgItem(hwnd, IDC_EDIT_LOG);
                    ShowWindow(hLog, SW_HIDE);
                    SetWindowTextW(hLog, L"");
                    RedrawWindow(hLog, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
                    ShowWindow(hLog, SW_SHOW);
                    return TRUE;
                }

                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                        L"CAN固件升级工具 v1.0\n\n"
                        L"用于通过 PCAN 接口升级板卡固件\n\n"
                        L"功能特性:\n"
                        L"• 支持多种波特率\n"
                        L"• 固件升级与测试模式\n"
                        L"• 版本查询与板卡重启",
                        L"关于",
                        MB_OK | MB_ICONINFORMATION);
                    return TRUE;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            return TRUE;

        case WM_DESTROY:
            if (g_canManager) CAN_Destroy(g_canManager);
            g_canManager = NULL;
            return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // 启用 DPI 感知
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);

    // 创建模式对话框
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);

    return 0;
}
