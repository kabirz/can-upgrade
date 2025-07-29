#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::error::Error;

slint::include_modules!();
use rfd::FileDialog;

fn main() -> Result<(), Box<dyn Error>> {
    let ui = AppWindow::new()?;
    let app_weak = ui.as_weak();

    ui.on_request_increase_value({
        let ui_handle = app_weak.clone();
        move || {
            let ui = ui_handle.unwrap();
            ui.set_counter(ui.get_counter() + 1);
        }
    });
    ui.on_openfile(move || {
        let app = app_weak.clone();
        std::thread::spawn(move || {
            if let Some(path) = FileDialog::new()
                .add_filter("Binary Files", &["bin"])
                .pick_file() {
                let path_str = path.display().to_string();
                // 回到主线程更新 UI
                slint::invoke_from_event_loop(move || {
                    if let Some(app) = app.upgrade() {
                        app.set_file_name(path_str.into());
                    }
                })
                .unwrap();
            }
        });
    });


    ui.run()?;

    Ok(())
}
