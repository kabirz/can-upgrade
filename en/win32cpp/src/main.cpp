#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <resource.h>
#include <can_manager.h>
#include <uart_manager.h>

// Transport mode
#define TRANSPORT_MODE_CAN    0
#define TRANSPORT_MODE_UART   1

// Custom messages
#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)

static int transportMode = TRANSPORT_MODE_CAN;  // Current transport mode
static UartManager* g_uartManager = nullptr;

static HWND hLog;
static bool isConnected = false;
static bool isUpdating = false;
static HWND hUpdatingDialog = nullptr;

// Progress callback function
static void OnProgressUpdate(int percent) {
    if (hUpdatingDialog) {
        PostMessage(hUpdatingDialog, WM_UPDATE_PROGRESS, percent, 0);
    }
}

// Firmware update thread parameters
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

// UART baud rate configuration
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

// Update start upgrade button state
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

// UART serial port info array
static SerialPortInfo g_serialPorts[MAX_SERIAL_PORTS];
static int g_serialPortCount = 0;

void getDeviceList(HWND hwnd) {
    wchar_t buf[128];
    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);

    SendMessage(hChannel, CB_RESETCONTENT, 0, 0);

    if (transportMode == TRANSPORT_MODE_CAN) {
        CanManager::getInstance().setCallback(AppendLog);

        // Detect real CAN devices
        g_channelCount = CanManager::getInstance().detectDevice(g_channels, MAX_DEVICES);

        // Add real devices to list
        for (int i = 0; i < g_channelCount; i++) {
            wsprintfW(buf, L"PCAN-USB: %xh", g_channels[i]);
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)buf);
        }

        // Add virtual CAN to end of list
        if (g_channelCount < MAX_DEVICES) {
            SendMessage(hChannel, CB_ADDSTRING, 0, (LPARAM)L"Virtual CAN (Test Mode)");
            g_channels[g_channelCount] = VIRTUAL_CAN_CHANNEL;
            g_channelCount++;
        }
    } else {
        // UART mode
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

// Update transport mode UI
void UpdateTransportModeUI(HWND hwnd) {
    HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
    HWND hUartBaudRate = GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE);
    HWND hRefresh = GetDlgItem(hwnd, IDC_BUTTON_REFRESH);

    if (transportMode == TRANSPORT_MODE_CAN) {
        // CAN mode
        ShowWindow(hBaudRate, SW_SHOW);
        ShowWindow(hUartBaudRate, SW_HIDE);
    } else {
        // UART mode
        ShowWindow(hBaudRate, SW_HIDE);
        ShowWindow(hUartBaudRate, SW_SHOW);
    }
    EnableWindow(hRefresh, !isConnected);  // Both modes support refresh

    // Update menu item check state
    CheckMenuRadioItem(GetMenu(hwnd), IDM_EDIT_TRANSPORT_CAN, IDM_EDIT_TRANSPORT_UART,
                       transportMode == TRANSPORT_MODE_UART ? IDM_EDIT_TRANSPORT_UART : IDM_EDIT_TRANSPORT_CAN,
                       MF_BYCOMMAND);

    // Refresh device list
    getDeviceList(hwnd);
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG: {
            // Set icon
            HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

            // Initialize CAN baud rate dropdown
            HWND hBaudRate = GetDlgItem(hwnd, IDC_COMBO_BAUDRATE);
            for (int i = 0; i < 8; i++) {
                SendMessage(hBaudRate, CB_ADDSTRING, 0, (LPARAM)baudNames[i]);
            }
            SendMessage(hBaudRate, CB_SETCURSEL, 5, 0);  // Default 250K

            // Initialize UART baud rate dropdown
            HWND hUartBaudRate = GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE);
            for (int i = 0; i < 8; i++) {
                SendMessage(hUartBaudRate, CB_ADDSTRING, 0, (LPARAM)uartBaudNames[i]);
            }
            SendMessage(hUartBaudRate, CB_SETCURSEL, 4, 0);  // Default 115200

            // Initialize progress bar
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
            SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(hProgress, PBM_SETPOS, 0, 0);

            // Initialize button state
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);

            hLog = GetDlgItem(hwnd, IDC_EDIT_LOG);

            // Initialize transport mode UI
            UpdateTransportModeUI(hwnd);

            // Initialize start upgrade button state
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
                    ofn.lpstrFilter = L"Firmware Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                    ofn.lpstrTitle = L"Select Firmware File";
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_FIRMWARE), fileName);
                        UpdateFlashButtonState(hwnd);
                    }
                    return TRUE;
                }

                case IDM_HELP_ABOUT:
                    MessageBoxW(hwnd,
                        L"Firmware Upgrade Tool v1.1\n\n"
                        L"CAN/UART Dual-Bus Firmware Upgrade\n\n"
                        L"Features:\n"
                        L"• CAN bus upgrade (PCAN interface)\n"
                        L"• UART serial upgrade\n"
                        L"• Firmware upgrade with test mode\n"
                        L"• Version query and board reboot\n"
                        L"• Auto-filter Bluetooth virtual serial ports",
                        L"About",
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
                        MessageBoxW(hwnd, L"Please disconnect current connection first", L"Tip", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    // Switch transport mode
                    transportMode = (LOWORD(wParam) == IDM_EDIT_TRANSPORT_UART) ? TRANSPORT_MODE_UART : TRANSPORT_MODE_CAN;
                    UpdateTransportModeUI(hwnd);
                    return TRUE;
                }
                case IDC_BUTTON_CONNECT: {
                    HWND hConnectBtn = GetDlgItem(hwnd, IDC_BUTTON_CONNECT);
                    HWND hChannel = GetDlgItem(hwnd, IDC_COMBO_CHANNEL);

                    if (isConnected) {
                        // Disconnect
                        EnableWindow(hConnectBtn, FALSE);
                        if (transportMode == TRANSPORT_MODE_CAN) {
                            CanManager::getInstance().disconnect();
                        } else {
                            UartManager_Disconnect(g_uartManager);
                        }
                        EnableWindow(hConnectBtn, TRUE);
                        isConnected = false;
                        SetWindowTextW(hConnectBtn, L"Connect");
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);
                        EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REFRESH), TRUE);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), L"Version: Not obtained");
                        EnableWindow(hChannel, TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_BAUDRATE), TRUE);
                        EnableWindow(GetDlgItem(hwnd, IDC_COMBO_UART_BAUDRATE), TRUE);
                        UpdateFlashButtonState(hwnd);
                        return TRUE;
                    } else {
                        // Connect
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
                            SetWindowTextW(hConnectBtn, L"Disconnect");
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
                                    L"Connection Failed\n\n"
                                    L"Please check if device is connected\nor occupied by other program",
                                    L"Connection Failed", MB_OK | MB_ICONWARNING
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
                        wsprintfW(verMsg, L"Version: v%u.%u.%u", (version >> 24) & 0xFF,
                              (version >> 16) & 0xFF, (version >> 8) & 0xFF);
                        SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), verMsg);
                    }
                    EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), TRUE);
                    return TRUE;
                }
                case IDC_BUTTON_REBOOT: {
                    int result = MessageBoxW(hwnd,
                                L"Confirm to reboot the board?", L"Confirm Reboot",
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
                            AppendLog("Waiting for reboot to complete");
                    }
                    return TRUE;
                }
                case IDC_BUTTON_FLASH: {
                    wchar_t fileName[MAX_PATH];
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_FIRMWARE), fileName, MAX_PATH);

                    if (wcslen(fileName) == 0) {
                        MessageBoxW(hwnd, L"Please select firmware file first", L"Tip", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (isUpdating) {
                        MessageBoxW(hwnd, L"Firmware update in progress, please wait", L"Tip", MB_OK | MB_ICONWARNING);
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
                        MessageBoxW(hwnd, L"Failed to create update thread", L"Error", MB_OK | MB_ICONERROR);
                    }
                    return TRUE;
                }
            }
            return FALSE;
        }

        case WM_UPDATE_PROGRESS: {
            // Update progress bar
            HWND hProgress = GetDlgItem(hwnd, IDC_PROGRESS);
            if (hProgress) {
                SendMessage(hProgress, PBM_SETPOS, wParam, 0);
            }
            // Update percentage text
            HWND hPercent = GetDlgItem(hwnd, IDC_LABEL_PERCENT);
            if (hPercent) {
                wchar_t buf[16];
                wsprintfW(buf, L"%d%%", wParam);
                SetWindowTextW(hPercent, buf);
            }
            return TRUE;
        }

        case WM_UPDATE_COMPLETE: {
            // Firmware update completed
            bool success = (wParam != 0);
            isUpdating = false;

            // Restore control state
            EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
            EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);
            UpdateFlashButtonState(hwnd);

            // Show result
            if (success) {
                MessageBoxW(hwnd, L"Firmware upgrade completed! Please reboot the board", L"Success", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(hwnd, L"Firmware upgrade failed, please check log", L"Failed", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }

        case WM_CLOSE:
            // If updating, prompt user
            if (isUpdating) {
                int result = MessageBoxW(hwnd,
                    L"Firmware update in progress, are you sure to exit?",
                    L"Warning",
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
    // Create UART manager
    g_uartManager = UartManager_Create();
    if (!g_uartManager) {
        MessageBoxW(NULL, L"Unable to create UART manager", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create modal dialog
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);

    // Clean up resources
    UartManager_Destroy(g_uartManager);

    return 0;
}
