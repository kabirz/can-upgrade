/*
 * UDP 固件升级工具 (参考 win32c 结构, 通信层换 UDP)
 *
 * 通信协议: 与 gateway 固件对齐 (配置端口 9200)
 *   FW_START 0x10 [size 4B LE]  → 回 [0x10][1/0]
 *   FW_DATA  0x11 [data 256B]   → 回 [0x11][offset 4B LE]
 *   FW_END   0x12 [test 1B][crc 2B LE] → 回 [0x12][1/0]
 *
 * 目标 IP 留空 → 多网卡子网定向广播自动发现固件
 */
/* winsock2 必须在 windows.h 之前 (udp_manager.h 内部已按此顺序 include,
 * 故放最前, 避免 windows.h 拉入 winsock1 与 iphlpapi 冲突) */
#include "udp_manager.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resource.h"

/* 自定义消息 */
#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)
#define WM_UPDATE_LOG         (WM_APP + 3)

/* 默认端口 */
#define DEFAULT_LOCAL_PORT    9201   /* 本地监听 (固件单播回复到此) */
#define DEFAULT_REMOTE_PORT   9200   /* 远程端口 (固件配置端口, 可改) */

static HWND hLog;
static HWND g_hwnd = NULL;   /* 主对话框句柄 (供工作线程 PostMessage) */
static int isConnected = 0;
static int isUpdating = 0;
static UdpManager *g_udp = NULL;
static HINSTANCE g_hInst = NULL;

/* ================================================================
 * 日志输出 (UTF-8 → UTF-16, 带时间戳)
 * ================================================================ */
static void AppendLog(const char *msg)
{
	if (!hLog || !msg) return;

	SYSTEMTIME st;
	GetLocalTime(&st);

	char tbuf[512];
	int len = sprintf(tbuf, "[%02d:%02d:%02d] %s\r\n",
			  st.wHour, st.wMinute, st.wSecond, msg);
	if (len < 0) len = 0;

	int wlen = MultiByteToWideChar(CP_UTF8, 0, tbuf, len, NULL, 0);
	if (wlen <= 0 || wlen >= 1024) return;
	wchar_t wbuf[1024];
	MultiByteToWideChar(CP_UTF8, 0, tbuf, len, wbuf, wlen);
	wbuf[wlen] = L'\0';   /* EM_REPLACESEL 要求 NUL 结尾, 否则输出栈垃圾乱码 */

	int pos = GetWindowTextLengthW(hLog);
	SendMessageW(hLog, EM_SETSEL, pos, pos);
	SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)wbuf);
}

/* udp_manager 消息回调 (保留备用, 当前未启用 — 见 IDC_BUTTON_CONNECT 注释).
 * 若未来需要异步响应上报, 通过 UdpManager_SetMsgCallback 注册即可. */
static void udp_msg_cb(const char *msg, void *user_data)
{
	(void)user_data;
	if (msg && g_hwnd) {
		PostMessageA(g_hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup(msg));
	}
}

/* ================================================================
 * 固件升级线程 (照 gateway-tool fw_upgrade_thread 流程)
 * ================================================================ */
typedef struct {
	HWND hwnd;
	char path[MAX_PATH];
	int testMode;
} FwParams;

static DWORD WINAPI FirmwareUpdateThread(LPVOID lpParam)
{
	FwParams *p = (FwParams *)lpParam;
	HWND hwnd = p->hwnd;
	BOOL result = FALSE;

	PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup("UDP 固件升级开始"));

	/* 读取固件文件 */
	HANDLE hFile = CreateFileA(p->path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup("无法打开固件文件"));
		goto done;
	}

	DWORD fileSize = GetFileSize(hFile, NULL);
	uint8_t *fileData = (uint8_t *)malloc(fileSize);
	DWORD bytesRead;

	if (!ReadFile(hFile, fileData, fileSize, &bytesRead, NULL) || bytesRead != fileSize) {
		CloseHandle(hFile);
		free(fileData);
		PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup("无法读取固件文件"));
		goto done;
	}
	CloseHandle(hFile);

	{
		char diag[128];
		sprintf(diag, "文件大小=%lu 字节", fileSize);
		PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup(diag));
	}

	/* 1. 算 CRC16-CCITT (与固件 crc16_ccitt 对齐) */
	uint16_t crc = UdpManager_CRC16_CCITT(fileData, fileSize);
	{
		char diag[128];
		sprintf(diag, "CRC16=0x%04x 测试模式=%d", crc, p->testMode);
		PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup(diag));
	}

	/* 2. START (带 size) */
	if (!UdpManager_FirmwareStart(g_udp, fileSize)) {
		PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup("[固件] 启动失败"));
		free(fileData);
		goto done;
	}
	PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup("[固件] 启动成功"));

	/* 3. DATA 循环 (每包 256B, 验证 offset) */
	result = TRUE;
	{
		int offset = 0;
		int chunk = 256;

		while (offset < (int)fileSize) {
			int sendLen = ((int)fileSize - offset > chunk) ? chunk : ((int)fileSize - offset);
			uint32_t got = 0;

			if (!UdpManager_FirmwareData(g_udp, fileData + offset,
						     sendLen, offset + sendLen, &got)) {
				char diag[128];
				sprintf(diag, "[固件] 数据发送失败 偏移=%d (固件偏移=%lu)",
					offset, got);
				PostMessageA(hwnd, WM_UPDATE_LOG, 0, (LPARAM)_strdup(diag));
				result = FALSE;
				break;
			}
			offset += sendLen;
			int pct = (int)((long long)offset * 100 / fileSize);
			PostMessageA(hwnd, WM_UPDATE_PROGRESS, (WPARAM)pct, 0);
		}
	}

	free(fileData);

	/* 4. END (CRC + test_mode), 仅 DATA 全部成功才发 */
	if (result) {
		if (UdpManager_FirmwareEnd(g_udp, (uint8_t)p->testMode, crc)) {
			PostMessageA(hwnd, WM_UPDATE_LOG, 0,
				     (LPARAM)_strdup("[固件] 升级完成，请重启设备"));
		} else {
			PostMessageA(hwnd, WM_UPDATE_LOG, 0,
				     (LPARAM)_strdup("[固件] 校验失败 (CRC 不匹配或错误)"));
			result = FALSE;
		}
	}

done:
	PostMessageA(hwnd, WM_UPDATE_COMPLETE, (WPARAM)result, 0);
	free(p);
	return 0;
}

/* ================================================================
 * 控件状态更新
 * ================================================================ */
static void UpdateFlashButtonState(HWND hwnd)
{
	HWND hFlash = GetDlgItem(hwnd, IDC_BUTTON_FLASH);
	HWND hFw = GetDlgItem(hwnd, IDC_EDIT_FIRMWARE);
	wchar_t fileName[MAX_PATH];

	GetWindowTextW(hFw, fileName, MAX_PATH);
	int hasFile = (wcslen(fileName) > 0);

	EnableWindow(hFlash, isConnected && hasFile && !isUpdating);
}

static void SetConnectedUI(HWND hwnd, int connected)
{
	HWND hConn = GetDlgItem(hwnd, IDC_BUTTON_CONNECT);
	HWND hIp = GetDlgItem(hwnd, IDC_EDIT_IP);
	HWND hPort = GetDlgItem(hwnd, IDC_EDIT_LOCAL_PORT);
	HWND hRPort = GetDlgItem(hwnd, IDC_EDIT_REMOTE_PORT);

	if (connected) {
		SetWindowTextW(hConn, L"断开");
		EnableWindow(hIp, FALSE);
		EnableWindow(hPort, FALSE);
		EnableWindow(hRPort, FALSE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), TRUE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), TRUE);
	} else {
		SetWindowTextW(hConn, L"连接");
		EnableWindow(hIp, TRUE);
		EnableWindow(hPort, TRUE);
		EnableWindow(hRPort, TRUE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);
		SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), L"固件版本: 未获取");
	}
	UpdateFlashButtonState(hwnd);
}

/* ================================================================
 * 对话框过程
 * ================================================================ */
static INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG: {
		g_hwnd = hwnd;
		/* 图标 */
		HICON hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_APPICON));
		SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
		SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

		/* 端口默认值 */
		SetDlgItemInt(hwnd, IDC_EDIT_LOCAL_PORT, DEFAULT_LOCAL_PORT, FALSE);
		SetDlgItemInt(hwnd, IDC_EDIT_REMOTE_PORT, DEFAULT_REMOTE_PORT, FALSE);

		/* 进度条 */
		HWND hProg = GetDlgItem(hwnd, IDC_PROGRESS);
		SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
		SendMessageW(hProg, PBM_SETPOS, 0, 0);

		/* 初始按钮状态 */
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_GETVERSION), FALSE);
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_REBOOT), FALSE);

		hLog = GetDlgItem(hwnd, IDC_EDIT_LOG);
		UpdateFlashButtonState(hwnd);

		AppendLog("就绪。目标 IP 留空则广播自动发现固件");
		return TRUE;
	}

	case WM_COMMAND: {
		switch (LOWORD(wParam)) {
		case IDCANCEL:
		case IDOK:
			if (isUpdating) {
				MessageBoxW(hwnd, L"固件升级进行中，请等待完成", L"提示", MB_OK | MB_ICONWARNING);
				return TRUE;
			}
			DestroyWindow(hwnd);
			PostQuitMessage(0);
			return TRUE;

		case IDM_FILE_EXIT:
			if (isUpdating) {
				MessageBoxW(hwnd, L"固件升级进行中，请等待完成", L"提示", MB_OK | MB_ICONWARNING);
				return TRUE;
			}
			DestroyWindow(hwnd);
			PostQuitMessage(0);
			return TRUE;

		case IDM_FILE_OPEN:
		case IDC_BUTTON_BROWSE: {
			wchar_t fileName[MAX_PATH] = L"";
			OPENFILENAMEW ofn = { sizeof(ofn) };
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
				L"UDP 固件升级工具 v1.0\n\n"
				L"通过 UDP 协议升级板卡固件\n\n"
				L"功能:\n"
				L"• 目标 IP / 本地端口 / 远程端口均可配置\n"
				L"• 目标 IP 留空则广播自动发现\n"
				L"• 固件升级与测试模式 (重启后回滚)\n"
				L"• 版本查询与板卡重启",
				L"关于", MB_OK | MB_ICONINFORMATION);
			return TRUE;

		case IDM_EDIT_CLEARLOG:
		case IDC_BUTTON_CLEAR_LOG:
			SetWindowTextW(hLog, L"");
			return TRUE;

		case IDC_BUTTON_CONNECT: {
			if (isUpdating) {
				MessageBoxW(hwnd, L"固件升级进行中，无法断开", L"提示", MB_OK | MB_ICONWARNING);
				return TRUE;
			}
			if (isConnected) {
				/* 断开 */
				UdpManager_StopRxThread(g_udp);
				UdpManager_Unbind(g_udp);
				isConnected = 0;
				SetConnectedUI(hwnd, 0);
				AppendLog("已断开");
				return TRUE;
			}
			/* 连接: 读 IP + 本地端口 + 远程端口 */
			char ip[64] = { 0 };
			GetDlgItemTextA(hwnd, IDC_EDIT_IP, ip, sizeof(ip));
			UINT localPort = GetDlgItemInt(hwnd, IDC_EDIT_LOCAL_PORT, NULL, FALSE);
			UINT remotePort = GetDlgItemInt(hwnd, IDC_EDIT_REMOTE_PORT, NULL, FALSE);
			if (localPort == 0) localPort = DEFAULT_LOCAL_PORT;
			if (remotePort == 0) remotePort = DEFAULT_REMOTE_PORT;

			/* ip 为空/非法 → 传 NULL, UdpManager_Bind 内部走广播自动发现 */
			const char *ipArg = (ip[0] != '\0') ? ip : NULL;
			if (UdpManager_Bind(g_udp, UDP_CHAN_CONFIG, (uint16_t)localPort,
					    ipArg, (uint16_t)remotePort)) {
				/* 不设 msg_cb: 升级/版本/重启都走同步 send_and_wait,
				 * 异步响应 (如 reboot 回复) 无意义, 设了反而和升级线程
				 * 的诊断日志并发 PostMessage 导致输出交错乱码. */
				UdpManager_StartRxThread(g_udp);
				isConnected = 1;
			SetConnectedUI(hwnd, 1);
			if (!ipArg) {
				AppendLog("已绑定 (广播自动发现)");
			} else if (strcmp(ipArg, "255.255.255.255") == 0) {
				AppendLog("已绑定 (有限广播)");
			} else {
				AppendLog("已连接 (单播)");
			}
			} else {
				MessageBoxW(hwnd,
					L"绑定失败\n\n请检查端口是否被占用",
					L"连接失败", MB_OK | MB_ICONWARNING);
			}
			return TRUE;
		}

		case IDC_BUTTON_GETVERSION: {
			char ver[64] = { 0 };
			if (UdpManager_GetVersion(g_udp, ver, sizeof(ver))) {
				char buf[96];
				sprintf(buf, "固件版本: %s", ver);
				int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
				if (wlen > 0) {
					wchar_t wbuf[96];
					MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);
					SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_VERSION), wbuf);
				}
				AppendLog(buf);
			} else {
				AppendLog("获取版本失败 (设备未响应，请确认已连接且固件支持版本查询)");
			}
			return TRUE;
		}

		case IDC_BUTTON_REBOOT: {
			if (MessageBoxW(hwnd, L"确认要重启板卡吗？", L"确认重启",
					MB_OKCANCEL | MB_ICONINFORMATION) == IDOK) {
				if (UdpManager_Reboot(g_udp)) {
					AppendLog("重启命令已发送");
				}
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
				MessageBoxW(hwnd, L"固件升级中，请等待完成", L"提示", MB_OK | MB_ICONWARNING);
				return TRUE;
			}

			int testMode = (SendMessageW(GetDlgItem(hwnd, IDC_CHECK_TESTMODE),
						     BM_GETCHECK, 0, 0) == BST_CHECKED);

			isUpdating = 1;
			EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), FALSE);
			EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), FALSE);
			SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, 0, 0);

			FwParams *params = (FwParams *)malloc(sizeof(FwParams));
			params->hwnd = hwnd;
			WideCharToMultiByte(CP_UTF8, 0, fileName, -1, params->path, MAX_PATH, NULL, NULL);
			params->testMode = testMode;

			HANDLE hThread = CreateThread(NULL, 0, FirmwareUpdateThread, params, 0, NULL);
			if (hThread) {
				CloseHandle(hThread);
			} else {
				free(params);
				isUpdating = 0;
				EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_FLASH), TRUE);
				EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
				EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);
				MessageBoxW(hwnd, L"创建升级线程失败", L"错误", MB_OK | MB_ICONERROR);
			}
			return TRUE;
		}
		}
		return FALSE;
	}

	case WM_UPDATE_PROGRESS: {
		SendMessageW(GetDlgItem(hwnd, IDC_PROGRESS), PBM_SETPOS, wParam, 0);
		wchar_t buf[16];
		wsprintfW(buf, L"%d%%", wParam);
		SetWindowTextW(GetDlgItem(hwnd, IDC_LABEL_PERCENT), buf);
		return TRUE;
	}

	case WM_UPDATE_LOG: {
		if (lParam) {
			AppendLog((const char *)lParam);
			free((char *)lParam);
		}
		return TRUE;
	}

	case WM_UPDATE_COMPLETE: {
		int success = (wParam != 0);
		isUpdating = 0;
		EnableWindow(GetDlgItem(hwnd, IDC_BUTTON_BROWSE), TRUE);
		EnableWindow(GetDlgItem(hwnd, IDC_CHECK_TESTMODE), TRUE);
		UpdateFlashButtonState(hwnd);
		if (success) {
			MessageBoxW(hwnd, L"固件升级完成！\n请点击「重启板卡」使新固件生效",
				L"成功", MB_OK | MB_ICONINFORMATION);
		} else {
			MessageBoxW(hwnd, L"固件升级失败，请查看日志", L"失败", MB_OK | MB_ICONERROR);
		}
		return TRUE;
	}

	case WM_CLOSE:
		if (isUpdating) {
			if (MessageBoxW(hwnd, L"固件升级进行中，确定要退出吗？",
					L"警告", MB_YESNO | MB_ICONWARNING) != IDYES) {
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

/* ================================================================
 * WinMain
 * ================================================================ */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	(void)hPrevInstance;
	(void)lpCmdLine;

	INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES };
	InitCommonControlsEx(&icex);

	g_hInst = hInstance;

	g_udp = UdpManager_Create();
	if (!g_udp) {
		MessageBoxW(NULL, L"无法创建 UDP 管理器", L"错误", MB_OK | MB_ICONERROR);
		return 1;
	}

	DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), NULL, DlgProc);

	UdpManager_Destroy(g_udp);
	return 0;
}
