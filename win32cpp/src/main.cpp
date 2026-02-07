#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <resource.h>
#include <can_manager.h>
#include <uart_manager.h>

// 传输模式
#define TRANSPORT_MODE_CAN    0
#define TRANSPORT_MODE_UART   1

// 自定义消息定义
#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)

static int transportMode = TRANSPORT_MODE_CAN;  // 当前传输模式
static UartManager* g_uartManager = nullptr;

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
    wchar_t fileName[MAX_PATH];
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

// UART 波特率配置
static const DWORD UART_BAUD_RATES[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};
static const wchar_t* uartBaudNames[] = {
    L"9600", L"19200", L"38400", L"57600", L"115200", L"230400", L"460800", L"921600"
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

// 更新开始升级按钮状态
static void UpdateFlashButtonState(HWND hwnd) {
    HWND hFlashBtn = GetDlgItem(hwnd, IDC_BUTTON_FLASH);
    HWND hFirmwareEdit = GetDlgItem(hwnd, IDC_EDIT_FIRMWARE);
    wchar_t fileName[MAX_PATH];

    GetWindowTextW(hFirmwareEdit, fileName, MAX_PATH);
    bool hasFile = (wcslen(fileName) > 0);

    EnableWindow(hFlashBtn, isConnected && hasFile && !isUpdating);
}

DWORD WINAPI FirmwareUpdateThread(LPVOID lpParam) {
    FirmwareUpdateParams* params = (FirmwareUpdateParams*)lpParam;

    hUpdatingDialog = params->hwnd;

    bool success = false;
    if (transportMode == TRANSPORT_MODE_CAN) {
        CanManager::getInstance().setProgressCallback(OnProgressUpdate);
        success = CanManager::getInstance().firmwareUpgrade(params->fileName, params->testMode);
    } else {
        UartManager_SetProgressCallback(g_uartManager, OnProgressUpdate);
        success = UartManager_FirmwareUpgrade(g_uartManager, params->fileName, params->testMode) != 0;
    }
    PostMessage(params->hwnd, WM_UPDATE_COMPLETE, success, 0);

    hUpdatingDialog = nullptr;
    delete params;
    return 0;
}

static TPCANHandle g_channels[MAX_DEVICES];
static int g_channelCount = 0;

// UART 串口信息数组
static SerialPortInfo g_serialPorts[MAX_SERIAL_PORTS];
static int g_serialPortCount = 0;

void getDeviceList(HWND hwnd) {
    wchar_t buf[128];
    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);

    SendMessage(hChannel, CB_RESETCONTENT, 0, 0);

    if (transportMode == TRANSPORT_MODE_CAN) {
        CanManager::getInstance().setCallback(AppendLog);

        // 检测真实 CAN 设备
        g_channelCount = CanManager::getInstance().detectDevice(g_channels, MAX_DEVICES);

        // 添加真实设备到列表
        for (int i = 0; i < g_channelCount; i++) {
            wsprintfW(buf, L"PCAN-USB: %xh", g_channels[i]);
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)buf);
        }

        // 将虚拟 CAN 添加到列表末尾
        if (g_channelCount < MAX_DEVICES) {
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"虚拟 CAN (测试模式)");
            g_channels[g_channelCount] = VIRTUAL_CAN_CHANNEL;
            g_channelCount++;
        }
    } else {
        // UART 模式
        UartManager_SetCallback(g_uartManager, AppendLog);
        g_serialPortCount = UartManager_EnumPorts(g_uartManager, g_serialPorts, MAX_SERIAL_PORTS);

        for (int i = 0; i < g_serialPortCount; i++) {
            MultiByteToWideChar(CP_UTF8, 0, g_serialPorts[i].friendlyName, -1, buf, 128);
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)buf);
        }
    }

    if (SendMessage(hChannel, CB_GETCOUNT, 0, 0) > 0) {
        SendMessage(hChannel, CB_SETCURSEL, 0, 0);
    }
}

// 更新传输模式界面
void UpdateTransportModeUI(HWND hwnd) {
    HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
    HWND hUartBaudRate = GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE);
    HWND hRefresh = GetDlgItem(hwnd, IDC_BUTTON_REFRESH);

    if (transportMode == TRANSPORT_MODE_CAN) {
        // CAN 模式
        ShowWindow(hBaudRate, SW_SHOW);
        ShowWindow(hUartBaudRate, SW_HIDE);
    } else {
        // UART 模式
        ShowWindow(hBaudRate, SW_HIDE);
        ShowWindow(hUartBaudRate, SW_SHOW);
    }
    EnableWindow(hRefresh, !isConnected);  // 两种模式都支持刷新

    // 更新菜单项勾选状态
    CheckMenuRadioItem(GetMenu(hwnd), IDM_EDIT_TRANSPORT_CAN, IDM_EDIT_TRANSPORT_UART,
                       transportMode == TRANSPORT_MODE_UART ? IDM_EDIT_TRANSPORT_UART : IDM_EDIT_TRANSPORT_CAN,
                       MF_BYCOMMAND);

    // 刷新设备列表
    getDeviceList(hwnd);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // 设置图标
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // 初始化 CAN 波特率下拉框
            HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
            for (int i = 0; i < 8; i++) {
                SendMessage(hBaudRate, CB_ADDSTRING, 0, (LPARAM)baudNames[i]);
            }
            SendMessage(hBaudRate, CB_SETCURSEL, 5, 0);  // 默认 250K

            // 初始化 UART 波特率下拉框
            HWND hUartBaudRate = GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE);
            for (int i = 0; i < 8; i++) {
                SendMessage(hUartBaudRate, CB_ADDSTRING, 0, (LPARAM)uartBaudNames[i]);
            }
            SendMessage(hUartBaudRate, CB_SETCURSEL, 4, 0);  // 默认 115200

            // 初始化进度条
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
            SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hProgress, PBM_SETPOS, 0, 0);

            // 初始化按钮状态
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);

            hLog = GetDlgItem(hwnd, IDC_EDIT_LOG);

            // 初始化传输模式界面
            UpdateTransportModeUI(hwnd);

            // 初始化开始升级按钮状态
            UpdateFlashButtonState(hwnd);

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
                        UpdateFlashButtonState(hwnd);
                    }
                    return TRUE;
                }

                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                        L"固件升级工具 v1.1\n\n"
                        L"支持 CAN/UART 双总线固件升级\n\n"
                        L"功能特性:\n"
                        L"• CAN 总线升级（PCAN 接口）\n"
                        L"• UART 串口升级\n"
                        L"• 固件升级与测试模式\n"
                        L"• 版本查询与板卡重启\n"
                        L"• 自动过滤蓝牙虚拟串口",
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
                case IDM_EDIT_TRANSPORT_CAN:
                case IDM_EDIT_TRANSPORT_UART: {
                    if (isConnected) {
                        MessageBoxW(hwnd, L"请先断开当前连接", L"提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    // 切换传输模式
                    transportMode = (LOWORD(wParam) == IDM_EDIT_TRANSPORT_UART) ? TRANSPORT_MODE_UART : TRANSPORT_MODE_CAN;
                    UpdateTransportModeUI(hwnd);
                    return TRUE;
                }
                case IDC_BUTTON_CONNECT: {
                    HWND hConnectBtn = GetDlgItem(hwnd, IDC_BUTTON_CONNECT);
                    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);

                    if (isConnected) {
                        // 断开连接
                        EnableWindow(hConnectBtn, FALSE);
                        if (transportMode == TRANSPORT_MODE_CAN) {
                            CanManager::getInstance().disconnect();
                        } else {
                            UartManager_Disconnect(g_uartManager);
                        }
                        EnableWindow(hConnectBtn, TRUE);
                        isConnected = false;
                        SetWindowTextW(hConnectBtn, L"连接");
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REFRESH), TRUE);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), L"固件版本: 未获取");
                        EnableWindow(hChannel, TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_BAUDRATE), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE), TRUE);
                        UpdateFlashButtonState(hwnd);
                        return TRUE;
                    } else {
                        // 连接
                        bool res = false;
                        if (transportMode == TRANSPORT_MODE_CAN) {
                            HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
                            int cnl_idx = SendMessage(hChannel, CB_GETCURSEL, 0, 0);
                            int baud_idx = SendMessage(hBaudRate, CB_GETCURSEL, 0, 0);
                            EnableWindow(hConnectBtn, FALSE);
                            if (cnl_idx >= 0 && cnl_idx < g_channelCount && baud_idx >= 0) {
                                res = CanManager::getInstance().connect(g_channels[cnl_idx], BAUD_RATES[baud_idx]);
                            }
                        } else {
                            HWND hUartBaudRate = GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE);
                            int port_idx = SendMessage(hChannel, CB_GETCURSEL, 0, 0);
                            int baud_idx = SendMessage(hUartBaudRate, CB_GETCURSEL, 0, 0);
                            EnableWindow(hConnectBtn, FALSE);
                            if (port_idx >= 0 && port_idx < g_serialPortCount && baud_idx >= 0) {
                                res = UartManager_Connect(g_uartManager, g_serialPorts[port_idx].portName, UART_BAUD_RATES[baud_idx]) != 0;
                            }
                        }

                        if (res) {
                            EnableWindow(hConnectBtn, TRUE);
                            isConnected = true;
                            SetWindowTextW(hConnectBtn, L"断开");
                            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), TRUE);
                            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), TRUE);
                            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REFRESH), FALSE);
                            EnableWindow(hChannel, FALSE);
                            EnableWindow(GetDlgItem(hwnd, IDC_COMBO_BAUDRATE), FALSE);
                            EnableWindow(GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE), FALSE);
                            UpdateFlashButtonState(hwnd);
                            return TRUE;
                        }
                        EnableWindow(hConnectBtn, TRUE);
                        MessageBoxW(hwnd,
                                    L"连接失败\n\n"
                                    L"请查看设备是否接入\n或者设备是否被其他程序占用",
                                    L"连接失败", MB_OK | MB_ICONWARNING
                                );
                        return FALSE;
                    }
                }
                case IDC_BUTTON_GETVERSION: {
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
                    uint32_t version = 0;
                    if (transportMode == TRANSPORT_MODE_CAN) {
                        version = CanManager::getInstance().getFirmwareVersion();
                    } else {
                        version = UartManager_GetFirmwareVersion(g_uartManager);
                    }
                    if (version) {
                        wchar_t verMsg[32];
                        wsprintfW(verMsg, L"固件版本: v%u.%u.%u", (version >> 24) & 0xFF,
                              (version >> 16) & 0xFF, (version >> 8) & 0xFF);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), verMsg);
                    }
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), TRUE);
                    return TRUE;
                }
                case IDC_BUTTON_REBOOT: {
                    int result = MessageBoxW(hwnd,
                                L"确认要重启板卡吗？", L"确认重启",
                                MB_OKCANCEL | MB_ICONINFORMATION
                            );
                    if (result == IDOK) {
                        bool status = false;
                        if (transportMode == TRANSPORT_MODE_CAN) {
                            status = CanManager::getInstance().boardReboot();
                        } else {
                            status = UartManager_BoardReboot(g_uartManager) != 0;
                        }
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
                    UpdateFlashButtonState(hwnd);
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), FALSE);
                    EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), FALSE);

                    SendMessage(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);

                    FirmwareUpdateParams* params = new FirmwareUpdateParams;
                    params->hwnd = hwnd;
                    wcscpy(params->fileName, fileName);
                    params->testMode = testMode;

                    DWORD threadId;
                    HANDLE hThread = CreateThread(NULL, 0, FirmwareUpdateThread, params, 0, &threadId);
                    if (hThread) {
                        CloseHandle(hThread);
                    } else {
                        delete params;
                        isUpdating = false;
                        UpdateFlashButtonState(hwnd);
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
            // 更新百分比文本
            HWND hPercent = GetDlgItem(hwnd, IDC_LABEL_PERCENT);
            if (hPercent) {
                wchar_t buf[16];
                wsprintfW(buf, L"%d%%", wParam);
                SetWindowTextW(hPercent, buf);
            }
            return TRUE;
        }

        case WM_UPDATE_COMPLETE: {
            // 固件更新完成
            bool success = (wParam != 0);
            isUpdating = false;

            // 恢复控件状态
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);
            UpdateFlashButtonState(hwnd);

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
    // 创建 UART 管理器
    g_uartManager = UartManager_Create();
    if (!g_uartManager) {
        MessageBoxW(NULL, L"无法创建UART管理器", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 创建模式对话框
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);

    // 清理资源
    UartManager_Destroy(g_uartManager);

    return 0;
}
