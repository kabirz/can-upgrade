//! PCAN-Basic FFI types and dynamic library loader
//!
//! Provides safe-ish wrappers around the PCANBasic.dll functions.
//! The DLL is loaded at runtime via libloading.

#![allow(dead_code)]

use libloading::Library;

// ── PCAN Constants ──────────────────────────────────────────

pub const PCAN_NONEBUS: u16 = 0x00;

// Error codes
pub const PCAN_ERROR_OK: u32 = 0x00000;

// Baudrate constants (used in BAUD_RATES)
pub const PCAN_BAUD_1M: u16 = 0x0014;
pub const PCAN_BAUD_500K: u16 = 0x001C;
pub const PCAN_BAUD_250K: u16 = 0x011C;
pub const PCAN_BAUD_125K: u16 = 0x031C;
pub const PCAN_BAUD_100K: u16 = 0x432F;
pub const PCAN_BAUD_50K: u16 = 0x472F;
pub const PCAN_BAUD_20K: u16 = 0x532F;
pub const PCAN_BAUD_10K: u16 = 0x672F;

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

impl TPCANMsg {
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

pub const BAUD_RATE_NAMES: &[&str] = &[
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


