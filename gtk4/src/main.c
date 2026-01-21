#include "window.h"
#include <gtk/gtk.h>

/* 加载 CSS 样式 */
static void load_css(void)
{
    GtkCssProvider *provider;
    GdkDisplay *display;
    const char *css =
        ".device-dropdown dropdown button {"
        "   opacity: 0.7;"
        "}"
        ".device-dropdown dropdown button label {"
        "   color: rgba(128, 128, 128, 0.8);"
        "}"
        ".status-disconnected {"
        "   color: #e74c3c;"
        "   font-weight: bold;"
        "}"
        ".status-connected {"
        "   color: #27ae60;"
        "   font-weight: bold;"
        "}";

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);

    display = gdk_display_get_default();
    gtk_style_context_add_provider_for_display(display,
                                                GTK_STYLE_PROVIDER(provider),
                                                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
}

static void on_activate(GtkApplication *app, gpointer user_data)
{
    load_css();
    AppWindow *win = app_window_new(app);
    gtk_window_present(GTK_WINDOW(win->parent));
}

int main(int argc, char *argv[])
{
    GtkApplication *app;
    int status;

    app = gtk_application_new(APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
