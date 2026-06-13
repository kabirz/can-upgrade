// PCANBasic.dll 动态绑定 —— 等价于 can-win32c 的 pcan_loader.c。
// 用 syscall 懒加载，DLL 缺失时优雅降级，无需第三方绑定。
package main

import (
	"syscall"
	"unsafe"
)

// PCANBasic.dll 函数（stdcall；LazyDLL 在首次调用时才加载）
var pcan = syscall.NewLazyDLL("PCANBasic.dll")

var (
	procInitialize     = pcan.NewProc("CAN_Initialize")
	procUninitialize   = pcan.NewProc("CAN_Uninitialize")
	procRead           = pcan.NewProc("CAN_Read")
	procWrite          = pcan.NewProc("CAN_Write")
	procFilterMessages = pcan.NewProc("CAN_FilterMessages")
	procLookUpChannel  = pcan.NewProc("CAN_LookUpChannel")
	procGetErrorText   = pcan.NewProc("CAN_GetErrorText")
)

// PCAN 状态码 / 句柄 / 模式
const (
	pcanOK      uint32 = 0x00000
	pcanQEmpty  uint32 = 0x00020
	pcanNoneBus uint16 = 0x00
	pcanModeStd uint8  = 0x00 // PCAN_MESSAGE_STANDARD
)

// 波特率码（BTR0BTR1 寄存器值），索引即下拉项顺序
var baudRates = []struct {
	name string
	code uint16
}{
	{"10K", 0x672F}, {"20K", 0x532F}, {"50K", 0x472F}, {"100K", 0x432F},
	{"125K", 0x031C}, {"250K", 0x011C}, {"500K", 0x001C}, {"1M", 0x0014},
}

func baudNames() []string {
	n := make([]string, len(baudRates))
	for i, b := range baudRates {
		n[i] = b.name
	}
	return n
}

// TPCANMsg：ID(4) + Type(1) + Len(1) + Data[8] + 填充(2) = 16 字节，与 C 布局一致。
type pcanMsg struct {
	ID   uint32
	Type uint8
	Len  uint8
	Data [8]uint8
}

// TPCANTimestamp：millis(4) + overflow(2) + micros(2) = 8 字节
type pcanTimestamp struct {
	millis   uint32
	overflow uint16
	micros   uint16
}

// pcanAvailable 检测驱动是否完整加载（任一导出函数缺失即视为未安装）。
func pcanAvailable() bool {
	for _, p := range []*syscall.LazyProc{
		procInitialize, procUninitialize, procRead, procWrite,
		procFilterMessages, procLookUpChannel, procGetErrorText,
	} {
		if p.Find() != nil {
			return false
		}
	}
	return true
}

func pcanInitialize(ch, baud uint16) uint32 {
	r, _, _ := procInitialize.Call(uintptr(ch), uintptr(baud), 0, 0, 0)
	return uint32(r)
}

func pcanUninitialize(ch uint16) {
	procUninitialize.Call(uintptr(ch))
}

func pcanRead(ch uint16, msg *pcanMsg, ts *pcanTimestamp) uint32 {
	r, _, _ := procRead.Call(uintptr(ch),
		uintptr(unsafe.Pointer(msg)), uintptr(unsafe.Pointer(ts)))
	return uint32(r)
}

func pcanWrite(ch uint16, msg *pcanMsg) uint32 {
	r, _, _ := procWrite.Call(uintptr(ch), uintptr(unsafe.Pointer(msg)))
	return uint32(r)
}

func pcanFilterMessages(ch uint16, from, to uint32, mode uint8) uint32 {
	r, _, _ := procFilterMessages.Call(uintptr(ch),
		uintptr(from), uintptr(to), uintptr(mode))
	return uint32(r)
}

func pcanLookUpChannel(param string, ch *uint16) uint32 {
	p, _ := syscall.BytePtrFromString(param)
	r, _, _ := procLookUpChannel.Call(uintptr(unsafe.Pointer(p)), uintptr(unsafe.Pointer(ch)))
	return uint32(r)
}

// firstGroupIconID 枚举本进程资源，返回首个 RT_GROUP_ICON(14) 的资源 ID。
// rsrc 把 manifest 与图标一起嵌入时图标组 ID 不固定（manifest 常占 ID 1），故运行时查找。
func firstGroupIconID() int {
	k32 := syscall.NewLazyDLL("kernel32.dll")
	enum := k32.NewProc("EnumResourceNamesW")
	hInst, _, _ := k32.NewProc("GetModuleHandleW").Call(0)
	var id int
	cb := syscall.NewCallback(func(_, _, name, _ uintptr) uintptr {
		if name < 0x10000 && id == 0 { // IS_INTRESOURCE：取首个数字 ID
			id = int(name)
		}
		return 1
	})
	enum.Call(hInst, 14 /*RT_GROUP_ICON*/, cb, 0)
	return id
}
