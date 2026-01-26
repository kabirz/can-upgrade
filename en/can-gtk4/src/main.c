#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "can_manager.h"

// Widget pointers
typedef struct {
    GtkApplication *app;
    GtkBuilder *builder;

    // Main widgets
    GtkWindow *main_window;
    GtkDropDown *combo_channel;
    GtkDropDown *combo_baudrate;
    GtkEntry *edit_firmware;
    GtkCheckButton *check_testmode;
    GtkProgressBar *progress_bar;
    GtkTextView *textview_log;
    GtkLabel *label_version;

    // Buttons
    GtkButton *button_refresh;
    GtkButton *button_connect;
    GtkButton *button_browse;
    GtkButton *button_upgrade;
    GtkButton *button_getversion;
    GtkButton *button_reboot;
    GtkButton *button_clear_log;

    // State
    int isConnected;
    int isUpdating;
    int upgradeSuccess;
    int last_progress_percent;
    CanManager *canManager;
    char ifNames[MAX_CAN_INTERFACES][MAX_IFNAME_LEN];
    int ifCount;

    // Firmware file
    char *firmwareFile;
} AppData;

// Forward declarations
static void show_alert_dialog(GtkWindow *parent, const char *title, const char *message);

// Baud rate configuration
static const struct {
    CanBaudRate value;
    const char *name;
} BAUD_RATES[] = {
    {CAN_BAUD_10K, "10K"},
    {CAN_BAUD_20K, "20K"},
    {CAN_BAUD_50K, "50K"},
    {CAN_BAUD_100K, "100K"},
    {CAN_BAUD_125K, "125K"},
    {CAN_BAUD_250K, "250K"},
    {CAN_BAUD_500K, "500K"},
    {CAN_BAUD_1M, "1000K"}
};

// Progress update data for main thread
typedef struct {
    GtkProgressBar *progress_bar;
    int percent;
} ProgressUpdateData;

// Main thread callback for progress update
static gboolean UpdateProgressInMainThread(gpointer user_data) {
    ProgressUpdateData *data = user_data;
    char buf[32];
    gtk_progress_bar_set_fraction(data->progress_bar, data->percent / 100.0);
    snprintf(buf, sizeof(buf), "%d%%", data->percent);
    gtk_progress_bar_set_text(data->progress_bar, buf);
    g_free(data);
    return G_SOURCE_REMOVE;
}

// Progress callback (called from worker thread)
// Throttled: only update when progress changes by at least 1%
static void OnProgressUpdate(int percent, gpointer user_data) {
    AppData *data = user_data;
    if (!data || !data->progress_bar) return;

    // Throttle: only update if progress changed significantly
    if (percent <= data->last_progress_percent) return;
    if (percent - data->last_progress_percent < 1 && percent != 100) return;

    data->last_progress_percent = percent;

    // Schedule UI update on main thread using g_idle_add
    ProgressUpdateData *updateData = g_new(ProgressUpdateData, 1);
    updateData->progress_bar = data->progress_bar;
    updateData->percent = percent;
    g_idle_add(UpdateProgressInMainThread, updateData);
}

// Append log callback wrapper
static void LogCallback(const char *msg, gpointer user_data) {
    AppData *data = user_data;
    GtkTextView *textview = data->textview_log;
    if (!textview) return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(textview);

    // Get current time
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", tm_info);

    // Insert text at end
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(buffer, &iter);
    gtk_text_buffer_insert(buffer, &iter, timestamp, -1);
    gtk_text_buffer_insert(buffer, &iter, msg, -1);
    gtk_text_buffer_insert(buffer, &iter, "\n", -1);

    // Scroll to end
    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_to_mark(textview, mark, 0.0, FALSE, 0.0, 1.0);
}

// Refresh device list
static void RefreshDeviceList(AppData *data) {
    // Detect CAN interfaces
    data->ifCount = CanManager_DetectDevice(data->canManager, data->ifNames, MAX_CAN_INTERFACES);

    // Create string list for dropdown
    GStrvBuilder *builder = g_strv_builder_new();

    // Add detected interfaces
    for (int i = 0; i < data->ifCount; i++) {
        g_strv_builder_add(builder, data->ifNames[i]);
    }

    // Set dropdown model
    char **strv = g_strv_builder_end(builder);
    GtkStringList *list = gtk_string_list_new((const char * const *)strv);
    gtk_drop_down_set_model(data->combo_channel, G_LIST_MODEL(list));
    g_strfreev(strv);
    g_strv_builder_unref(builder);

    if (data->ifCount > 0) {
        gtk_drop_down_set_selected(data->combo_channel, 0);
    }
}

// Update button states
static void UpdateButtonStates(AppData *data) {
    const char *firmware = gtk_editable_get_text(GTK_EDITABLE(data->edit_firmware));
    int hasFile = (firmware && strlen(firmware) > 0);

    gtk_widget_set_sensitive(GTK_WIDGET(data->button_upgrade),
                             data->isConnected && hasFile && !data->isUpdating);
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_getversion), data->isConnected);
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_reboot), data->isConnected);
    gtk_widget_set_sensitive(GTK_WIDGET(data->combo_channel), !data->isConnected);
    gtk_widget_set_sensitive(GTK_WIDGET(data->combo_baudrate), !data->isConnected);
}

// Show alert dialog
static void show_alert_dialog(GtkWindow *parent, const char *title, const char *message) {
    GtkAlertDialog *alert = gtk_alert_dialog_new("%s", title);
    gtk_alert_dialog_set_message(alert, message);
    const char *buttons[] = {"OK", NULL};
    gtk_alert_dialog_set_buttons(alert, buttons);
    gtk_alert_dialog_show(alert, parent);
    g_object_unref(alert);
}

// File dialog callback
static void on_file_dialog_open_finish(GtkFileDialog *dialog, GAsyncResult *result, gpointer user_data) {
    AppData *data = user_data;
    GFile *file = gtk_file_dialog_open_finish(dialog, result, NULL);
    if (file) {
        char *path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(data->edit_firmware), path);

        if (data->firmwareFile) free(data->firmwareFile);
        data->firmwareFile = strdup(path);

        g_free(path);
        g_object_unref(file);
        UpdateButtonStates(data);
    }
    g_object_unref(dialog);
}

// Connect/Disconnect button handler
static void on_connect_clicked(GtkButton *button, AppData *data) {
    (void)button;

    if (data->isConnected) {
        // Disconnect
        CanManager_Disconnect(data->canManager);
        data->isConnected = 0;
        gtk_button_set_label(data->button_connect, "Connect");
        gtk_label_set_text(data->label_version, "Ver: NA");
        UpdateButtonStates(data);
    } else {
        // Connect
        guint if_idx = gtk_drop_down_get_selected(data->combo_channel);
        guint baud_idx = gtk_drop_down_get_selected(data->combo_baudrate);

        if (if_idx < (guint)data->ifCount && baud_idx < 8) {
            int res = CanManager_Connect(data->canManager,
                                        data->ifNames[if_idx],
                                        BAUD_RATES[baud_idx].value);
            if (res) {
                data->isConnected = 1;
                gtk_button_set_label(data->button_connect, "Disconnect");
                UpdateButtonStates(data);
            } else {
                show_alert_dialog(data->main_window, "CAN Connection Failed",
                    "Connection Failed\n\nPlease check if the CAN interface exists\n"
                    "and is properly configured.\n\n"
                    "Use 'ip link show' to check available interfaces.");
            }
        }
    }
}

// Refresh button handler
static void on_refresh_clicked(GtkButton *button, AppData *data) {
    (void)button;
    RefreshDeviceList(data);
}

// Browse button handler
static void on_browse_clicked(GtkButton *button, AppData *data) {
    (void)button;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Firmware File");

    // Add filter for .bin files
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Firmware Files");
    gtk_file_filter_add_pattern(filter, "*.bin");
    g_list_store_append(filters, filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    g_list_store_append(filters, all_filter);

    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dialog, GTK_WINDOW(data->main_window), NULL,
                         (GAsyncReadyCallback)on_file_dialog_open_finish, data);
}

// Get Version button handler
static void on_getversion_clicked(GtkButton *button, AppData *data) {
    (void)button;
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_getversion), FALSE);
    uint32_t version = CanManager_GetFirmwareVersion(data->canManager);
    if (version) {
        char verMsg[64];
        snprintf(verMsg, sizeof(verMsg), "Ver: v%u.%u.%u",
                 (version >> 24) & 0xFF, (version >> 16) & 0xFF, (version >> 8) & 0xFF);
        gtk_label_set_text(data->label_version, verMsg);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_getversion), TRUE);
}

// Reboot button handler
static void on_reboot_clicked(GtkButton *button, AppData *data) {
    (void)button;
    CanManager_BoardReboot(data->canManager);
}

// Clear Log button handler
static void on_clear_log_clicked(GtkButton *button, AppData *data) {
    (void)button;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(data->textview_log);
    gtk_text_buffer_set_text(buffer, "", -1);
}

// Idle callback for upgrade completion
static gboolean on_upgrade_complete(gpointer user_data) {
    AppData *data = user_data;
    int success = data->upgradeSuccess;
    data->isUpdating = 0;

    gtk_widget_set_sensitive(GTK_WIDGET(data->button_browse), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(data->check_testmode), TRUE);
    UpdateButtonStates(data);

    // Show result dialog
    if (success) {
        show_alert_dialog(data->main_window, "Success",
            "Firmware upgrade completed! Please reboot the board");
    } else {
        show_alert_dialog(data->main_window, "Failed",
            "Firmware upgrade failed, please check the log");
    }

    return G_SOURCE_REMOVE;
}

// Upgrade thread function
typedef struct {
    AppData *data;
    char *fileName;
    int testMode;
} UpgradeParams;

static gpointer FirmwareUpgradeThread(gpointer user_data) {
    UpgradeParams *params = user_data;

    CanManager_SetProgressCallback(params->data->canManager, (progressCallback)OnProgressUpdate, params->data);
    params->data->upgradeSuccess = CanManager_FirmwareUpgrade(params->data->canManager,
                                                              params->fileName, params->testMode);

    // Update UI on main thread
    g_idle_add(on_upgrade_complete, params->data);

    free(params->fileName);
    free(params);
    return NULL;
}

// Upgrade button handler
static void on_upgrade_clicked(GtkButton *button, AppData *data) {
    (void)button;
    const char *fileName = gtk_editable_get_text(GTK_EDITABLE(data->edit_firmware));

    if (!fileName || strlen(fileName) == 0) {
        show_alert_dialog(data->main_window, "Tip", "Please select a firmware file first");
        return;
    }

    if (data->isUpdating) {
        show_alert_dialog(data->main_window, "Tip", "Firmware update in progress, please wait");
        return;
    }

    int testMode = gtk_check_button_get_active(data->check_testmode);

    data->isUpdating = 1;
    data->last_progress_percent = -1;
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_upgrade), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(data->button_browse), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(data->check_testmode), FALSE);

    gtk_progress_bar_set_fraction(data->progress_bar, 0.0);
    gtk_progress_bar_set_text(data->progress_bar, "0%");

    UpgradeParams *params = malloc(sizeof(UpgradeParams));
    params->data = data;
    params->fileName = strdup(fileName);
    params->testMode = testMode;

    g_thread_new("upgrade", FirmwareUpgradeThread, params);
}

// Application startup
static void on_activate(GtkApplication *app, AppData *data) {
    data->app = app;

    // Load UI from file
    data->builder = gtk_builder_new();
    GError *error = NULL;
    gtk_builder_add_from_file(data->builder, "resources/main.ui", &error);

    if (error) {
        g_printerr("Failed to load UI: %s\n", error->message);
        g_error_free(error);
        return;
    }

    // Get widgets
    data->main_window = GTK_WINDOW(gtk_builder_get_object(data->builder, "main_window"));
    data->combo_channel = GTK_DROP_DOWN(gtk_builder_get_object(data->builder, "combo_channel"));
    data->combo_baudrate = GTK_DROP_DOWN(gtk_builder_get_object(data->builder, "combo_baudrate"));
    data->edit_firmware = GTK_ENTRY(gtk_builder_get_object(data->builder, "edit_firmware"));
    data->check_testmode = GTK_CHECK_BUTTON(gtk_builder_get_object(data->builder, "check_testmode"));
    data->progress_bar = GTK_PROGRESS_BAR(gtk_builder_get_object(data->builder, "progress_bar"));
    data->textview_log = GTK_TEXT_VIEW(gtk_builder_get_object(data->builder, "textview_log"));
    data->label_version = GTK_LABEL(gtk_builder_get_object(data->builder, "label_version"));

    data->button_refresh = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_refresh"));
    data->button_connect = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_connect"));
    data->button_browse = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_browse"));
    data->button_upgrade = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_upgrade"));
    data->button_getversion = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_getversion"));
    data->button_reboot = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_reboot"));
    data->button_clear_log = GTK_BUTTON(gtk_builder_get_object(data->builder, "button_clear_log"));

    // Connect signals
    g_signal_connect(data->button_refresh, "clicked", G_CALLBACK(on_refresh_clicked), data);
    g_signal_connect(data->button_connect, "clicked", G_CALLBACK(on_connect_clicked), data);
    g_signal_connect(data->button_browse, "clicked", G_CALLBACK(on_browse_clicked), data);
    g_signal_connect(data->button_upgrade, "clicked", G_CALLBACK(on_upgrade_clicked), data);
    g_signal_connect(data->button_getversion, "clicked", G_CALLBACK(on_getversion_clicked), data);
    g_signal_connect(data->button_reboot, "clicked", G_CALLBACK(on_reboot_clicked), data);
    g_signal_connect(data->button_clear_log, "clicked", G_CALLBACK(on_clear_log_clicked), data);

    // Initialize baud rate dropdown
    GStrvBuilder *builder = g_strv_builder_new();
    for (int i = 0; i < 8; i++) {
        g_strv_builder_add(builder, BAUD_RATES[i].name);
    }
    char **strv = g_strv_builder_end(builder);
    GtkStringList *list = gtk_string_list_new((const char * const *)strv);
    gtk_drop_down_set_model(data->combo_baudrate, G_LIST_MODEL(list));
    g_strfreev(strv);
    g_strv_builder_unref(builder);
    gtk_drop_down_set_selected(data->combo_baudrate, 5);  // Default 250K

    // Set callback
    CanManager_SetCallback(data->canManager, (msgCallback)LogCallback, data);

    // Initialize device list
    RefreshDeviceList(data);

    // Update button states
    UpdateButtonStates(data);

    // Show window
    gtk_window_set_application(data->main_window, app);
    gtk_window_present(data->main_window);
}

int main(int argc, char *argv[]) {
    GtkApplication *app = gtk_application_new("com.can.upgrade", G_APPLICATION_DEFAULT_FLAGS);

    AppData *data = g_new0(AppData, 1);
    data->canManager = CanManager_Create();
    if (!data->canManager) {
        g_printerr("Cannot create CAN manager\n");
        return 1;
    }

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), data);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    CanManager_Destroy(data->canManager);
    if (data->firmwareFile) free(data->firmwareFile);
    if (data->builder) g_object_unref(data->builder);
    g_free(data);
    g_object_unref(app);

    return status;
}
