//! PCAN-Basic FFI types and dynamic library loader
//!
//! Provides safe-ish wrappers around the PCANBasic.dll functions.
//! The DLL is loaded at runtime via libloading.

#![allow(dead_code)]

use libloading::Library;

// ── PCAN Constants ──────────────────────────────────────────

pub const PCAN_NONEBUS: u16 = 0x00;

// PCAN-USB channel handles
pub const PCAN_USBBUS1: u16 = 0x51;
pub const PCAN_USBBUS2: u16 = 0x52;
pub const PCAN_USBBUS3: u16 = 0x53;
pub const PCAN_USBBUS4: u16 = 0x54;
pub const PCAN_USBBUS5: u16 = 0x55;
pub const PCAN_USBBUS6: u16 = 0x56;
pub const PCAN_USBBUS7: u16 = 0x57;
pub const PCAN_USBBUS8: u16 = 0x58;

// Error codes
pub const PCAN_ERROR_OK: u32 = 0x00000;
pub const PCAN_ERROR_QRCVEMPTY: u32 = 0x00020;

// CAN IDs for our protocol
pub const PLATFORM_RX: u32 = 0x101; // Host → Board (commands)
pub const PLATFORM_TX: u32 = 0x102; // Board → Host (responses)
pub const FW_DATA_RX: u32 = 0x103; // Host → Board (firmware data)

// Board commands
pub const BOARD_START_UPDATE: u32 = 0;
pub const BOARD_CONFIRM: u32 = 1;
pub const BOARD_VERSION: u32 = 2;
pub const BOARD_REBOOT: u32 = 3;

// Firmware response codes
pub const FW_CODE_OFFSET: u32 = 0;
pub const FW_CODE_UPDATE_SUCCESS: u32 = 1;
pub const FW_CODE_VERSION: u32 = 2;
pub const FW_CODE_CONFIRM: u32 = 3;
pub const FW_CODE_FLASH_ERROR: u32 = 4;
pub const FW_CODE_TRANFER_ERROR: u32 = 5;

// Baudrate constants
pub const PCAN_BAUD_1M: u16 = 0x0014;
pub const PCAN_BAUD_800K: u16 = 0x0016;
pub const PCAN_BAUD_500K: u16 = 0x001C;
pub const PCAN_BAUD_250K: u16 = 0x011C;
pub const PCAN_BAUD_125K: u16 = 0x031C;
pub const PCAN_BAUD_100K: u16 = 0x432F;
pub const PCAN_BAUD_95K: u16 = 0xC34E;
pub const PCAN_BAUD_83K: u16 = 0x852B;
pub const PCAN_BAUD_50K: u16 = 0x472F;
pub const PCAN_BAUD_47K: u16 = 0x1414;
pub const PCAN_BAUD_33K: u16 = 0x8B2F;
pub const PCAN_BAUD_20K: u16 = 0x532F;
pub const PCAN_BAUD_10K: u16 = 0x672F;
pub const PCAN_BAUD_5K: u16 = 0x7F7F;

pub const PCAN_MODE_STANDARD: u8 = 0x00;
pub const PCAN_USB: u8 = 0x05;

// ── FFI Types ───────────────────────────────────────────────

/// PCAN message structure (8 bytes data)
#[repr(C)]
#[derive(Clone, Debug)]
pub struct TPCANMsg {
    pub id: u32,            // 11/29-bit message identifier
    pub msgtype: u8,        // Message type (standard/extended/etc)
    pub len: u8,            // Data Length Code (0..8)
    pub data: [u8; 8],      // Data bytes
}

/// PCAN timestamp structure
#[repr(C)]
#[derive(Clone, Debug)]
pub struct TPCANTimestamp {
    pub millis: u32,            // Base-value: milliseconds
    pub millis_overflow: u16,   // Roll-arounds of millis
    pub micros: u16,            // Microseconds: 0..999
}

/// CAN frame used in our upgrade protocol (embedded in msg.data)
#[repr(C)]
#[derive(Clone, Debug)]
pub struct CanFrame {
    pub code: u32,
    pub val: u32,
}

impl TPCANMsg {
    pub fn new_command(id: u32, code: u32, val: u32) -> Self {
        let mut msg = TPCANMsg {
            id,
            msgtype: PCAN_MODE_STANDARD,
            len: 8,
            data: [0u8; 8],
        };
        let frame = CanFrame { code, val };
        let bytes = unsafe {
            std::mem::transmute::<CanFrame, [u8; 8]>(frame)
        };
        msg.data = bytes;
        msg
    }

    pub fn new_data(id: u32, buf: &[u8]) -> Self {
        let mut data = [0u8; 8];
        let len = buf.len().min(8);
        data[..len].copy_from_slice(&buf[..len]);
        TPCANMsg {
            id,
            msgtype: PCAN_MODE_STANDARD,
            len: len as u8,
            data,
        }
    }

    pub fn parse_can_frame(&self) -> Option<(u32, u32)> {
        if self.len >= 8 {
            let bytes: [u8; 8] = self.data;
            let frame: CanFrame = unsafe { std::mem::transmute(bytes) };
            Some((frame.code, frame.val))
        } else {
            None
        }
    }
}

// ── Baudrate helpers ────────────────────────────────────────

pub const BAUD_RATES: &[u16] = &[
    PCAN_BAUD_10K,
    PCAN_BAUD_20K,
    PCAN_BAUD_50K,
    PCAN_BAUD_100K,
    PCAN_BAUD_125K,
    PCAN_BAUD_250K,
    PCAN_BAUD_500K,
    PCAN_BAUD_1M,
];

pub const BAUD_NAMES: &[&str] = &[
    "10 Kbit/s",
    "20 Kbit/s",
    "50 Kbit/s",
    "100 Kbit/s",
    "125 Kbit/s",
    "250 Kbit/s",
    "500 Kbit/s",
    "1000 Kbit/s",
];

// ── Dynamic Library Loader ──────────────────────────────────

/// Wraps a dynamically loaded PCANBasic.dll and provides
/// access to its exported functions.
pub struct PcanDll {
    lib: Library,
    // Function pointers (all use stdcall calling convention on Windows)
    can_initialize:
        unsafe extern "system" fn(u16, u16, u8, u32, u16) -> u32,
    can_uninitialize: unsafe extern "system" fn(u16) -> u32,
    can_write: unsafe extern "system" fn(u16, *const TPCANMsg) -> u32,
    can_read:
        unsafe extern "system" fn(u16, *mut TPCANMsg, *mut TPCANTimestamp) -> u32,
    can_filter_messages:
        unsafe extern "system" fn(u16, u32, u32, u8) -> u32,
    can_lookup_channel:
        unsafe extern "system" fn(*const u8, *mut u16) -> u32,
}

impl PcanDll {
    pub fn load(dll_path: Option<&str>) -> Result<Self, String> {
        let path = dll_path.unwrap_or("PCANBasic.dll");
        let lib = unsafe {
            Library::new(path)
                .map_err(|e| format!("无法加载 PCANBasic.dll ({}): {}", path, e))?
        };

        macro_rules! get_fn {
            ($lib:expr, $name:literal, $t:ty) => {
                *unsafe {
                    $lib.get::<$t>($name.as_bytes())
                        .map_err(|e| format!("找不到函数 {}: {}", $name, e))?
                }
            };
        }

        let can_initialize = get_fn!(lib, "CAN_Initialize", unsafe extern "system" fn(u16, u16, u8, u32, u16) -> u32);
        let can_uninitialize = get_fn!(lib, "CAN_Uninitialize", unsafe extern "system" fn(u16) -> u32);
        let can_write = get_fn!(lib, "CAN_Write", unsafe extern "system" fn(u16, *const TPCANMsg) -> u32);
        let can_read = get_fn!(lib, "CAN_Read", unsafe extern "system" fn(u16, *mut TPCANMsg, *mut TPCANTimestamp) -> u32);
        let can_filter_messages = get_fn!(lib, "CAN_FilterMessages", unsafe extern "system" fn(u16, u32, u32, u8) -> u32);
        let can_lookup_channel = get_fn!(lib, "CAN_LookUpChannel", unsafe extern "system" fn(*const u8, *mut u16) -> u32);

        Ok(PcanDll {
            lib,
            can_initialize,
            can_uninitialize,
            can_write,
            can_read,
            can_filter_messages,
            can_lookup_channel,
        })
    }

    /// Initialize a PCAN channel.
    pub fn initialize(&self, channel: u16, baudrate: u16) -> u32 {
        unsafe {
            (self.can_initialize)(channel, baudrate, PCAN_USB, 0, 0)
        }
    }

    /// Uninitialize a PCAN channel.
    pub fn uninitialize(&self, channel: u16) -> u32 {
        unsafe { (self.can_uninitialize)(channel) }
    }

    /// Write a CAN message.
    pub fn write(&self, channel: u16, msg: &TPCANMsg) -> u32 {
        unsafe { (self.can_write)(channel, msg as *const TPCANMsg) }
    }

    /// Read a CAN message (non-blocking).
    pub fn read(&self, channel: u16, msg: &mut TPCANMsg, ts: &mut TPCANTimestamp) -> u32 {
        unsafe {
            (self.can_read)(channel, msg as *mut TPCANMsg, ts as *mut TPCANTimestamp)
        }
    }

    /// Set message filter.
    pub fn filter_messages(&self, channel: u16, from_id: u32, to_id: u32, mode: u8) -> u32 {
        unsafe { (self.can_filter_messages)(channel, from_id, to_id, mode) }
    }

    /// Look up a PCAN channel by device type and controller number.
    pub fn lookup_channel(&self, device_type: &str, channel_out: &mut u16) -> u32 {
        let c_str = std::ffi::CString::new(device_type).unwrap_or_default();
        unsafe {
            (self.can_lookup_channel)(c_str.as_ptr() as *const u8, channel_out as *mut u16)
        }
    }
}


