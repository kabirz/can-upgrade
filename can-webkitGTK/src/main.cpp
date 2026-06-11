#include "CanManager.h"
#include "index_html.h"

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

struct AppState {
    GtkApplication *app = nullptr;
    GtkWindow *window = nullptr;
    WebKitWebView *webview = nullptr;
    std::unique_ptr<CanManager> canMgr;
    bool isConnected = false;
    std::atomic<bool> isUpdating{false};
    std::vector<std::string> channels;
};

static AppState g_app;

static std::string JsonEscape(const char* str) {
    std::string result;
    result.reserve(strlen(str) * 2);
    for (const char* p = str; *p; ++p) {
        switch (*p) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += *p; break;
        }
    }
    return result;
}

static void PostJson(const char* json) {
    if (!g_app.webview) return;
    std::string js = "if(window._onBridgeMessage)window._onBridgeMessage({data:";
    js += json;
    js += "})";
    webkit_web_view_evaluate_javascript(g_app.webview, js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
}

static std::string GetJsonString(const char* json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\":";
    const char* pos = strstr(json, search.c_str());
    if (!pos) return "";
    pos += search.length();
    while (*pos == ' ') pos++;
    if (*pos != '"') return "";
    pos++;
    std::string result;
    while (*pos && *pos != '"') {
        if (*pos == '\\' && *(pos + 1)) {
            pos++;
            switch (*pos) {
                case '"':  result += '"'; break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                default:   result += *pos; break;
            }
        } else {
            result += *pos;
        }
        pos++;
    }
    return result;
}

static int GetJsonInt(const char* json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\":";
    const char* pos = strstr(json, search.c_str());
    if (!pos) return -1;
    pos += search.length();
    while (*pos == ' ') pos++;
    return atoi(pos);
}

static bool GetJsonBool(const char* json, const char* key) {
    std::string search = "\"";
    search += key;
    search += "\":";
    const char* pos = strstr(json, search.c_str());
    if (!pos) return false;
    pos += search.length();
    while (*pos == ' ') pos++;
    return *pos == 't';
}

static gboolean idle_post_json(gpointer data) {
    char* json = static_cast<char*>(data);
    PostJson(json);
    g_free(json);
    return G_SOURCE_REMOVE;
}

static void PostJsonFromThread(const char* json) {
    g_idle_add(idle_post_json, g_strdup(json));
}

static void OnLogMessage(const char* msg) {
    std::string escaped = JsonEscape(msg);
    std::string json = "{\"event\":\"log\",\"message\":\"" + escaped + "\"}";
    PostJsonFromThread(json.c_str());
}

static void OnProgress(int percent) {
    char json[64];
    snprintf(json, sizeof(json), "{\"event\":\"progress\",\"percent\":%d}", percent);
    PostJsonFromThread(json);
}

static void HandleRefreshDevices() {
    g_app.canMgr->SetLogCallback(OnLogMessage);
    g_app.channels = g_app.canMgr->DetectDevices();
    std::string json = "{\"event\":\"devices\",\"channels\":[";
    for (size_t i = 0; i < g_app.channels.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + g_app.channels[i] + "\"}";
    }
    json += "]}";
    PostJson(json.c_str());
}

static void HandleConnect(int channelIndex, int baudIndex) {
    if (channelIndex < 0 || static_cast<size_t>(channelIndex) >= g_app.channels.size() || baudIndex < 0 || baudIndex >= 8) {
        PostJson("{\"event\":\"connectResult\",\"success\":false,\"errorMessage\":\"无效参数\"}");
        return;
    }
    bool ok = g_app.canMgr->Connect(g_app.channels[channelIndex], baudIndex);
    g_app.isConnected = ok;
    if (ok) {
        PostJson("{\"event\":\"connectResult\",\"success\":true}");
    } else {
        PostJson("{\"event\":\"connectResult\",\"success\":false,\"errorMessage\":\"连接失败，请检查设备\"}");
    }
}

static void HandleDisconnect() {
    g_app.canMgr->Disconnect();
    g_app.isConnected = false;
    PostJson("{\"event\":\"connectResult\",\"success\":false}");
}

static void on_file_open_cb(GObject* source, GAsyncResult* result, gpointer) {
    GtkFileDialog* dialog = GTK_FILE_DIALOG(source);
    GFile* file = gtk_file_dialog_open_finish(dialog, result, nullptr);
    if (!file) return;

    char* path = g_file_get_path(file);
    g_object_unref(file);
    if (!path) return;

    std::string escaped = JsonEscape(path);
    std::string json = "{\"event\":\"fileSelected\",\"path\":\"" + escaped + "\"}";
    PostJson(json.c_str());
    g_free(path);
}

static void HandleBrowseFile() {
    GtkFileDialog* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "选择固件文件");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "固件文件 (*.bin)");
    gtk_file_filter_add_suffix(filter, "bin");
    GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    g_object_unref(filters);
    gtk_file_dialog_open(dialog, g_app.window, nullptr, on_file_open_cb, nullptr);
    g_object_unref(dialog);
}

static void HandleGetVersion() {
    uint32_t ver = g_app.canMgr->GetFirmwareVersion();
    if (ver) {
        char buf[64];
        snprintf(buf, sizeof(buf), "v%u.%u.%u", (ver >> 24) & 0xFF, (ver >> 16) & 0xFF, (ver >> 8) & 0xFF);
        std::string json = "{\"event\":\"version\",\"version\":\"" + std::string(buf) + "\"}";
        PostJson(json.c_str());
    }
}

static void HandleReboot() {
    if (g_app.canMgr->BoardReboot()) {
        OnLogMessage("等待重启完成");
    }
}

static void HandleFlash(const std::string& path, bool testMode) {
    if (g_app.isUpdating || !g_app.isConnected) return;
    g_app.isUpdating = true;
    g_app.canMgr->SetLogCallback(OnLogMessage);
    PostJson("{\"event\":\"flashStart\"}");

    std::thread([path, testMode]() {
        g_app.canMgr->SetProgressCallback(OnProgress);
        bool ok = g_app.canMgr->FirmwareUpgrade(path, testMode);
        char json[64];
        snprintf(json, sizeof(json), "{\"event\":\"flashComplete\",\"success\":%s}", ok ? "true" : "false");
        PostJsonFromThread(json);
        g_app.isUpdating = false;
    }).detach();
}

static void on_script_message(WebKitUserContentManager*, JSCValue* value, gpointer) {
    if (!jsc_value_is_string(value)) return;
    char* msg = jsc_value_to_string(value);
    if (!msg) return;

    std::string action = GetJsonString(msg, "action");

    if (action == "refreshDevices") {
        HandleRefreshDevices();
    } else if (action == "connect") {
        HandleConnect(GetJsonInt(msg, "channelIndex"), GetJsonInt(msg, "baudIndex"));
    } else if (action == "disconnect") {
        HandleDisconnect();
    } else if (action == "browseFile") {
        HandleBrowseFile();
    } else if (action == "getVersion") {
        HandleGetVersion();
    } else if (action == "reboot") {
        HandleReboot();
    } else if (action == "flash") {
        std::string path = GetJsonString(msg, "path");
        HandleFlash(path, GetJsonBool(msg, "testMode"));
    }

    g_free(msg);
}

struct CloseCallbackData {
    gboolean* result;
    GMainLoop* loop;
};

static void on_alert_response(GtkAlertDialog*, int response, gpointer user_data) {
    auto* data = static_cast<CloseCallbackData*>(user_data);
    *(data->result) = (response == 1);
    g_main_loop_quit(data->loop);
}

static gboolean on_close_request(GtkWindow*, gpointer) {
    if (g_app.isUpdating) {
        GtkAlertDialog* dialog = gtk_alert_dialog_new("升级中，确定退出？");
        gtk_alert_dialog_set_modal(dialog, TRUE);
        const char* buttons[] = {"取消", "退出", nullptr};
        gtk_alert_dialog_set_buttons(dialog, buttons);

        gboolean should_close = FALSE;
        GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
        CloseCallbackData cbd = {&should_close, loop};

        g_signal_connect(dialog, "response", G_CALLBACK(on_alert_response), &cbd);
        gtk_alert_dialog_show(dialog, g_app.window);

        g_main_loop_run(loop);
        g_main_loop_unref(loop);
        g_object_unref(dialog);

        if (!should_close) {
            g_signal_stop_emission_by_name(g_app.window, "close-request");
            return TRUE;
        }
    }
    return FALSE;
}

static void on_activate(GtkApplication* app, gpointer) {
    g_app.app = app;

    GtkWidget* window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "固件升级工具 (CAN)");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 520);
    g_app.window = GTK_WINDOW(window);

    g_signal_connect(window, "close-request", G_CALLBACK(on_close_request), nullptr);

    GtkWidget* webview = webkit_web_view_new();
    g_app.webview = WEBKIT_WEB_VIEW(webview);

    WebKitUserContentManager* ucm = webkit_web_view_get_user_content_manager(g_app.webview);
    g_signal_connect(ucm, "script-message-received::bridge", G_CALLBACK(on_script_message), nullptr);
    webkit_user_content_manager_register_script_message_handler(ucm, "bridge", nullptr);

    WebKitSettings* settings = webkit_web_view_get_settings(g_app.webview);
    webkit_settings_set_enable_developer_extras(settings, TRUE);

    gtk_widget_set_vexpand(webview, TRUE);
    gtk_widget_set_hexpand(webview, TRUE);

    gtk_window_set_child(GTK_WINDOW(window), webview);

    webkit_web_view_load_html(g_app.webview, INDEX_HTML, nullptr);

    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char* argv[]) {
    g_app.canMgr = std::make_unique<CanManager>();

    GtkApplication* app = gtk_application_new("com.can.upgrade", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
