#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>

#include <wrl.h>
#include "WebView2.h"

#include "CanManager.h"
#include "resource.h"

using namespace Microsoft::WRL;

#define WM_UPDATE_PROGRESS    (WM_APP + 1)
#define WM_UPDATE_COMPLETE    (WM_APP + 2)
#define WM_LOG_MESSAGE        (WM_APP + 3)

static const int BAUD_RATES[] = {
    PCAN_BAUD_10K, PCAN_BAUD_20K, PCAN_BAUD_50K, PCAN_BAUD_100K,
    PCAN_BAUD_125K, PCAN_BAUD_250K, PCAN_BAUD_500K, PCAN_BAUD_1M
};

struct AppState {
    HINSTANCE hInst = nullptr;
    HWND hWnd = nullptr;
    ComPtr<ICoreWebView2> webView;
    ComPtr<ICoreWebView2Controller> controller;
    std::unique_ptr<CanManager> canMgr;
    bool isConnected = false;
    bool isUpdating = false;
    std::vector<TPCANHandle> channels;
};

static AppState g_app;

static std::wstring JsonEscape(const char* utf8Str) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, nullptr, 0);
    if (wlen <= 0) return L"";
    auto wlenUnsigned = static_cast<size_t>(wlen);
    std::wstring wstr(wlenUnsigned - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, &wstr[0], wlen);
    std::wstring result;
    result.reserve(wstr.size() * 2);
    for (wchar_t c : wstr) {
        switch (c) {
            case L'"':  result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:    result += c; break;
        }
    }
    return result;
}

static std::wstring JsonEscapeWide(const std::wstring& str) {
    std::wstring result;
    result.reserve(str.size() * 2);
    for (wchar_t c : str) {
        switch (c) {
            case L'"':  result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:    result += c; break;
        }
    }
    return result;
}

static void PostJson(const wchar_t* json) {
    if (g_app.webView) g_app.webView->PostWebMessageAsJson(json);
}

static void OnLogMessage(const char* msg) {
    auto* copy = _strdup(msg);
    PostMessage(g_app.hWnd, WM_LOG_MESSAGE, 0, reinterpret_cast<LPARAM>(copy));
}

static void OnProgress(int percent) {
    PostMessage(g_app.hWnd, WM_UPDATE_PROGRESS, static_cast<WPARAM>(percent), 0);
}

struct FlashParams {
    std::wstring fileName;
    bool testMode;
};

static DWORD WINAPI FlashThread(LPVOID lpParam) {
    auto* p = static_cast<FlashParams*>(lpParam);
    g_app.canMgr->SetProgressCallback(OnProgress);
    bool ok = g_app.canMgr->FirmwareUpgrade(p->fileName.c_str(), p->testMode);
    PostMessage(g_app.hWnd, WM_UPDATE_COMPLETE, ok ? 1 : 0, 0);
    delete p;
    return 0;
}

static std::wstring GetJsonString(const wchar_t* json, const wchar_t* key) {
    std::wstring search = L"\"";
    search += key;
    search += L"\":";
    const wchar_t* pos = wcsstr(json, search.c_str());
    if (!pos) return L"";
    pos += search.length();
    while (*pos == L' ') pos++;
    if (*pos != L'"') return L"";
    pos++;
    std::wstring result;
    while (*pos && *pos != L'"') {
        if (*pos == L'\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case L'"':  result += L'"'; break;
                case L'\\': result += L'\\'; break;
                case L'n':  result += L'\n'; break;
                case L'r':  result += L'\r'; break;
                case L't':  result += L'\t'; break;
                default:    result += *pos; break;
            }
        } else {
            result += *pos;
        }
        pos++;
    }
    return result;
}

static int GetJsonInt(const wchar_t* json, const wchar_t* key) {
    std::wstring search = L"\"";
    search += key;
    search += L"\":";
    const wchar_t* pos = wcsstr(json, search.c_str());
    if (!pos) return -1;
    pos += search.length();
    while (*pos == L' ') pos++;
    return _wtoi(pos);
}

static bool GetJsonBool(const wchar_t* json, const wchar_t* key) {
    std::wstring search = L"\"";
    search += key;
    search += L"\":";
    const wchar_t* pos = wcsstr(json, search.c_str());
    if (!pos) return false;
    pos += search.length();
    while (*pos == L' ') pos++;
    return *pos == L't';
}

static void HandleRefreshDevices() {
    g_app.canMgr->SetLogCallback(OnLogMessage);
    g_app.channels = g_app.canMgr->DetectDevices();
    std::wstring json = L"{\"event\":\"devices\",\"channels\":[";
    for (size_t i = 0; i < g_app.channels.size(); i++) {
        if (i > 0) json += L",";
        wchar_t buf[64];
        swprintf(buf, 64, L"{\"handle\":%u}", static_cast<unsigned>(g_app.channels[i]));
        json += buf;
    }
    json += L"]}";
    PostJson(json.c_str());
}

static void HandleConnect(int channelIndex, int baudIndex) {
    if (channelIndex < 0 || static_cast<size_t>(channelIndex) >= g_app.channels.size() || baudIndex < 0 || baudIndex >= 8) {
        PostJson(L"{\"event\":\"connectResult\",\"success\":false,\"errorMessage\":\"无效参数\"}");
        return;
    }
    bool ok = g_app.canMgr->Connect(g_app.channels[channelIndex], static_cast<TPCANBaudrate>(BAUD_RATES[baudIndex]));
    g_app.isConnected = ok;
    if (ok) {
        PostJson(L"{\"event\":\"connectResult\",\"success\":true}");
    } else {
        PostJson(L"{\"event\":\"connectResult\",\"success\":false,\"errorMessage\":\"连接失败，请检查设备\"}");
    }
}

static void HandleDisconnect() {
    g_app.canMgr->Disconnect();
    g_app.isConnected = false;
    PostJson(L"{\"event\":\"connectResult\",\"success\":false}");
}

static void HandleBrowseFile() {
    wchar_t fn[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_app.hWnd;
    ofn.lpstrFilter = L"固件文件 (*.bin)\0*.bin\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fn;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) {
        std::wstring escaped = JsonEscapeWide(fn);
        std::wstring json = L"{\"event\":\"fileSelected\",\"path\":\"" + escaped + L"\"}";
        PostJson(json.c_str());
    }
}

static void HandleGetVersion() {
    uint32_t ver = g_app.canMgr->GetFirmwareVersion();
    if (ver) {
        wchar_t buf[64];
        swprintf(buf, 64, L"v%u.%u.%u", (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
        std::wstring json = L"{\"event\":\"version\",\"version\":\"" + std::wstring(buf) + L"\"}";
        PostJson(json.c_str());
    }
}

static void HandleReboot() {
    if (g_app.canMgr->BoardReboot()) {
        OnLogMessage("等待重启完成");
    }
}

static void HandleFlash(const wchar_t* path, bool testMode) {
    if (g_app.isUpdating || !g_app.isConnected) return;
    g_app.isUpdating = true;
    auto* p = new FlashParams{path, testMode};
    PostJson(L"{\"event\":\"flashStart\"}");
    HANDLE hThread = CreateThread(nullptr, 0, FlashThread, p, 0, nullptr);
    if (!hThread) {
        delete p;
        g_app.isUpdating = false;
        OnLogMessage("创建线程失败");
        PostJson(L"{\"event\":\"flashComplete\",\"success\":false}");
    } else {
        CloseHandle(hThread);
    }
}

static HRESULT OnWebMessageReceived(ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) {
    PWSTR message = nullptr;
    args->get_WebMessageAsJson(&message);
    if (!message) return S_OK;
    std::wstring action = GetJsonString(message, L"action");
    if (action == L"refreshDevices") {
        HandleRefreshDevices();
    } else if (action == L"connect") {
        HandleConnect(GetJsonInt(message, L"channelIndex"), GetJsonInt(message, L"baudIndex"));
    } else if (action == L"disconnect") {
        HandleDisconnect();
    } else if (action == L"browseFile") {
        HandleBrowseFile();
    } else if (action == L"getVersion") {
        HandleGetVersion();
    } else if (action == L"reboot") {
        HandleReboot();
    } else if (action == L"flash") {
        std::wstring path = GetJsonString(message, L"path");
        HandleFlash(path.c_str(), GetJsonBool(message, L"testMode"));
    }
    CoTaskMemFree(message);
    return S_OK;
}

static std::wstring LoadHtmlFromResource() {
    HRSRC hRes = FindResource(g_app.hInst, MAKEINTRESOURCE(IDR_HTML_INDEX), RT_RCDATA);
    if (!hRes) return L"";
    HGLOBAL hData = LoadResource(g_app.hInst, hRes);
    if (!hData) return L"";
    auto* data = static_cast<const char*>(LockResource(hData));
    DWORD size = SizeofResource(g_app.hInst, hRes);
    if (!data || size == 0) return L"";
    std::string html(data, size);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, html.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return L"";
    auto wlenUnsigned = static_cast<size_t>(wlen);
    std::wstring whtml(wlenUnsigned - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, html.c_str(), -1, &whtml[0], wlen);
    return whtml;
}

static void CreateWebView() {
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) {
                    MessageBoxW(g_app.hWnd,
                        L"WebView2 初始化失败，请确保已安装 Microsoft Edge WebView2 运行时\n\n"
                        L"下载地址: https://developer.microsoft.com/microsoft-edge/webview2/",
                        L"错误", MB_OK | MB_ICONERROR);
                    return hr;
                }
                env->CreateCoreWebView2Controller(g_app.hWnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(hr) || !controller) return hr;
                            g_app.controller = controller;
                            controller->get_CoreWebView2(&g_app.webView);
                            RECT bounds;
                            GetClientRect(g_app.hWnd, &bounds);
                            g_app.controller->put_Bounds(bounds);
                            ICoreWebView2Settings* settings = nullptr;
                            g_app.webView->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                settings->Release();
                            }
                            g_app.webView->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    OnWebMessageReceived
                                ).Get(), nullptr);
                            std::wstring html = LoadHtmlFromResource();
                            if (!html.empty()) {
                                g_app.webView->NavigateToString(html.c_str());
                            } else {
                                g_app.webView->Navigate(L"about:blank");
                            }
                            return S_OK;
                        }
                    ).Get());
                return S_OK;
            }
        ).Get());
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_app.controller && SIZE_MINIMIZED != wParam) {
            RECT bounds;
            GetClientRect(hwnd, &bounds);
            g_app.controller->put_Bounds(bounds);
        }
        return 0;
    case WM_UPDATE_PROGRESS: {
        wchar_t json[64];
        swprintf(json, 64, L"{\"event\":\"progress\",\"percent\":%d}", static_cast<int>(wParam));
        PostJson(json);
        return 0;
    }
    case WM_UPDATE_COMPLETE: {
        g_app.isUpdating = false;
        wchar_t json[64];
        swprintf(json, 64, L"{\"event\":\"flashComplete\",\"success\":%s}", wParam ? L"true" : L"false");
        PostJson(json);
        return 0;
    }
    case WM_LOG_MESSAGE: {
        auto* m = reinterpret_cast<char*>(lParam);
        std::wstring escaped = JsonEscape(m);
        std::wstring json = L"{\"event\":\"log\",\"message\":\"" + escaped + L"\"}";
        PostJson(json.c_str());
        free(m);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 380;
        return 0;
    }
    case WM_CLOSE:
        if (g_app.isUpdating) {
            if (IDYES != MessageBoxW(hwnd, L"升级中，确定退出？",
                    L"警告", MB_YESNO | MB_ICONWARNING))
                return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_app.controller.Reset();
        g_app.webView.Reset();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow) {
    g_app.hInst = hInstance;
    g_app.canMgr = std::make_unique<CanManager>();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CANUpgradeTool";
    RegisterClassExW(&wc);
    g_app.hWnd = CreateWindowExW(0, L"CANUpgradeTool", L"固件升级工具 (CAN)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 640, 520,
        nullptr, nullptr, hInstance, nullptr);
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON));
    SendMessage(g_app.hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
    SendMessage(g_app.hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    ShowWindow(g_app.hWnd, nCmdShow);
    UpdateWindow(g_app.hWnd);
    CreateWebView();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
