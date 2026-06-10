use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

pub const PLATFORM_RX: u32 = 0x101;
pub const PLATFORM_TX: u32 = 0x102;
pub const FW_DATA_RX: u32 = 0x103;

pub const BOARD_START_UPDATE: u32 = 0;
pub const BOARD_CONFIRM: u32 = 1;
pub const BOARD_VERSION: u32 = 2;
pub const BOARD_REBOOT: u32 = 3;

pub const FW_CODE_OFFSET: u32 = 0;
pub const FW_CODE_UPDATE_SUCCESS: u32 = 1;
pub const FW_CODE_VERSION: u32 = 2;
pub const FW_CODE_CONFIRM: u32 = 3;
#[allow(dead_code)]
pub const FW_CODE_FLASH_ERROR: u32 = 4;
pub const FW_CODE_TRANSFER_ERROR: u32 = 5;

#[derive(Clone)]
pub struct DeviceEntry {
    pub id: String,
    pub name: String,
}

pub struct CanManager {
    #[cfg(target_os = "windows")]
    pcan_dll: Option<Arc<Mutex<crate::pcan::PcanDll>>>,
    #[cfg(target_os = "linux")]
    socketcan_bus: Option<Arc<Mutex<crate::socketcan::SocketCanBus>>>,

    connected: bool,
    connected_channel: String,

    log_callback: Option<Box<dyn Fn(String) + Send>>,
    progress_callback: Option<Box<dyn Fn(u8) + Send>>,
}

impl CanManager {
    pub fn new() -> Self {
        CanManager {
            #[cfg(target_os = "windows")]
            pcan_dll: None,
            #[cfg(target_os = "linux")]
            socketcan_bus: None,
            connected: false,
            connected_channel: String::new(),
            log_callback: None,
            progress_callback: None,
        }
    }

    pub fn set_log_callback<F: Fn(String) + Send + 'static>(&mut self, cb: F) {
        self.log_callback = Some(Box::new(cb));
    }

    pub fn set_progress_callback<F: Fn(u8) + Send + 'static>(&mut self, cb: F) {
        self.progress_callback = Some(Box::new(cb));
    }

    fn log(&self, msg: &str) {
        if let Some(ref cb) = self.log_callback {
            cb(msg.to_string());
        }
    }

    fn progress(&self, pct: u8) {
        if let Some(ref cb) = self.progress_callback {
            cb(pct);
        }
    }

    #[allow(dead_code)]
    pub fn is_connected(&self) -> bool {
        self.connected
    }

    // ── Platform init ─────────────────────────────────────

    #[cfg(target_os = "windows")]
    pub fn init_backend(&mut self) {
        match crate::pcan::PcanDll::load(None) {
            Ok(dll) => {
                self.pcan_dll = Some(Arc::new(Mutex::new(dll)));
                self.log("PCANBasic.dll 加载成功");
            }
            Err(e) => {
                self.log(&format!("PCANBasic.dll 加载失败: {}", e));
            }
        }
    }

    #[cfg(target_os = "linux")]
    pub fn init_backend(&mut self) {
        let ifaces = crate::socketcan::SocketCanBus::list_interfaces();
        if ifaces.is_empty() {
            self.log("未检测到 SocketCAN 接口");
        } else {
            for iface in &ifaces {
                self.log(&format!("检测到 CAN 接口: {}", iface));
            }
        }
    }

    // ── Device detection ─────────────────────────────────

    pub fn detect_devices(&self) -> Vec<DeviceEntry> {
        let mut devices = Vec::new();

        #[cfg(target_os = "windows")]
        if let Some(ref dll_arc) = self.pcan_dll {
            use crate::pcan::*;
            if let Ok(dll) = dll_arc.lock() {
                for i in 0..16i32 {
                    let query = format!("devicetype=pcan_usb,controllernumber={}", i);
                    let mut ch: u16 = PCAN_NONEBUS;
                    let status = dll.lookup_channel(&query, &mut ch);
                    if status == PCAN_ERROR_OK && ch != PCAN_NONEBUS {
                        devices.push(DeviceEntry {
                            id: format!("pcan:{}", ch),
                            name: format!("PCAN-USB: {:X}h", ch),
                        });
                    }
                }
            }
        }

        #[cfg(target_os = "linux")]
        {
            let ifaces = crate::socketcan::SocketCanBus::list_interfaces();
            for iface in ifaces {
                devices.push(DeviceEntry {
                    id: format!("socketcan:{}", iface),
                    name: format!("SocketCAN: {}", iface),
                });
            }
        }

        self.log(&format!("查询到 {} 个可用 CAN 设备", devices.len()));
        devices
    }

    // ── Connect / Disconnect ─────────────────────────────

    pub fn connect(&mut self, device_id: &str, baud_idx: usize) -> Result<(), String> {
        if self.connected {
            return Err("已经连接".into());
        }

        if let Some(ch) = device_id.strip_prefix("pcan:") {
            #[cfg(target_os = "windows")]
            {
                let channel: u16 = ch.parse().map_err(|_| "无效的 PCAN 通道".to_string())?;
                return self.connect_pcan(channel, baud_idx);
            }
            #[cfg(not(target_os = "windows"))]
            return Err("PCAN 仅支持 Windows".into());
        }

        if let Some(ifname) = device_id.strip_prefix("socketcan:") {
            #[cfg(target_os = "linux")]
            return self.connect_socketcan(ifname);
            #[cfg(not(target_os = "linux"))]
            {
                let _ = ifname;
                return Err("SocketCAN 仅支持 Linux".into());
            }
        }

        Err(format!("未知的设备类型: {}", device_id))
    }

    #[cfg(target_os = "windows")]
    fn connect_pcan(&mut self, channel: u16, baud_idx: usize) -> Result<(), String> {
        use crate::pcan::*;
        let dll_arc = self.pcan_dll.as_ref().ok_or("PCAN 驱动未初始化")?.clone();
        let dll = dll_arc.lock().map_err(|e| format!("锁错误: {}", e))?;

        let baudrate = BAUD_RATES[baud_idx];
        let status = dll.initialize(channel, baudrate);
        if status != PCAN_ERROR_OK {
            return Err(format!("CAN 初始化失败, 状态码: 0x{:X}", status));
        }
        dll.filter_messages(channel, PLATFORM_TX, PLATFORM_TX, PCAN_MODE_STANDARD);
        drop(dll);

        self.connected = true;
        self.connected_channel = format!("pcan:{:X}h", channel);
        self.log(&format!("CAN(id={:X}h) 连接成功", channel));
        Ok(())
    }

    #[cfg(target_os = "linux")]
    fn connect_socketcan(&mut self, ifname: &str) -> Result<(), String> {
        let bus = crate::socketcan::SocketCanBus::open(ifname)?;
        bus.set_filter(PLATFORM_TX, PLATFORM_TX)?;
        self.socketcan_bus = Some(Arc::new(Mutex::new(bus)));
        self.connected = true;
        self.connected_channel = format!("socketcan:{}", ifname);
        self.log(&format!("SocketCAN({}) 连接成功", ifname));
        Ok(())
    }

    pub fn disconnect(&mut self) {
        if !self.connected {
            return;
        }

        let label = std::mem::take(&mut self.connected_channel);

        #[cfg(target_os = "windows")]
        if label.starts_with("pcan:") {
            if let Some(ref dll_arc) = self.pcan_dll {
                if let Some(ch_str) = label.strip_prefix("pcan:") {
                    let ch_str = ch_str.trim_end_matches('h');
                    if let Ok(ch) = u16::from_str_radix(ch_str, 16) {
                        if let Ok(dll) = dll_arc.lock() {
                            dll.uninitialize(ch);
                        }
                    }
                }
            }
        }

        #[cfg(target_os = "linux")]
        {
            self.socketcan_bus = None;
        }

        self.connected = false;
        let id_part = label.strip_prefix("pcan:").unwrap_or(&label);
        self.log(&format!("CAN(id={}) 连接已断开", id_part));
    }

    // ── CAN communication ───────────────────────────────

    fn write_frame(&self, id: u32, data: &[u8]) -> Result<(), String> {
        #[cfg(target_os = "windows")]
        if let Some(ref dll_arc) = self.pcan_dll {
            use crate::pcan::*;
            let ch: u16 = self
                .connected_channel
                .strip_prefix("pcan:")
                .and_then(|s| u16::from_str_radix(s.trim_end_matches('h'), 16).ok())
                .ok_or("未连接到 PCAN 设备")?;
            let dll = dll_arc.lock().map_err(|e| format!("锁错误: {}", e))?;
            let msg = TPCANMsg::new_data(id, data);
            let status = dll.write(ch, &msg);
            if status != PCAN_ERROR_OK {
                return Err(format!("CAN 发送失败, 状态码: 0x{:X}", status));
            }
            return Ok(());
        }

        #[cfg(target_os = "linux")]
        if let Some(ref bus_arc) = self.socketcan_bus {
            let bus = bus_arc.lock().map_err(|e| format!("锁错误: {}", e))?;
            return bus.write_frame(id, data);
        }

        Err("CAN 未连接".into())
    }

    fn send_command(&self, code: u32, val: u32) -> Result<(), String> {
        let mut data = [0u8; 8];
        data[..4].copy_from_slice(&code.to_le_bytes());
        data[4..8].copy_from_slice(&val.to_le_bytes());
        self.write_frame(PLATFORM_RX, &data)
    }

    fn read_frame(&self, timeout_ms: u64) -> Option<(u32, [u8; 8], u8)> {
        #[cfg(target_os = "windows")]
        let _start = Instant::now();

        #[cfg(target_os = "windows")]
        if let Some(ref dll_arc) = self.pcan_dll {
            use crate::pcan::*;
            let ch = self
                .connected_channel
                .strip_prefix("pcan:")
                .and_then(|s| u16::from_str_radix(s.trim_end_matches('h'), 16).ok())?;
            let dll = dll_arc.lock().ok()?;
            loop {
                if _start.elapsed().as_millis() as u64 >= timeout_ms {
                    return None;
                }
                let mut msg = TPCANMsg {
                    id: 0,
                    msgtype: 0,
                    len: 0,
                    data: [0u8; 8],
                };
                let mut ts = TPCANTimestamp {
                    millis: 0,
                    millis_overflow: 0,
                    micros: 0,
                };
                let status = dll.read(ch, &mut msg, &mut ts);
                if status == PCAN_ERROR_OK {
                    return Some((msg.id, msg.data, msg.len));
                }
                std::thread::sleep(Duration::from_millis(10));
            }
        }

        #[cfg(target_os = "linux")]
        if let Some(ref bus_arc) = self.socketcan_bus {
            let bus = bus_arc.lock().ok()?;
            return bus.read_frame(timeout_ms);
        }

        None
    }

    fn wait_for_response(&self, timeout_ms: u64) -> Option<(u32, u32)> {
        let start = Instant::now();
        loop {
            let remaining = timeout_ms.saturating_sub(start.elapsed().as_millis() as u64);
            if remaining == 0 {
                return None;
            }
            if let Some((id, data, _len)) = self.read_frame(50) {
                if id == PLATFORM_TX {
                    let code = u32::from_le_bytes(data[..4].try_into().unwrap_or([0; 4]));
                    let val = u32::from_le_bytes(data[4..8].try_into().unwrap_or([0; 4]));
                    return Some((code, val));
                }
            }
        }
    }

    // ── Firmware version ──────────────────────────────────

    pub fn get_firmware_version(&mut self) -> Result<u32, String> {
        if !self.connected {
            return Err("CAN 已断开连接, 请重新连接".into());
        }
        self.send_command(BOARD_VERSION, 0)?;
        match self.wait_for_response(5000) {
            Some((FW_CODE_VERSION, version)) => {
                let major = (version >> 24) & 0xFF;
                let minor = (version >> 16) & 0xFF;
                let patch = (version >> 8) & 0xFF;
                self.log(&format!("固件版本: v{}.{}.{}", major, minor, patch));
                Ok(version)
            }
            Some((code, _)) => Err(format!("CAN 读取失败, 数据错误! code={}", code)),
            None => Err("CAN 读取失败, 超时!".into()),
        }
    }

    // ── Board reboot ─────────────────────────────────────

    pub fn board_reboot(&mut self) -> Result<(), String> {
        if !self.connected {
            return Err("CAN 已断开连接, 请重新连接".into());
        }
        self.send_command(BOARD_REBOOT, 0)?;
        self.log("重启命令已发送");
        Ok(())
    }

    // ── Firmware upgrade ────────────────────────────────

    pub fn firmware_upgrade(&mut self, file_path: &str, test_mode: bool) -> Result<(), String> {
        if !self.connected {
            return Err("CAN 已断开连接, 请重新连接".into());
        }
        let firmware_data = std::fs::read(file_path).map_err(|e| format!("无法打开文件: {}", e))?;
        let file_size = firmware_data.len();
        self.log(&format!("开始固件升级, 固件大小: {} 字节", file_size));

        self.send_command(BOARD_START_UPDATE, file_size as u32)?;
        match self.wait_for_response(15000) {
            Some((FW_CODE_OFFSET, 0)) => {}
            Some((code, val)) => {
                return Err(format!("Flash 擦除失败: code({}), offset({})", code, val));
            }
            None => return Err("Flash 擦除超时".into()),
        }

        let mut bytes_sent = 0usize;
        for chunk in firmware_data.chunks(8) {
            self.write_frame(FW_DATA_RX, chunk)?;
            bytes_sent += chunk.len();
            if bytes_sent.is_multiple_of(64) || bytes_sent == file_size {
                self.progress((bytes_sent * 100 / file_size) as u8);
                match self.wait_for_response(5000) {
                    Some((FW_CODE_UPDATE_SUCCESS, offset)) if offset == bytes_sent as u32 => break,
                    Some((FW_CODE_OFFSET, _)) => {}
                    Some((code, val)) => {
                        return Err(format!("固件升级失败: code({}), offset({})", code, val));
                    }
                    None => return Err("固件更新超时!".into()),
                }
            }
        }

        self.progress(100);
        self.send_command(BOARD_CONFIRM, if test_mode { 0 } else { 1 })?;
        match self.wait_for_response(30000) {
            Some((FW_CODE_CONFIRM, 0x55AA55AA)) => {
                self.log(&format!(
                    "固件 {} 上传完成. 点击重启，板卡将在45-60秒内完成重启",
                    file_path
                ));
                Ok(())
            }
            Some((FW_CODE_TRANSFER_ERROR, _)) => Err("固件更新失败 (传输错误)".into()),
            Some((code, val)) => Err(format!("固件确认失败: code({}), val(0x{:X})", code, val)),
            None => Err("固件确认超时!".into()),
        }
    }
}
