// CAN 管理器 —— 等价于 can-win32c 的 can_manager.c。
// 封装设备探测、连接、版本查询、重启、固件升级。
package main

import (
	"encoding/binary"
	"fmt"
	"os"
	"sync"
	"time"
)

// CAN 协议：ID 与命令码
const (
	platformRx uint32 = 0x101 // PC → 板卡
	platformTx uint32 = 0x102 // 板卡 → PC
	fwDataRx   uint32 = 0x103 // 固件数据帧

	cmdStartUpdate uint32 = 0
	cmdConfirm     uint32 = 1
	cmdVersion     uint32 = 2
	cmdReboot      uint32 = 3
)

// 板卡应答 code
const (
	codeOffset        uint32 = 0
	codeUpdateSuccess uint32 = 1
	codeVersion       uint32 = 2
	codeConfirm       uint32 = 3
	codeTransferError uint32 = 5
)

// 升级确认应答值
const confirmMagic uint32 = 0x55AA55AA

// Manager 线程安全地管理一条 PCAN 通道。
type Manager struct {
	mu   sync.Mutex
	ch   uint16
	logf func(string) // 日志回调（由调用方转发到 UI 线程）
}

func newManager(logf func(string)) *Manager {
	return &Manager{ch: pcanNoneBus, logf: logf}
}

func (m *Manager) log(s string) {
	if m.logf != nil {
		m.logf(s)
	}
}

// requireConnected 已连接返回 true，否则记录日志并返回 false。
func (m *Manager) requireConnected() bool {
	if m.ch == pcanNoneBus {
		m.log("CAN 未连接")
		return false
	}
	return true
}

// setCmd 填充一条 8 字节命令帧 {code, val}。
func (m *pcanMsg) setCmd(id, code, val uint32) {
	m.ID = id
	m.Type = pcanModeStd
	m.Len = 8
	binary.LittleEndian.PutUint32(m.Data[0:4], code)
	binary.LittleEndian.PutUint32(m.Data[4:8], val)
}

// Detect 探测最多 16 个 PCAN-USB 通道。driverOk=false 表示驱动缺失。
func (m *Manager) Detect() (channels []uint16, driverOk bool) {
	if !pcanAvailable() {
		return nil, false
	}
	for i := 0; i < 16; i++ {
		var ch uint16 = pcanNoneBus
		param := fmt.Sprintf("devicetype=pcan_usb,controllernumber=%d", i)
		if pcanLookUpChannel(param, &ch) == pcanOK && ch != pcanNoneBus {
			channels = append(channels, ch)
		}
	}
	m.log(fmt.Sprintf("查询到 %d 个可用 CAN 设备", len(channels)))
	return channels, true
}

// Connect 初始化通道并只接收 PLATFORM_TX。
func (m *Manager) Connect(ch, baud uint16) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.ch != pcanNoneBus {
		m.log("CAN 已连接，请勿重复连接")
		return true
	}
	if pcanInitialize(ch, baud) != pcanOK {
		m.log("CAN 初始化失败")
		return false
	}
	m.ch = ch
	pcanFilterMessages(ch, platformTx, platformTx, pcanModeStd)
	m.log(fmt.Sprintf("CAN(id=%xh) 连接成功", ch))
	return true
}

func (m *Manager) Disconnect() {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.ch == pcanNoneBus {
		return
	}
	m.log(fmt.Sprintf("CAN(id=%xh) 连接已断开", m.ch))
	pcanUninitialize(m.ch)
	m.ch = pcanNoneBus
}

// wait 轮询等待 PLATFORM_TX 应答，解析 {code, val}。
func (m *Manager) wait(timeout time.Duration) (code, val uint32, ok bool) {
	deadline := time.Now().Add(timeout)
	var msg pcanMsg
	var ts pcanTimestamp
	for time.Now().Before(deadline) {
		if pcanRead(m.ch, &msg, &ts) == pcanOK && msg.ID == platformTx {
			return binary.LittleEndian.Uint32(msg.Data[0:4]),
				binary.LittleEndian.Uint32(msg.Data[4:8]), true
		}
		time.Sleep(time.Millisecond)
	}
	return 0, 0, false
}

// Version 查询固件版本（阻塞，最多 5s）。
func (m *Manager) Version() uint32 {
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.requireConnected() {
		return 0
	}
	var msg pcanMsg
	msg.setCmd(platformRx, cmdVersion, 0)
	if pcanWrite(m.ch, &msg) != pcanOK {
		m.log("CAN 发送失败")
		return 0
	}
	code, ver, ok := m.wait(5 * time.Second)
	if !ok {
		m.log("CAN 读取超时")
		return 0
	}
	if code == codeVersion {
		m.log(fmt.Sprintf("固件版本: v%d.%d.%d", ver>>24, ver>>16&0xFF, ver>>8&0xFF))
		return ver
	}
	m.log("CAN 读取数据错误")
	return 0
}

// Reboot 发送重启命令（不等待应答）。
func (m *Manager) Reboot() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.requireConnected() {
		return false
	}
	var msg pcanMsg
	msg.setCmd(platformRx, cmdReboot, 0)
	if pcanWrite(m.ch, &msg) != pcanOK {
		m.log("CAN 发送失败")
		return false
	}
	return true
}

// Upgrade 执行完整固件升级流程：擦除 → 分块发送 → 确认。
// onProg 为进度回调（百分比 0-100），可为 nil。
func (m *Manager) Upgrade(filename string, testMode bool, onProg func(int)) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if !m.requireConnected() {
		return false
	}
	prog := func(p int) {
		if onProg != nil {
			onProg(p)
		}
	}

	data, err := os.ReadFile(filename)
	if err != nil {
		m.log("无法打开文件: " + filename)
		return false
	}
	size := uint32(len(data))
	m.log(fmt.Sprintf("固件大小: %d 字节", size))

	var msg pcanMsg
	msg.setCmd(platformRx, cmdStartUpdate, size) // 通知文件大小
	if pcanWrite(m.ch, &msg) != pcanOK {
		m.log("发送固件大小失败")
		return false
	}
	code, off, ok := m.wait(15 * time.Second) // 等待 Flash 擦除
	if !ok {
		m.log("Flash 擦除超时")
		return false
	}
	if code != codeOffset || off != 0 {
		m.log(fmt.Sprintf("Flash 擦除失败: code(%d), offset(%d)", code, off))
		return false
	}

	// 分块发送固件，每 64 字节等一次 ACK
	var sent uint32
	for sent < size {
		n := uint32(8)
		if size-sent < 8 {
			n = size - sent
		}
		msg.ID = fwDataRx
		msg.Type = pcanModeStd
		msg.Len = byte(n)
		copy(msg.Data[:], data[sent:sent+n])
		for i := n; i < 8; i++ { // 清零未用字节
			msg.Data[i] = 0
		}
		if pcanWrite(m.ch, &msg) != pcanOK {
			m.log("发送数据失败")
			return false
		}
		sent += n
		if sent%64 == 0 || sent == size {
			prog(int(sent * 100 / size))
			code, off, ok = m.wait(5 * time.Second)
			if !ok {
				m.log("固件更新超时")
				return false
			}
			if code == codeUpdateSuccess && off == sent {
				break // 板卡完成写入
			}
			if code != codeOffset {
				m.log(fmt.Sprintf("固件升级失败: code(%d), offset(%d)", code, off))
				return false
			}
		}
	}
	prog(100)

	// 发送确认：正式升级=1，测试模式=0
	confirmVal := uint32(1)
	if testMode {
		confirmVal = 0
	}
	msg.setCmd(platformRx, cmdConfirm, confirmVal)
	if pcanWrite(m.ch, &msg) != pcanOK {
		m.log("发送确认失败")
		return false
	}
	code, off, ok = m.wait(30 * time.Second)
	if !ok {
		m.log("固件确认超时")
		return false
	}
	if code == codeConfirm && off == confirmMagic {
		m.log(fmt.Sprintf("文件 %s 上传完成，请重启板卡", filename))
		return true
	}
	if code == codeTransferError {
		m.log("固件更新失败")
	}
	return false
}
