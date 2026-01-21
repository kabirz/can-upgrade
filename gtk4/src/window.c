#include "window.h"
#include "can_socket.h"
#include <string.h>

/* 进度更新回调的用户数据 */
typedef struct {
    AppWindow *win;
    size_t current;
    size_t total;
} ProgressData;

/* 日志回调的用户数据 */
typedef struct {
    AppWindow *win;
    char *message;
} LogData;

/* 前向声明 */
static void on_conn_clicked(GtkButton *btn, gpointer user_data);
static void on_refresh_devices_clicked(GtkButton *btn, gpointer user_data);
static void on_browse_clicked(GtkButton *btn, gpointer user_data);
static void on_flash_clicked(GtkButton *btn, gpointer user_data);
static void on_version_clicked(GtkButton *btn, gpointer user_data);
static void on_reboot_clicked(GtkButton *btn, gpointer user_data);
static gpointer firmware_upgrade_thread(gpointer user_data);
static gboolean update_progress_idle(gpointer user_data);
static gboolean update_log_idle(gpointer user_data);
static gboolean flash_failed_idle(gpointer user_data);

/* 更新连接状态显示 */
static void update_connection_status(AppWindow *win, gboolean connected)
{
    gtk_widget_remove_css_class(GTK_WIDGET(win->status_label), "status-disconnected");
    gtk_widget_remove_css_class(GTK_WIDGET(win->status_label), "status-connected");

    if (connected) {
        gtk_widget_add_css_class(GTK_WIDGET(win->status_label), "status-connected");
        gtk_label_set_text(win->status_label, "已连接");
    } else {
        gtk_widget_add_css_class(GTK_WIDGET(win->status_label), "status-disconnected");
        gtk_label_set_text(win->status_label, "未连接");
    }
}

/* 更新升级按钮状态（需同时满足：已连接 AND 已选择文件） */
static void update_flash_button_state(AppWindow *win)
{
    const char *file_path = gtk_editable_get_text(GTK_EDITABLE(win->file_entry));
    gboolean has_file = (file_path && strlen(file_path) > 0);
    gboolean can_flash = win->is_connected && has_file;
    gtk_widget_set_sensitive(GTK_WIDGET(win->flash_btn), can_flash);
}

/* 刷新 CAN 设备列表 */
static void refresh_device_list(AppWindow *win)
{
    char **devices;
    int count;

    devices = can_enumerate_devices(&count);

    /* 创建字符串列表 */
    GtkStringList *string_list;
    if (!devices || count == 0) {
        /* 没有找到设备，创建一个灰色占位符列表 */
        const char *empty_list[] = {"无 CAN 设备", NULL};
        string_list = gtk_string_list_new((const char * const *)empty_list);
        app_window_log(win, "未找到 CAN 设备，请先配置虚拟 CAN 或连接硬件");
    } else {
        /* 转换为字符串数组 */
        const char **strings = g_new(const char *, count + 1);
        for (int i = 0; i < count; i++) {
            strings[i] = devices[i];
        }
        strings[count] = NULL;

        string_list = gtk_string_list_new((const char * const *)strings);
        g_free(strings);
        can_free_device_list(devices, count);

        char msg[128];
        snprintf(msg, sizeof(msg), "找到 %d 个 CAN 设备", count);
        app_window_log(win, msg);
    }

    /* 创建新的下拉框 */
    GtkWidget *old_combo = GTK_WIDGET(win->device_combo);
    win->device_combo = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(string_list), NULL));

    /* 如果有设备，选择第一个；如果没有，不选择 */
    if (devices && count > 0) {
        gtk_drop_down_set_selected(win->device_combo, 0);
    } else {
        gtk_drop_down_set_selected(win->device_combo, GTK_INVALID_LIST_POSITION);
    }

    /* 替换旧的下拉框，保持在原位置 */
    GtkWidget *parent = gtk_widget_get_parent(old_combo);
    if (parent) {
        GtkBox *box = GTK_BOX(parent);
        /* 获取设备标签（下拉框的前一个兄弟） */
        GtkWidget *device_label = gtk_widget_get_first_child(GTK_WIDGET(box));
        gtk_box_remove(box, old_combo);
        /* 插入到设备标签之后 */
        gtk_box_insert_child_after(box, GTK_WIDGET(win->device_combo), device_label);
    }

    /* 设置样式，让占位符显示为灰色 */
    gtk_widget_add_css_class(GTK_WIDGET(win->device_combo), "device-dropdown");
}

/* CAN 固件升级回调 */
static void upgrade_log_cb(const char *msg, void *user_data)
{
    LogData *data = g_new0(LogData, 1);

    data->win = (AppWindow *)user_data;
    data->message = g_strdup(msg);
    g_idle_add(update_log_idle, data);
}

static void upgrade_progress_cb(size_t current, size_t total, void *user_data)
{
    ProgressData *data = g_new0(ProgressData, 1);

    data->win = (AppWindow *)user_data;
    data->current = current;
    data->total = total;
    g_idle_add(update_progress_idle, data);
}

/* 空闲回调：更新进度条 */
static gboolean update_progress_idle(gpointer user_data)
{
    ProgressData *data = (ProgressData *)user_data;
    gtk_progress_bar_set_fraction(data->win->progress_bar, (double)data->current / data->total);
    g_free(data);
    return G_SOURCE_REMOVE;
}

/* 空闲回调：更新日志 */
static gboolean update_log_idle(gpointer user_data)
{
    LogData *data = (LogData *)user_data;
    app_window_log(data->win, data->message);
    g_free(data->message);
    g_free(data);
    return G_SOURCE_REMOVE;
}

/* 空闲回调：升级失败后恢复按钮 */
static gboolean flash_failed_idle(gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    gtk_widget_set_sensitive(GTK_WIDGET(win->flash_btn), TRUE);
    return G_SOURCE_REMOVE;
}

/* 连接/断开 CAN */
static void on_conn_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;

    if (win->is_connected) {
        /* 断开连接 */
        if (win->can_sock) {
            can_socket_destroy(win->can_sock);
            win->can_sock = NULL;
        }

        win->is_connected = 0;
        win->connected_device[0] = '\0';

        gtk_button_set_label(win->conn_btn, "连接");
        gtk_widget_set_sensitive(GTK_WIDGET(win->device_combo), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(win->refresh_btn), TRUE);
        update_flash_button_state(win);
        gtk_widget_set_sensitive(GTK_WIDGET(win->version_btn), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(win->reboot_btn), FALSE);

        app_window_log(win, "已断开连接");
        update_connection_status(win, FALSE);
    } else {
        /* 连接 */
        guint device_idx = gtk_drop_down_get_selected(win->device_combo);

        /* 获取选中的设备名称 */
        GtkStringList *model = GTK_STRING_LIST(gtk_drop_down_get_model(win->device_combo));
        if (!model || device_idx == GTK_INVALID_LIST_POSITION) {
            app_window_log(win, "请选择 CAN 设备");
            return;
        }

        const char *device = gtk_string_list_get_string(model, device_idx);
        if (!device || strcmp(device, "无 CAN 设备") == 0) {
            app_window_log(win, "请选择有效的 CAN 设备");
            return;
        }

        can_socket_t *sock = can_socket_create(device);
        if (!sock) {
            app_window_log(win, "连接 CAN 失败，请检查设备");
            return;
        }

        win->can_sock = sock;
        win->is_connected = 1;
        strncpy(win->connected_device, device, sizeof(win->connected_device) - 1);

        /* 按钮显示"断开" */
        gtk_button_set_label(win->conn_btn, "断开");
        gtk_widget_set_sensitive(GTK_WIDGET(win->device_combo), FALSE);
        gtk_widget_set_sensitive(GTK_WIDGET(win->refresh_btn), FALSE);
        update_flash_button_state(win);
        gtk_widget_set_sensitive(GTK_WIDGET(win->version_btn), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(win->reboot_btn), TRUE);

        char msg[128];
        snprintf(msg, sizeof(msg), "已连接到 %s", device);
        app_window_log(win, msg);
        update_connection_status(win, TRUE);
    }
}

/* 刷新设备列表按钮点击 */
static void on_refresh_devices_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    refresh_device_list(win);
}

/* 浏览文件 - 响应回调 */
static void on_file_dialog_response(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source_object), result, NULL);

    if (file) {
        char *path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(win->file_entry), path);
        g_free(path);
        g_object_unref(file);
        update_flash_button_state(win);
    }
}

/* 浏览文件 */
static void on_browse_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    GtkFileDialog *dialog;
    GListStore *filters;
    GtkFileFilter *filter;

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "选择固件文件");

    filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "固件文件 (*.bin)");
    gtk_file_filter_add_pattern(filter, "*.bin");
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dialog, GTK_WINDOW(win->parent), NULL,
                         on_file_dialog_response, win);
}

/* 固件升级线程 */
static gpointer firmware_upgrade_thread(gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    const char *file_path = gtk_editable_get_text(GTK_EDITABLE(win->file_entry));
    int test = gtk_check_button_get_active(win->test_check);

    if (!file_path || strlen(file_path) == 0) {
        g_idle_add(flash_failed_idle, win);
        app_window_log(win, "请选择固件文件");
        return NULL;
    }

    gtk_progress_bar_set_fraction(win->progress_bar, 0);

    int ret = can_firmware_upgrade(win->can_sock, file_path, test,
                                   upgrade_progress_cb, upgrade_log_cb, win);

    g_idle_add(flash_failed_idle, win);

    return NULL;
}

/* 固件升级 */
static void on_flash_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;

    gtk_widget_set_sensitive(GTK_WIDGET(win->flash_btn), FALSE);

    g_thread_new("upgrade", firmware_upgrade_thread, win);
}

/* 获取版本 */
static void on_version_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;
    char version_str[32] = {0};

    int ret = can_firmware_get_version(win->can_sock, version_str, sizeof(version_str));
    if (ret == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "固件版本: %s", version_str);
        app_window_log(win, msg);
        gtk_label_set_text(win->version_label, version_str);
    } else {
        app_window_log(win, "获取版本失败");
    }
}

/* 重启板子 */
static void on_reboot_clicked(GtkButton *btn, gpointer user_data)
{
    AppWindow *win = (AppWindow *)user_data;

    int ret = can_board_reboot(win->can_sock);
    if (ret == 0) {
        app_window_log(win, "重启命令已发送");
    } else {
        app_window_log(win, "发送重启命令失败");
    }
}

void app_window_log(AppWindow *win, const char *message)
{
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(win->log_buffer, &iter);
    gtk_text_buffer_insert(win->log_buffer, &iter, message, -1);
    gtk_text_buffer_insert(win->log_buffer, &iter, "\n", -1);
}

AppWindow* app_window_new(GtkApplication *app)
{
    AppWindow *win = g_new0(AppWindow, 1);
    win->is_connected = 0;
    win->can_sock = NULL;
    win->connected_device[0] = '\0';

    /* 创建主窗口 */
    win->parent = GTK_APPLICATION_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(win->parent), "CAN 固件升级工具");
    gtk_window_set_default_size(GTK_WINDOW(win->parent), 480, 500);

    /* 主布局 */
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_window_set_child(GTK_WINDOW(win->parent), box);

    /* CAN 连接区域 */
    GtkWidget *conn_frame = gtk_frame_new("CAN 连接");
    gtk_box_append(GTK_BOX(box), conn_frame);
    GtkWidget *conn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(conn_box, 10);
    gtk_widget_set_margin_end(conn_box, 10);
    gtk_widget_set_margin_top(conn_box, 10);
    gtk_widget_set_margin_bottom(conn_box, 10);
    gtk_frame_set_child(GTK_FRAME(conn_frame), conn_box);

    /* 设备选择 */
    gtk_box_append(GTK_BOX(conn_box), gtk_label_new("设备:"));
    /* 初始空列表，稍后刷新 */
    const char *empty_list[] = {NULL};
    win->device_combo = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(empty_list));
    gtk_box_append(GTK_BOX(conn_box), GTK_WIDGET(win->device_combo));

    /* 刷新按钮 */
    win->refresh_btn = GTK_BUTTON(gtk_button_new_with_label("刷新"));
    g_signal_connect(win->refresh_btn, "clicked", G_CALLBACK(on_refresh_devices_clicked), win);
    gtk_box_append(GTK_BOX(conn_box), GTK_WIDGET(win->refresh_btn));

    /* 连接/断开按钮 */
    win->conn_btn = GTK_BUTTON(gtk_button_new_with_label("连接"));
    g_signal_connect(win->conn_btn, "clicked", G_CALLBACK(on_conn_clicked), win);
    gtk_box_append(GTK_BOX(conn_box), GTK_WIDGET(win->conn_btn));

    /* 状态显示 */
    gtk_box_append(GTK_BOX(conn_box), gtk_label_new("状态:"));
    win->status_label = GTK_LABEL(gtk_label_new("未连接"));
    gtk_widget_add_css_class(GTK_WIDGET(win->status_label), "status-disconnected");
    gtk_box_append(GTK_BOX(conn_box), GTK_WIDGET(win->status_label));

    /* 固件升级区域 */
    GtkWidget *flash_frame = gtk_frame_new("固件升级");
    gtk_box_append(GTK_BOX(box), flash_frame);
    GtkWidget *flash_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(flash_box, 10);
    gtk_widget_set_margin_end(flash_box, 10);
    gtk_widget_set_margin_top(flash_box, 10);
    gtk_widget_set_margin_bottom(flash_box, 10);
    gtk_frame_set_child(GTK_FRAME(flash_frame), flash_box);

    /* 文件选择 */
    GtkWidget *file_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(flash_box), file_box);

    win->file_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(win->file_entry, "选择固件文件...");
    gtk_box_append(GTK_BOX(file_box), GTK_WIDGET(win->file_entry));

    win->browse_btn = GTK_BUTTON(gtk_button_new_with_label("浏览..."));
    g_signal_connect(win->browse_btn, "clicked", G_CALLBACK(on_browse_clicked), win);
    gtk_box_append(GTK_BOX(file_box), GTK_WIDGET(win->browse_btn));

    /* 测试模式复选框 */
    win->test_check = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_check_button_set_label(win->test_check, "测试模式 (第二次重启后固件恢复成之前的固件)");
    gtk_box_append(GTK_BOX(flash_box), GTK_WIDGET(win->test_check));

    /* 进度条 */
    win->progress_bar = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_box_append(GTK_BOX(flash_box), GTK_WIDGET(win->progress_bar));

    /* 升级按钮 */
    win->flash_btn = GTK_BUTTON(gtk_button_new_with_label("开始升级"));
    g_signal_connect(win->flash_btn, "clicked", G_CALLBACK(on_flash_clicked), win);
    gtk_widget_set_sensitive(GTK_WIDGET(win->flash_btn), FALSE);
    gtk_box_append(GTK_BOX(flash_box), GTK_WIDGET(win->flash_btn));

    /* 控制按钮区域 */
    GtkWidget *ctrl_frame = gtk_frame_new("板子控制");
    gtk_box_append(GTK_BOX(box), ctrl_frame);
    GtkWidget *ctrl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(ctrl_box, 10);
    gtk_widget_set_margin_end(ctrl_box, 10);
    gtk_widget_set_margin_top(ctrl_box, 10);
    gtk_widget_set_margin_bottom(ctrl_box, 10);
    gtk_frame_set_child(GTK_FRAME(ctrl_frame), ctrl_box);

    win->version_btn = GTK_BUTTON(gtk_button_new_with_label("获取版本"));
    g_signal_connect(win->version_btn, "clicked", G_CALLBACK(on_version_clicked), win);
    gtk_widget_set_sensitive(GTK_WIDGET(win->version_btn), FALSE);
    gtk_box_append(GTK_BOX(ctrl_box), GTK_WIDGET(win->version_btn));

    win->reboot_btn = GTK_BUTTON(gtk_button_new_with_label("重启板子"));
    g_signal_connect(win->reboot_btn, "clicked", G_CALLBACK(on_reboot_clicked), win);
    gtk_widget_set_sensitive(GTK_WIDGET(win->reboot_btn), FALSE);
    gtk_box_append(GTK_BOX(ctrl_box), GTK_WIDGET(win->reboot_btn));

    gtk_box_append(GTK_BOX(ctrl_box), gtk_label_new("当前版本:"));
    win->version_label = GTK_LABEL(gtk_label_new("-"));
    gtk_box_append(GTK_BOX(ctrl_box), GTK_WIDGET(win->version_label));

    /* 日志区域 */
    GtkWidget *log_frame = gtk_frame_new("日志");
    gtk_box_append(GTK_BOX(box), log_frame);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 150);
    gtk_frame_set_child(GTK_FRAME(log_frame), scrolled);

    win->log_text = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(win->log_text, FALSE);
    gtk_text_view_set_wrap_mode(win->log_text, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(win->log_text, 5);
    gtk_text_view_set_right_margin(win->log_text, 5);
    gtk_text_view_set_top_margin(win->log_text, 5);
    gtk_text_view_set_bottom_margin(win->log_text, 5);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(win->log_text));

    win->log_buffer = gtk_text_view_get_buffer(win->log_text);

    /* 自动扫描 CAN 设备 */
    refresh_device_list(win);

    return win;
}
