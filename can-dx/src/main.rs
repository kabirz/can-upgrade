mod app;
mod can_manager;
mod pcan;

#[cfg(target_os = "linux")]
mod socketcan;

fn main() {
    use dioxus::desktop::{Config, WindowBuilder};

    let cfg = Config::new()
        .with_window(
            WindowBuilder::new()
                .with_title("固件升级工具")
                .with_inner_size(dioxus::desktop::LogicalSize::new(740.0, 600.0)),
        )
        .with_menu(None);
    dioxus::LaunchBuilder::desktop()
        .with_cfg(cfg)
        .launch(app::App);
}
