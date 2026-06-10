use crate::can_manager::{CanManager, DeviceEntry};
use crate::pcan;
use dioxus::prelude::*;
use std::sync::mpsc;
use std::sync::Mutex;
use std::time::Duration;

pub enum CanCommand {
    RefreshDevices,
    Connect { device_id: String, baud_idx: usize },
    Disconnect,
    GetVersion,
    Reboot,
    Flash { file_path: String, test_mode: bool },
}

pub enum CanEvent {
    Log(String),
    Progress(u8),
    Connected,
    Disconnected,
    DeviceList(Vec<DeviceEntry>),
    Version(u32),
    FlashResult(bool),
    Error(String),
}

static EVENT_QUEUE: once_cell::sync::Lazy<Mutex<Vec<CanEvent>>> =
    once_cell::sync::Lazy::new(|| Mutex::new(Vec::new()));

fn push_event(evt: CanEvent) {
    if let Ok(mut q) = EVENT_QUEUE.lock() {
        q.push(evt);
    }
}

fn drain_events() -> Vec<CanEvent> {
    if let Ok(mut q) = EVENT_QUEUE.lock() {
        std::mem::take(&mut *q)
    } else {
        Vec::new()
    }
}

fn can_worker(rx: std::sync::mpsc::Receiver<CanCommand>) {
    let mut manager = CanManager::new();

    manager.set_log_callback(move |msg| {
        push_event(CanEvent::Log(msg));
    });

    manager.set_progress_callback(move |pct| {
        push_event(CanEvent::Progress(pct));
    });

    manager.init_backend();

    {
        let devices = manager.detect_devices();
        push_event(CanEvent::DeviceList(devices));
    }

    for cmd in rx {
        match cmd {
            CanCommand::RefreshDevices => {
                let devices = manager.detect_devices();
                push_event(CanEvent::DeviceList(devices));
            }
            CanCommand::Connect {
                device_id,
                baud_idx,
            } => {
                if device_id.is_empty() {
                    push_event(CanEvent::Error("无效的设备".into()));
                    continue;
                }
                match manager.connect(&device_id, baud_idx) {
                    Ok(()) => push_event(CanEvent::Connected),
                    Err(e) => push_event(CanEvent::Error(e)),
                }
            }
            CanCommand::Disconnect => {
                manager.disconnect();
                push_event(CanEvent::Disconnected);
            }
            CanCommand::GetVersion => match manager.get_firmware_version() {
                Ok(v) => push_event(CanEvent::Version(v)),
                Err(e) => push_event(CanEvent::Error(e)),
            },
            CanCommand::Reboot => {
                let _ = manager.board_reboot();
            }
            CanCommand::Flash {
                file_path,
                test_mode,
            } => match manager.firmware_upgrade(&file_path, test_mode) {
                Ok(()) => {
                    push_event(CanEvent::Log("固件升级完成".into()));
                    push_event(CanEvent::FlashResult(true));
                }
                Err(e) => {
                    push_event(CanEvent::Error(e));
                    push_event(CanEvent::FlashResult(false));
                }
            },
        }
    }
}

#[derive(Clone, PartialEq)]
struct DeviceInfo {
    id: String,
    name: String,
}

#[component]
pub fn App() -> Element {
    let mut log_lines = use_signal(Vec::<String>::new);
    let mut progress = use_signal(|| 0u8);
    let mut devices = use_signal(Vec::<DeviceInfo>::new);
    let mut connected = use_signal(|| false);
    let mut selected_device = use_signal(|| 0usize);
    let mut selected_baud = use_signal(|| 5usize);
    let mut firmware_path = use_signal(String::new);
    let mut test_mode = use_signal(|| false);
    let mut version_str = use_signal(|| "未获取".to_string());
    let mut is_updating = use_signal(|| false);
    let mut is_version_querying = use_signal(|| false);

    let baud_rates: &[&str] = pcan::BAUD_RATE_NAMES;

    let mut cmd_tx = use_signal(|| {
        let (tx, rx) = mpsc::channel::<CanCommand>();
        std::thread::spawn(move || can_worker(rx));
        tx
    });

    let timestamp = || chrono::Local::now().format("[%H:%M:%S] ").to_string();

    use_future(move || async move {
        loop {
            let events = drain_events();
            for evt in events {
                match evt {
                    CanEvent::Log(msg) => {
                        log_lines.write().push(timestamp() + &msg);
                    }
                    CanEvent::Progress(pct) => {
                        progress.set(pct);
                    }
                    CanEvent::Connected => {
                        connected.set(true);
                        is_updating.set(false);
                    }
                    CanEvent::Disconnected => {
                        connected.set(false);
                        version_str.set("未获取".to_string());
                    }
                    CanEvent::DeviceList(list) => {
                        let devs: Vec<DeviceInfo> = list
                            .into_iter()
                            .map(|entry| DeviceInfo {
                                id: entry.id,
                                name: entry.name,
                            })
                            .collect();
                        devices.set(devs);
                    }
                    CanEvent::Version(v) => {
                        let major = (v >> 24) & 0xFF;
                        let minor = (v >> 16) & 0xFF;
                        let patch = (v >> 8) & 0xFF;
                        version_str.set(format!("v{}.{}.{}", major, minor, patch));
                        is_version_querying.set(false);
                    }
                    CanEvent::FlashResult(success) => {
                        is_updating.set(false);
                        if success {
                            let reboot = rfd::MessageDialog::new()
                                .set_title("固件升级")
                                .set_description("固件烧写完成，是否重启板卡？")
                                .set_buttons(rfd::MessageButtons::YesNo)
                                .show();
                            if reboot == rfd::MessageDialogResult::Yes {
                                progress.set(0);
                                let _ = cmd_tx.write().send(CanCommand::Reboot);
                            }
                        }
                    }
                    CanEvent::Error(e) => {
                        log_lines
                            .write()
                            .push(timestamp() + &format!("错误: {}", e));
                        is_updating.set(false);
                        is_version_querying.set(false);
                    }
                }
            }
            async_std::task::sleep(Duration::from_millis(30)).await;
        }
    });

    let on_refresh = move |_| {
        let _ = cmd_tx.write().send(CanCommand::RefreshDevices);
    };

    let on_connect = move |_| {
        let tx = cmd_tx.write();
        if *connected.read() {
            let _ = tx.send(CanCommand::Disconnect);
        } else {
            let idx = *selected_device.read();
            let device_id = devices
                .read()
                .get(idx)
                .map(|d| d.id.clone())
                .unwrap_or_default();
            let baud_idx = *selected_baud.read();
            let _ = tx.send(CanCommand::Connect {
                device_id,
                baud_idx,
            });
        }
    };

    let on_get_version = move |_| {
        is_version_querying.set(true);
        let _ = cmd_tx.write().send(CanCommand::GetVersion);
    };

    let on_reboot = move |_| {
        let confirm = rfd::MessageDialog::new()
            .set_title("确认重启")
            .set_description("确定要重启板卡吗？")
            .set_buttons(rfd::MessageButtons::YesNo)
            .show();
        if confirm == rfd::MessageDialogResult::Yes {
            progress.set(0);
            let _ = cmd_tx.write().send(CanCommand::Reboot);
        }
    };

    let on_flash = move |_| {
        let fp = firmware_path.peek().clone();
        if fp.is_empty() {
            log_lines.write().push("请先选择固件文件".into());
            return;
        }
        is_updating.set(true);
        progress.set(0);
        let tm = *test_mode.read();
        let _ = cmd_tx.write().send(CanCommand::Flash {
            file_path: fp,
            test_mode: tm,
        });
    };

    let on_clear_log = move |_| {
        log_lines.write().clear();
    };

    let has_file = !firmware_path.read().is_empty();
    let can_flash = *connected.read() && has_file && !*is_updating.read();

    let device_names: Vec<String> = devices.read().iter().map(|d| d.name.clone()).collect();

    let log_text = log_lines.read().join("\n");

    let ver = version_str.read().clone();
    let ver_color = if ver == "未获取" {
        "color: #8B0000;"
    } else {
        "color: #1B8A1B;"
    };

    rsx! {
        div { class: "container",

            div { class: "card toolbar",

                label { "CAN 设备:" }
                select { class: "device-select",
                    disabled: *connected.read(),
                    value: "{*selected_device.read()}",
                    onchange: move |e| {
                        if let Ok(idx) = e.value().parse::<usize>() {
                            selected_device.set(idx);
                        }
                    },
                    for (i, name) in device_names.iter().enumerate() {
                        option { value: "{i}", "{name}" }
                    }
                }

                label { "波特率:" }
                select { class: "baud-select",
                    disabled: *connected.read(),
                    value: "{*selected_baud.read()}",
                    onchange: move |e| {
                        if let Ok(idx) = e.value().parse::<usize>() {
                            selected_baud.set(idx);
                        }
                    },
                    for (i, name) in baud_rates.iter().enumerate() {
                        option { value: "{i}", "{name}" }
                    }
                }

                div { class: "pull-right",
                    button { class: "btn",
                        disabled: *connected.read(),
                        onclick: on_refresh,
                        "刷新"
                    }
                    button { class: if *connected.read() { "btn btn-danger" } else { "btn btn-primary" },
                        onclick: on_connect,
                        if *connected.read() { "断开" } else { "连接" }
                    }
                }
            }

            div { class: "card firmware-card",

                div { class: "firmware-row",
                    input { class: "input", readonly: true,
                        value: "{*firmware_path.read()}",
                        placeholder: "选择固件 .bin 文件...",
                    }
                    button { class: "btn",
                        onclick: move |_| {
                            if let Some(p) = rfd::FileDialog::new()
                                .add_filter("固件文件", &["bin"])
                                .add_filter("所有文件", &["*"])
                                .set_title("选择固件文件")
                                .pick_file()
                            {
                                firmware_path.set(p.to_string_lossy().to_string());
                            }
                        },
                        disabled: *is_updating.read(),
                        "浏览..."
                    }
                }

                div { class: "firmware-row",
                    label { class: "version-label",
                        "版本号: "
                        span { style: "{ver_color}", "{ver}" }
                    }
                    div { class: "pull-right",
                        button { class: "btn",
                            disabled: !*connected.read() || *is_updating.read() || *is_version_querying.read(),
                            onclick: on_get_version,
                            "获取版本"
                        }
                        button { class: "btn btn-warning",
                            disabled: !*connected.read() || *is_updating.read(),
                            onclick: on_reboot,
                            "重启板卡"
                        }
                        label { class: "checkbox-label",
                            input { r#type: "checkbox",
                                checked: *test_mode.read(),
                                oninput: move |e| { test_mode.set(e.checked()); },
                            }
                            " 测试模式"
                        }
                        button { class: "btn btn-success",
                            disabled: !can_flash,
                            onclick: on_flash,
                            if *is_updating.read() { "升级中..." } else { "开始升级" }
                        }
                    }
                }

                div { class: "progress-bar",
                    div { class: "progress-fill", style: "width: {*progress.read()}%;" }
                    div { class: "progress-text", "{*progress.read()}%" }
                }
            }

            fieldset { class: "fieldset",
                legend { "日志" }
                div { class: "log-box", "{log_text}" }
                div { style: "display:flex;justify-content:flex-end;margin-top:4px;",
                    button { class: "btn btn-sm", onclick: on_clear_log, "清除" }
                }
            }
        }
    }
}
