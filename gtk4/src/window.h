#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#define APP_ID "com.example.can-upgrade"

typedef struct {
    GtkApplicationWindow *parent;
    GtkDropDown *device_combo;
    GtkButton *refresh_btn;
    GtkButton *conn_btn;
    GtkButton *flash_btn;
    GtkButton *version_btn;
    GtkButton *reboot_btn;
    GtkButton *browse_btn;
    GtkEntry *file_entry;
    GtkCheckButton *test_check;
    GtkLabel *status_label;
    GtkLabel *version_label;
    GtkProgressBar *progress_bar;
    GtkTextView *log_text;
    GtkTextBuffer *log_buffer;

    void *can_sock;
    int is_connected;
    char connected_device[32];
} AppWindow;

AppWindow* app_window_new(GtkApplication *app);
void app_window_log(AppWindow *win, const char *message);

#endif /* WINDOW_H */
