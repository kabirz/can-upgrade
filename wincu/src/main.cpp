#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <resource.h>
#include <can_manager.h>

// 自定义消息定义
#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)

static HWND hLog;
static bool isConnected = false;
static bool isUpdating = false;
static HWND hUpdatingDialog = nullptr;

// 进度回调函数
static void OnProgressUpdate(int percent) {
    if (hUpdatingDialog) {
        PostMessage(hUpdatingDialog, WM_UPDATE_PROGRESS, percent, 0);
    }
}

// 固件更新线程参数
typedef struct {
    HWND hwnd;
    char fileName[MAX_PATH];
    bool testMode;
} FirmwareUpdateParams;

static const int BAUD_RATES[] = {
    PCAN_BAUD_10K, PCAN_BAUD_20K, PCAN_BAUD_50K, PCAN_BAUD_100K,
    PCAN_BAUD_125K, PCAN_BAUD_250K, PCAN_BAUD_500K, PCAN_BAUD_1M
};
static const wchar_t* baudNames[] = {
    L"10K", L"20K", L"50K", L"100K",
    L"125K", L"250K", L"500K", L"1000K"
};

void AppendLog(const char* msg) {
    if (!hLog) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, -1, nullptr, 0);
    wchar_t* wstr = new wchar_t[wlen];
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wstr, wlen);

    SYSTEMTIME st;
    wchar_t timestamp[16];

    GetLocalTime(&st);
    wsprintfW(timestamp, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);

    int len = GetWindowTextLengthW(hLog);

    SendMessageW(hLog, EM_SETSEL, len, len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)timestamp);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)wstr);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hLog, EM_SETSEL, -1, -1);

    delete[] wstr;
}

DWORD WINAPI FirmwareUpdateThread(LPVOID lpParam) {
    FirmwareUpdateParams* params = (FirmwareUpdateParams*)lpParam;

    hUpdatingDialog = params->hwnd;

    CanManager::getInstance().setProgressCallback(OnProgressUpdate);
    bool success = CanManager::getInstance().firmwareUpgrade(params->fileName, params->testMode);
    PostMessage(params->hwnd, WM_UPDATE_COMPLETE, success, 0);

    hUpdatingDialog = nullptr;
    delete params;
    return 0;
}

static TPCANHandle g_channels[MAX_DEVICES];
static int g_channelCount = 0;

void getDeviceList(HWND hwnd) {
    wchar_t buf[64];
    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);
    CanManager::getInstance().setCallback(AppendLog);
    g_channelCount = CanManager::getInstance().detectDevice(g_channels, MAX_DEVICES);

    SendMessage(hChannel, CB_RESETCONTENT, 0, 0);

    for (int i = 0; i < g_channelCount; i++) {
        wsprintfW(buf, L"PCAN-USB: %xh", g_channels[i]);
        SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendMessage(hChannel, CB_SETCURSEL, 0, 0);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // 设置图标
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // 初始化波特率下拉框
            HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
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

            hLog = GetDlgItem(hwnd, IDC_EDIT_LOG);

            // 获取设备列表
            getDeviceList(hwnd);
            return TRUE;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDCANCEL:
                case IDOK:
                    DestroyWindow(hwnd);
                    PostQuitMessage(0);
                    return TRUE;

                case IDM_FILE_EXIT:
                    DestroyWindow(hwnd);
                    PostQuitMessage(0);
                    return TRUE;

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

                case IDM_EDIT_CLEARLOG:
                case IDC_BUTTON_CLEAR_LOG: {
                    ShowWindow(hLog, SW_HIDE);
                    SetWindowTextW(hLog, L"");
                    RedrawWindow(hLog, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
                    ShowWindow(hLog, SW_SHOW);
                    return TRUE;
                }
                case IDC_BUTTON_REFRESH:
                    getDeviceList(hwnd);
                    return TRUE;
                case IDC_BUTTON_CONNECT: {
                    HWND hConnectBtn = GetDlgItem(hwnd, IDC_BUTTON_CONNECT);
                    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);
                    HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);

                    if (isConnected) {
                        // 断开连接
                        CanManager::getInstance().disconnect();
                        isConnected = false;
                        SetWindowTextW(hConnectBtn, L"连接");
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REFRESH), TRUE);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), L"固件版本: 未连接");
                        EnableWindow(hChannel, TRUE);
                        EnableWindow(hBaudRate, TRUE);
                        return TRUE;
                    } else {
                        // 连接
                        int cnl_idx = SendMessage(hChannel, CB_GETCURSEL, 0, 0);
                        int baud_idx = SendMessage(hBaudRate, CB_GETCURSEL, 0, 0);
                        if (cnl_idx >= 0 && cnl_idx < g_channelCount && baud_idx >= 0) {
                            bool res = CanManager::getInstance().connect(g_channels[cnl_idx], BAUD_RATES[baud_idx]);
                            if (res) {
                                isConnected = true;
                                SetWindowTextW(hConnectBtn, L"断开");
                                EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), TRUE);
                                EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), TRUE);
                                EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), TRUE);
                                EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REFRESH), FALSE);
                                EnableWindow(hChannel, FALSE);
                                EnableWindow(hBaudRate, FALSE);
                                return TRUE;
                            }
                        }
                        MessageBoxW(hwnd,
                                    L"连接失败\n\n"
                                    L"请查看设备是否接入\n或者设备是否被被其他程序占用",
                                    L"CAN 连接失败", MB_OK | MB_ICONWARNING 
                                );
                        return FALSE;
                    }
                }
                case IDC_BUTTON_GETVERSION: {
                    uint32_t version = CanManager::getInstance().getFirmwareVersion();
                    if (version) {
                        wchar_t verMsg[32];
                        wsprintfW(verMsg, L"固件版本: v%u.%u.%u", (version >> 24) & 0xFF,
                              (version >> 16) & 0xFF, (version >> 8) & 0xFF);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), verMsg);
                    }
                    return TRUE;
                }
                case IDC_BUTTON_REBOOT: {
                    int result = MessageBoxW(hwnd,
                                L"确认要重启板卡吗？", L"确认重启",
                                MB_OKCANCEL | MB_ICONINFORMATION
                            );
                    if (result == IDOK) {
                        bool status = CanManager::getInstance().boardReboot();
                        if (status)
                            AppendLog("等待重启完成");
                    }
                    return TRUE;
                }
                case IDC_BUTTON_FLASH: {
                    wchar_t fileName[MAX_PATH];
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_FIRMWARE), fileName, MAX_PATH);

                    if (wcslen(fileName) == 0) {
                        MessageBoxW(hwnd, L"请先选择固件文件", L"提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (isUpdating) {
                        MessageBoxW(hwnd, L"固件更新中，请等待完成", L"提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    HWND hTestMode = GetDlgItem(hwnd, IDC_CHECK_TESTMODE);
                    bool testMode = SendMessage(hTestMode, BM_GETCHECK, 0, 0) == BST_CHECKED;

                    isUpdating = true;
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), FALSE);

                    SendMessage(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);

                    FirmwareUpdateParams* params = new FirmwareUpdateParams;
                    params->hwnd = hwnd;
                    WideCharToMultiByte(CP_UTF8, 0, fileName, -1, params->fileName, MAX_PATH, NULL, NULL);
                    params->testMode = testMode;

                    DWORD threadId;
                    HANDLE hThread = CreateThread(NULL, 0, FirmwareUpdateThread, params, 0, &threadId);
                    if (hThread) {
                        CloseHandle(hThread);
                    } else {
                        delete params;
                        isUpdating = false;
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);
                        MessageBoxW(hwnd, L"创建更新线程失败", L"错误", MB_OK | MB_ICONERROR);
                    }
                    return TRUE;
                }
            }
            return FALSE;
        }

        case WM_UPDATE_PROGRESS: {
            // 更新进度条
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
            if (hProgress) {
                SendMessage(hProgress, PBM_SETPOS, wParam, 0);
            }
            return TRUE;
        }

        case WM_UPDATE_COMPLETE: {
            // 固件更新完成
            bool success = (wParam != 0);
            isUpdating = false;

            // 恢复控件状态
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);

            // 显示结果
            if (success) {
                MessageBoxW(hwnd, L"固件升级完成！请重启板卡", L"成功", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"固件升级失败，请查看日志", L"失败", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }

        case WM_CLOSE:
            // 如果正在更新，提示用户
            if (isUpdating) {
                int result = MessageBoxW(hwnd,
                    L"固件更新正在进行中，确定要退出吗？",
                    L"警告",
                    MB_YESNO | MB_ICONWARNING);
                if (result != IDYES) {
                    return TRUE;
                }
            }
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            return TRUE;

        case WM_DESTROY:
            PostQuitMessage(0);
            return TRUE;
    }
    return FALSE;
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // 创建模式对话框
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);

    return 0;
}
