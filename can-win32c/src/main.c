#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"
#include "can_manager.h"

#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)

#define DLG(id)  GetDlgItem(hwnd, id)

static HWND hLog;
static int isConnected;
static int isUpdating;
static CanManager* g_canManager;

static TPCANHandle g_channels[MAX_DEVICES];
static int g_channelCount;

static const int BAUD_RATES[] = {
    PCAN_BAUD_10K, PCAN_BAUD_20K, PCAN_BAUD_50K, PCAN_BAUD_100K,
    PCAN_BAUD_125K, PCAN_BAUD_250K, PCAN_BAUD_500K, PCAN_BAUD_1M
};
static const wchar_t* baudNames[] = {
    L"10K", L"20K", L"50K", L"100K",
    L"125K", L"250K", L"500K", L"1000K"
};

typedef struct {
    HWND hwnd;
    wchar_t fileName[MAX_PATH];
    int testMode;
} FirmwareUpdateParams;

static void OnProgress(int percent) {
    if (hLog) PostMessage(GetParent(hLog), WM_UPDATE_PROGRESS, percent, 0);
}

static void UpdateConnectBtn(HWND hwnd);
static void UpdateFlashBtn(HWND hwnd);

void AppendLog(const char* msg) {
    if (!hLog) return;
    int wlen = MultiByteToWideChar(CP_UTF8, 0, msg, -1, NULL, 0);
    wchar_t* wstr = (wchar_t*)malloc(sizeof(wchar_t) * wlen);
    MultiByteToWideChar(CP_UTF8, 0, msg, -1, wstr, wlen);
    wchar_t ts[16];
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(ts, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)ts);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)wstr);
    SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(hLog, EM_SETSEL, -1, -1);
    free(wstr);
}

static void EnableUI(HWND hwnd, int conn) {
    EnableWindow(DLG(IDC_BUTTON_GETVERSION), conn);
    EnableWindow(DLG(IDC_BUTTON_REBOOT), conn);
    EnableWindow(DLG(IDC_BUTTON_REFRESH), !conn);
    EnableWindow(DLG(IDC_COMBO_CHANNEL), !conn);
    EnableWindow(DLG(IDC_COMBO_BAUDRATE), !conn);
    if (conn) {
        EnableWindow(DLG(IDC_BUTTON_CONNECT), TRUE);
    } else {
        UpdateConnectBtn(hwnd);
    }
    if (!conn) SetWindowTextW(DLG(IDC_LABEL_VERSION), L"固件版本: 未获取");
}

static void UpdateFlashBtn(HWND hwnd) {
    wchar_t fn[MAX_PATH];
    GetWindowTextW(DLG(IDC_EDIT_FIRMWARE), fn, MAX_PATH);
    EnableWindow(DLG(IDC_BUTTON_FLASH), isConnected && wcslen(fn) > 0 && !isUpdating);
}

static void UpdateConnectBtn(HWND hwnd) {
    int idx = SendMessage(DLG(IDC_COMBO_CHANNEL), CB_GETCURSEL, 0, 0);
    EnableWindow(DLG(IDC_BUTTON_CONNECT), idx >= 0 && g_channelCount > 0 && !isUpdating);
}

static void RefreshDevices(HWND hwnd) {
    HWND hCombo = DLG(IDC_COMBO_CHANNEL);
    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    CanManager_SetCallback(g_canManager, AppendLog);
    g_channelCount = CanManager_DetectDevice(g_canManager, g_channels, MAX_DEVICES);
    if (g_channelCount < 0) {
        AppendLog("缺少 PCANBasic.dll，可能未安装 PCAN 驱动，请安装驱动后重试");
    } else if (g_channelCount > 0){
        wchar_t buf[128];
        for (int i = 0; i < g_channelCount; i++) {
            wsprintfW(buf, L"PCAN-USB: %xh", g_channels[i]);
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
    }
    UpdateConnectBtn(hwnd);
}

DWORD WINAPI FirmwareThread(LPVOID lpParam) {
    FirmwareUpdateParams* p = (FirmwareUpdateParams*)lpParam;
    CanManager_SetProgressCallback(g_canManager, OnProgress);
    int ok = CanManager_FirmwareUpgrade(g_canManager, p->fileName, p->testMode);
    PostMessage(p->hwnd, WM_UPDATE_COMPLETE, ok, 0);
    free(p);
    return 0;
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        HWND hBaud = DLG(IDC_COMBO_BAUDRATE);
        for (int i = 0; i < 8; i++)
            SendMessage(hBaud, CB_ADDSTRING, 0, (LPARAM)baudNames[i]);
        SendMessage(hBaud, CB_SETCURSEL, 5, 0);
        HWND hProg = DLG(IDC_PROGRESS);
        SendMessage(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hProg, PBM_SETPOS, 0, 0);
        EnableWindow(DLG(IDC_BUTTON_FLASH), FALSE);
        EnableWindow(DLG(IDC_BUTTON_GETVERSION), FALSE);
        EnableWindow(DLG(IDC_BUTTON_REBOOT), FALSE);
        EnableWindow(DLG(IDC_BUTTON_CONNECT), FALSE);
        hLog = DLG(IDC_EDIT_LOG);
        UpdateFlashBtn(hwnd);
        RefreshDevices(hwnd);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDCANCEL:
        case IDOK:
            DestroyWindow(hwnd);
            PostQuitMessage(0);
            return TRUE;
        case IDC_BUTTON_BROWSE: {
            wchar_t fn[MAX_PATH] = L"";
            OPENFILENAMEW ofn = {sizeof(ofn)};
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = L"固件文件 (*.bin)\0*.bin\0所有文件 (*.*)\0*.*\0";
            ofn.lpstrFile = fn;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) {
                SetWindowTextW(DLG(IDC_EDIT_FIRMWARE), fn);
                UpdateFlashBtn(hwnd);
            }
            return TRUE;
        }
        case IDC_BUTTON_CLEAR_LOG:
            SetWindowTextW(hLog, L"");
            return TRUE;
        case IDC_BUTTON_REFRESH:
            RefreshDevices(hwnd);
            return TRUE;
        case IDC_BUTTON_CONNECT: {
            if (isConnected) {
                CanManager_Disconnect(g_canManager);
                isConnected = 0;
                SetWindowTextW(DLG(IDC_BUTTON_CONNECT), L"连接");
                EnableUI(hwnd, 0);
                UpdateFlashBtn(hwnd);
                return TRUE;
            }
            int idx = SendMessage(DLG(IDC_COMBO_CHANNEL), CB_GETCURSEL, 0, 0);
            int baud = SendMessage(DLG(IDC_COMBO_BAUDRATE), CB_GETCURSEL, 0, 0);
            if (idx < 0 || idx >= g_channelCount || baud < 0) return TRUE;
            EnableWindow(DLG(IDC_BUTTON_CONNECT), FALSE);
            int ok = CanManager_Connect(g_canManager, g_channels[idx], BAUD_RATES[baud]);
            EnableWindow(DLG(IDC_BUTTON_CONNECT), TRUE);
            if (ok) {
                isConnected = 1;
                SetWindowTextW(DLG(IDC_BUTTON_CONNECT), L"断开");
                EnableUI(hwnd, 1);
                UpdateFlashBtn(hwnd);
            } else {
                MessageBoxW(hwnd, L"连接失败，请检查设备", L"提示", MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }
        case IDC_BUTTON_GETVERSION: {
            EnableWindow(DLG(IDC_BUTTON_GETVERSION), FALSE);
            uint32_t ver = CanManager_GetFirmwareVersion(g_canManager);
            if (ver) {
                wchar_t buf[32];
                wsprintfW(buf, L"固件版本: v%u.%u.%u",
                          (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
                SetWindowTextW(DLG(IDC_LABEL_VERSION), buf);
            }
            EnableWindow(DLG(IDC_BUTTON_GETVERSION), TRUE);
            return TRUE;
        }
        case IDC_BUTTON_REBOOT:
            if (IDOK == MessageBoxW(hwnd, L"确认重启板卡？", L"确认",
                                    MB_OKCANCEL | MB_ICONINFORMATION)) {
                if (CanManager_BoardReboot(g_canManager))
                    AppendLog("等待重启完成");
            }
            return TRUE;
        case IDC_BUTTON_FLASH: {
            wchar_t fn[MAX_PATH];
            GetWindowTextW(DLG(IDC_EDIT_FIRMWARE), fn, MAX_PATH);
            if (wcslen(fn) == 0 || isUpdating) return TRUE;
            int test = SendMessage(DLG(IDC_CHECK_TESTMODE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            isUpdating = 1;
            EnableWindow(DLG(IDC_BUTTON_FLASH), FALSE);
            EnableWindow(DLG(IDC_BUTTON_BROWSE), FALSE);
            EnableWindow(DLG(IDC_CHECK_TESTMODE), FALSE);
            SendMessage(DLG(IDC_PROGRESS), PBM_SETPOS, 0, 0);

            FirmwareUpdateParams* p = (FirmwareUpdateParams*)malloc(sizeof(*p));
            p->hwnd = hwnd;
            wcscpy(p->fileName, fn);
            p->testMode = test;
            HANDLE hThread = CreateThread(NULL, 0, FirmwareThread, p, 0, NULL);
            if (!hThread) {
                free(p);
                isUpdating = 0;
                EnableWindow(DLG(IDC_BUTTON_FLASH), TRUE);
                EnableWindow(DLG(IDC_BUTTON_BROWSE), TRUE);
                EnableWindow(DLG(IDC_CHECK_TESTMODE), TRUE);
                MessageBoxW(hwnd, L"创建线程失败", L"错误", MB_OK | MB_ICONERROR);
            } else {
                CloseHandle(hThread);
            }
            return TRUE;
        }
        }
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO_CHANNEL) {
            UpdateConnectBtn(hwnd);
            return TRUE;
        }
        return FALSE;
    case WM_UPDATE_PROGRESS: {
        SendMessage(DLG(IDC_PROGRESS), PBM_SETPOS, wParam, 0);
        wchar_t buf[16];
        wsprintfW(buf, L"%d%%", wParam);
        SetWindowTextW(DLG(IDC_LABEL_PERCENT), buf);
        return TRUE;
    }
    case WM_UPDATE_COMPLETE: {
        isUpdating = 0;
        EnableWindow(DLG(IDC_BUTTON_BROWSE), TRUE);
        EnableWindow(DLG(IDC_CHECK_TESTMODE), TRUE);
        UpdateFlashBtn(hwnd);
        MessageBoxW(hwnd, wParam ? L"固件升级完成！请重启板卡" : L"固件升级失败，请查看日志",
                    wParam ? L"成功" : L"失败",
                    wParam ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
        return TRUE;
    }
    case WM_CLOSE:
        if (isUpdating && IDYES != MessageBoxW(hwnd, L"升级中，确定退出？",
                                               L"警告", MB_YESNO | MB_ICONWARNING))
            return TRUE;
        DestroyWindow(hwnd);
        PostQuitMessage(0);
        return TRUE;
    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_canManager = CanManager_Create();
    DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), NULL, DlgProc);
    CanManager_Destroy(g_canManager);
    return 0;
}
