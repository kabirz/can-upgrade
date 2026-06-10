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
            let in_body = (10..22).contains(&x) && (10..22).contains(&y);
            let in_notch = (10..13).contains(&x) && (10..13).contains(&y);
            let pin_top = (13..19).contains(&x) && (7..10).contains(&y);
            let pin_bot = (13..19).contains(&x) && (22..25).contains(&y);
            let pin_left = (7..10).contains(&x) && (13..19).contains(&y);
            let pin_right = (22..25).contains(&x) && (13..19).contains(&y);
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
