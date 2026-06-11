//! SocketCAN backend for Linux
//!
//! Raw CAN communication via Linux SocketCAN using libc syscalls.

use std::ffi::CString;
use std::os::unix::io::RawFd;

// ── Constants ──────────────────────────────────────────────────

const AF_CAN: u16 = 29;
const PF_CAN: libc::c_int = 29;
const SOCK_RAW: libc::c_int = 3;
const CAN_RAW: libc::c_int = 1;
const SOL_CAN_RAW: libc::c_int = 101;
const CAN_RAW_FILTER: libc::c_int = 1;
const CAN_SFF_MASK: u32 = 0x7FF;

// ARPHRD_CAN from linux/if_arp.h
const ARPHRD_CAN: u32 = 280;

// ── FFI structures ─────────────────────────────────────────────

/// Matches kernel struct sockaddr_can: { u16 can_family; int can_ifindex; union { ... } can_addr; }
/// Total size = 16 bytes (2 + 2pad + 4 + 8)
#[repr(C)]
struct SockAddrCan {
    can_family: u16,
    _pad: u16,
    can_ifindex: libc::c_int,
    can_addr: [u8; 8],
}

/// Matches kernel struct can_frame (16 bytes)
#[repr(C)]
struct CanFrame {
    can_id: u32,
    can_dlc: u8,
    __pad: u8,
    __res0: u8,
    __res1: u8,
    data: [u8; 8],
}

/// Matches kernel struct can_filter (8 bytes)
#[repr(C)]
struct CanFilter {
    can_id: u32,
    can_mask: u32,
}

// Static layout assertions
const _: () = assert!(std::mem::size_of::<CanFrame>() == 16);
const _: () = assert!(std::mem::size_of::<CanFilter>() == 8);
const _: () = assert!(std::mem::size_of::<SockAddrCan>() == 16);

// ── SocketCanBus ───────────────────────────────────────────────

pub struct SocketCanBus {
    fd: RawFd,
}

impl Drop for SocketCanBus {
    fn drop(&mut self) {
        if self.fd >= 0 {
            unsafe {
                libc::close(self.fd);
            }
        }
    }
}

impl SocketCanBus {
    /// List available CAN interfaces by scanning /sys/class/net/
    pub fn list_interfaces() -> Vec<String> {
        let mut ifaces = Vec::new();
        let Ok(entries) = std::fs::read_dir("/sys/class/net/") else {
            return ifaces;
        };
        for entry in entries.flatten() {
            let name = entry.file_name();
            let name_str = name.to_string_lossy();
            let type_path = format!("/sys/class/net/{}/type", name_str);
            if let Ok(type_str) = std::fs::read_to_string(&type_path) {
                if type_str.trim().parse::<u32>() == Ok(ARPHRD_CAN) {
                    ifaces.push(name_str.into_owned());
                    continue;
                }
            }
            if name_str.starts_with("can") || name_str.starts_with("vcan") {
                ifaces.push(name_str.into_owned());
            }
        }
        ifaces.sort();
        ifaces
    }

    /// Open a SocketCAN interface by name (e.g. "can0")
    pub fn open(ifname: &str) -> Result<Self, String> {
        let fd = unsafe { libc::socket(PF_CAN, SOCK_RAW, CAN_RAW) };
        if fd < 0 {
            return Err(format!("无法创建 CAN socket: {}", std::io::Error::last_os_error()));
        }

        let c_ifname = CString::new(ifname).map_err(|_| "无效的接口名")?;
        let ifindex = unsafe { libc::if_nametoindex(c_ifname.as_ptr()) };
        if ifindex == 0 {
            unsafe { libc::close(fd) };
            return Err(format!("接口 {} 不存在", ifname));
        }

        let addr = SockAddrCan {
            can_family: AF_CAN,
            _pad: 0,
            can_ifindex: ifindex as libc::c_int,
            can_addr: [0; 8],
        };
        let ret = unsafe {
            libc::bind(
                fd,
                &addr as *const SockAddrCan as *const libc::sockaddr,
                std::mem::size_of::<SockAddrCan>() as libc::socklen_t,
            )
        };
        if ret < 0 {
            let err = std::io::Error::last_os_error();
            unsafe { libc::close(fd) };
            return Err(format!("绑定 CAN 接口 {} 失败: {}", ifname, err));
        }

        Ok(SocketCanBus { fd })
    }

    /// Set CAN ID filter (accept frames with IDs in [from_id, to_id])
    pub fn set_filter(&self, from_id: u32, to_id: u32) -> Result<(), String> {
        let filter = if from_id == to_id {
            CanFilter {
                can_id: from_id & CAN_SFF_MASK,
                can_mask: CAN_SFF_MASK,
            }
        } else {
            let xor = from_id ^ to_id;
            let mask = !(xor | (xor - 1)) & CAN_SFF_MASK;
            CanFilter {
                can_id: from_id & mask,
                can_mask: mask,
            }
        };

        let ret = unsafe {
            libc::setsockopt(
                self.fd,
                SOL_CAN_RAW,
                CAN_RAW_FILTER,
                &filter as *const CanFilter as *const libc::c_void,
                std::mem::size_of::<CanFilter>() as libc::socklen_t,
            )
        };
        if ret < 0 {
            return Err(format!(
                "设置 CAN 过滤器失败: {}",
                std::io::Error::last_os_error()
            ));
        }
        Ok(())
    }

    /// Write a CAN frame
    pub fn write_frame(&self, id: u32, data: &[u8]) -> Result<(), String> {
        let mut frame = CanFrame {
            can_id: id & CAN_SFF_MASK,
            can_dlc: data.len().min(8) as u8,
            __pad: 0,
            __res0: 0,
            __res1: 0,
            data: [0u8; 8],
        };
        let len = data.len().min(8);
        frame.data[..len].copy_from_slice(&data[..len]);

        let written = unsafe {
            libc::write(
                self.fd,
                &frame as *const CanFrame as *const libc::c_void,
                std::mem::size_of::<CanFrame>(),
            )
        };
        if written < 0 {
            return Err(format!(
                "CAN 发送失败: {}",
                std::io::Error::last_os_error()
            ));
        }
        Ok(())
    }

    /// Read a CAN frame with timeout (returns None on timeout)
    pub fn read_frame(&self, timeout_ms: u64) -> Option<(u32, [u8; 8], u8)> {
        let mut pfd = libc::pollfd {
            fd: self.fd,
            events: libc::POLLIN,
            revents: 0,
        };

        let ret = unsafe { libc::poll(&mut pfd, 1, timeout_ms as libc::c_int) };
        if ret <= 0 {
            return None;
        }

        let mut frame: CanFrame = unsafe { std::mem::zeroed() };
        let n = unsafe {
            libc::read(
                self.fd,
                &mut frame as *mut CanFrame as *mut libc::c_void,
                std::mem::size_of::<CanFrame>(),
            )
        };
        if n <= 0 {
            return None;
        }

        // Mask out EFF/RTR/ERR flag bits, return pure CAN ID
        let clean_id = frame.can_id & CAN_SFF_MASK;
        Some((clean_id, frame.data, frame.can_dlc))
    }
}
