mod app;
mod can_manager;
mod pcan;

#[cfg(target_os = "linux")]
mod socketcan;

fn make_icon() -> Option<dioxus::desktop::tao::window::Icon> {
    use dioxus::desktop::tao::window::Icon;
    let s = 32u32;
    let mut rgba = vec![0u8; (s * s * 4) as usize];
    for y in 0..s {
        for x in 0..s {
            let i = ((y * s + x) * 4) as usize;
            let in_body = x >= 10 && x < 22 && y >= 10 && y < 22;
            let in_notch = x >= 10 && x < 13 && y >= 10 && y < 13;
            let pin_top = x >= 13 && x < 19 && y >= 7 && y < 10;
            let pin_bot = x >= 13 && x < 19 && y >= 22 && y < 25;
            let pin_left = x >= 7 && x < 10 && y >= 13 && y < 19;
            let pin_right = x >= 22 && x < 25 && y >= 13 && y < 19;
            if in_notch {
                rgba[i] = 0x11; rgba[i + 1] = 0x11; rgba[i + 2] = 0x11; rgba[i + 3] = 0xFF;
            } else if in_body {
                rgba[i] = 0x19; rgba[i + 1] = 0x76; rgba[i + 2] = 0xD2; rgba[i + 3] = 0xFF;
            } else if pin_top || pin_bot || pin_left || pin_right {
                rgba[i] = 0xB0; rgba[i + 1] = 0xB0; rgba[i + 2] = 0xB0; rgba[i + 3] = 0xFF;
            }
        }
    }
    Icon::from_rgba(rgba, s, s).ok()
}

fn main() {
    use dioxus::desktop::{Config, WindowBuilder};

    let cfg = Config::new()
        .with_window(
            WindowBuilder::new()
                .with_title("固件升级工具")
                .with_inner_size(dioxus::desktop::LogicalSize::new(740.0, 600.0))
                .with_window_icon(make_icon()),
        )
        .with_menu(None)
        .with_custom_head(
            r#"<style>body { margin: 0; padding: 0; overflow: hidden; box-sizing: border-box; }</style>"#.to_string(),
        );
    dioxus::LaunchBuilder::desktop()
        .with_cfg(cfg)
        .launch(app::App);
}
