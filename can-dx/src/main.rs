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
                rgba[i] = 0x11;
                rgba[i + 1] = 0x11;
                rgba[i + 2] = 0x11;
                rgba[i + 3] = 0xFF;
            } else if in_body {
                rgba[i] = 0x19;
                rgba[i + 1] = 0x76;
                rgba[i + 2] = 0xD2;
                rgba[i + 3] = 0xFF;
            } else if pin_top || pin_bot || pin_left || pin_right {
                rgba[i] = 0xB0;
                rgba[i + 1] = 0xB0;
                rgba[i + 2] = 0xB0;
                rgba[i + 3] = 0xFF;
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
        .with_custom_head(r#"
<style>
:root {
  --bg: #f0f2f5; --card: #fff; --text: #333;
  --border: #d9d9d9;
  --primary: #1677ff; --primary-hover: #4096ff;
  --success: #52c41a; --success-hover: #73d13d;
  --danger: #ff4d4f; --danger-hover: #ff7875;
  --warning: #faad14;
  --disabled-bg: #f5f5f5; --disabled-text: #bfbfbf;
  --radius: 6px;
  --shadow: 0 1px 2px rgba(0,0,0,.06);
  --font: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Microsoft YaHei', sans-serif;
  --mono: 'Cascadia Code', 'Consolas', 'Courier New', monospace;
}
*,*::before,*::after{box-sizing:border-box}
body{margin:0;padding:0;font-family:var(--font);background:var(--bg);color:var(--text);overflow:hidden}
.container{display:flex;flex-direction:column;height:100vh;padding:12px;gap:10px}
.card{background:var(--card);border-radius:var(--radius);padding:10px 14px;box-shadow:var(--shadow)}
.toolbar{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.toolbar>label{white-space:nowrap}
.pull-right{margin-left:auto;display:flex;gap:8px;align-items:center}
.firmware-card{display:flex;flex-direction:column;gap:10px}
.firmware-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
select{padding:4px 8px;border:1px solid var(--border);border-radius:4px;background:var(--card);font-size:14px;outline:none;transition:border-color .2s}
select:focus{border-color:var(--primary)}
select:disabled{background:var(--disabled-bg);color:var(--disabled-text);cursor:not-allowed}
.device-select{min-width:200px}
.baud-select{min-width:100px}
.input{flex:1;padding:4px 8px;border:1px solid var(--border);border-radius:4px;font-size:14px;outline:none;background:#fafafa}
.btn{display:inline-flex;align-items:center;justify-content:center;padding:4px 14px;border:1px solid var(--border);border-radius:4px;background:var(--card);color:var(--text);font-size:14px;cursor:pointer;white-space:nowrap;transition:all .2s;line-height:1.4;user-select:none}
.btn:hover{border-color:var(--primary);color:var(--primary)}
.btn:active{opacity:.85}
.btn:disabled{background:var(--disabled-bg);color:var(--disabled-text);border-color:var(--border);cursor:not-allowed;opacity:1}
.btn-primary{background:var(--primary);border-color:var(--primary);color:#fff}
.btn-primary:hover{background:var(--primary-hover);border-color:var(--primary-hover);color:#fff}
.btn-danger{background:var(--danger);border-color:var(--danger);color:#fff}
.btn-danger:hover{background:var(--danger-hover);border-color:var(--danger-hover);color:#fff}
.btn-success{background:var(--success);border-color:var(--success);color:#fff;font-weight:600;padding:6px 22px;border:none}
.btn-success:hover{background:var(--success-hover)}
.btn-success:disabled{background:#d9d9d9;color:#999;cursor:not-allowed}
.btn-warning{background:var(--warning);border-color:var(--warning);color:#fff}
.btn-warning:hover{background:#ffc53d;border-color:#ffc53d}
.btn-sm{padding:2px 10px;font-size:13px}
.fieldset{flex:1;display:flex;flex-direction:column;border:1px solid var(--border);border-radius:var(--radius);padding:10px 12px;min-height:0;background:var(--card)}
.fieldset legend{font-size:14px;font-weight:500;padding:0 6px}
.log-box{flex:1;overflow-y:auto;background:#fafafa;padding:8px;border-radius:4px;font-family:var(--mono);font-size:13px;white-space:pre-wrap;word-break:break-all;min-height:100px;border:1px solid var(--border)}
.progress-bar{height:22px;background:#e9ecef;border-radius:4px;overflow:hidden;position:relative}
.progress-fill{height:100%;background:linear-gradient(90deg,var(--primary),#69b1ff);transition:width .3s ease;border-radius:4px}
.progress-text{position:absolute;top:0;left:0;right:0;bottom:0;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:600}
.checkbox-label{display:flex;align-items:center;gap:4px;cursor:pointer;font-size:14px}
.checkbox-label input{cursor:pointer;margin:0}
.version-label{font-weight:600;min-width:90px;font-size:14px}
label{font-size:14px;white-space:nowrap}
</style>"#.to_string());
    dioxus::LaunchBuilder::desktop()
        .with_cfg(cfg)
        .launch(app::App);
}
