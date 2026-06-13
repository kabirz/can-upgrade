// 固件升级工具 (CAN) —— Walk GUI 主程序
package main

import (
	"fmt"
	"time"

	"github.com/lxn/walk"
	d "github.com/lxn/walk/declarative"
	"github.com/lxn/win"
)

// App 持有所有控件引用与运行状态。
type App struct {
	mw         *walk.MainWindow
	channelCB  *walk.ComboBox
	baudCB     *walk.ComboBox
	fileLE     *walk.LineEdit
	testCB     *walk.CheckBox
	progress   *walk.ProgressBar
	percentL   *walk.Label
	versionL   *walk.Label
	connectBtn *walk.PushButton
	flashBtn   *walk.PushButton
	refreshBtn *walk.PushButton
	gvBtn      *walk.PushButton
	rebootBtn  *walk.PushButton
	logTE      *walk.TextEdit

	mgr       *Manager
	channels  []uint16
	connected bool
	updating  bool
}

func main() {
	app := &App{}
	app.mgr = newManager(app.appendLog)

	mw := d.MainWindow{
		AssignTo: &app.mw,
		Title:    "固件升级工具 (CAN)",
		Size:     d.Size{Width: 660, Height: 540},
		MinSize:  d.Size{Width: 660, Height: 540},
		Layout:   d.VBox{MarginsZero: true, Spacing: 6},
		Children: []d.Widget{
			// 连接设置：设备 / 波特率 / 刷新 / 连接
			d.GroupBox{Title: "连接设置", Layout: d.HBox{},
				Children: []d.Widget{
					d.Label{Text: "设备:"},
					d.ComboBox{AssignTo: &app.channelCB, Editable: false, MinSize: d.Size{Width: 120, Height: 0}},
					d.Label{Text: "波特率:"},
					d.ComboBox{AssignTo: &app.baudCB, Editable: false, Model: baudNames(), MinSize: d.Size{Width: 80, Height: 0}},
					d.PushButton{AssignTo: &app.refreshBtn, Text: "刷新", OnClicked: app.refresh},
					d.PushButton{AssignTo: &app.connectBtn, Text: "连接", OnClicked: app.toggleConnect},
				}},
			// 固件升级：文件 / 测试模式 / 进度
			d.GroupBox{Title: "固件升级", Layout: d.VBox{Margins: d.Margins{Left: 10, Top: 6, Right: 10, Bottom: 8}, Spacing: 6},
				Children: []d.Widget{
					d.Composite{Layout: d.HBox{MarginsZero: true, Spacing: 8}, Children: []d.Widget{
						d.Label{Text: "固件文件:"},
						d.LineEdit{AssignTo: &app.fileLE, ReadOnly: true, StretchFactor: 1},
						d.PushButton{Text: "浏览...", OnClicked: app.browse},
					}},
					d.Composite{Layout: d.HBox{MarginsZero: true}, Children: []d.Widget{
						d.CheckBox{AssignTo: &app.testCB, Text: "测试模式(第二次重启后恢复原固件)"},
						d.HSpacer{}, // 弹性填充，使复选框左对齐
					}},
					d.Composite{Layout: d.HBox{MarginsZero: true, Spacing: 8}, Children: []d.Widget{
						d.Label{Text: "进度:"},
						d.ProgressBar{AssignTo: &app.progress, StretchFactor: 1},
						d.Label{AssignTo: &app.percentL, Text: "0%", MinSize: d.Size{Width: 34, Height: 0}},
						d.PushButton{AssignTo: &app.flashBtn, Text: "开始升级", OnClicked: app.flash},
					}},
				}},
			// 日志：文本框 + 版本/获取版本/重启/清空
			d.GroupBox{Title: "日志", Layout: d.HBox{}, StretchFactor: 1,
				Children: []d.Widget{
					d.TextEdit{AssignTo: &app.logTE, ReadOnly: true, VScroll: true, StretchFactor: 1},
					d.Composite{Layout: d.VBox{MarginsZero: true}, Children: []d.Widget{
						d.Label{AssignTo: &app.versionL, Text: "固件版本: 未获取", MinSize: d.Size{Width: 92, Height: 0}},
						d.PushButton{AssignTo: &app.gvBtn, Text: "获取版本", OnClicked: app.getVersion},
						d.PushButton{AssignTo: &app.rebootBtn, Text: "重启板卡", OnClicked: app.reboot},
						d.VSpacer{}, // 弹性填充，清空日志靠底部、其余靠顶部
						d.PushButton{Text: "清空日志", OnClicked: app.clearLog},
					}},
				}},
		},
	}

	if err := mw.Create(); err != nil {
		panic(err)
	}
	app.startUp()
	app.mw.Run()
}

// startUp 窗口创建后、进入消息循环前的初始化。
func (app *App) startUp() {
	// 窗口居中到屏幕（全程 native 像素，避免 DPI 单位不一致导致偏移）
	var r win.RECT
	win.GetWindowRect(app.mw.Handle(), &r)
	w := int(r.Right - r.Left)
	h := int(r.Bottom - r.Top)
	sw := int(win.GetSystemMetrics(win.SM_CXSCREEN))
	sh := int(win.GetSystemMetrics(win.SM_CYSCREEN))
	win.SetWindowPos(app.mw.Handle(), 0, int32((sw-w)/2), int32((sh-h)/2),
		0, 0, win.SWP_NOSIZE|win.SWP_NOZORDER)
	if id := firstGroupIconID(); id != 0 {
		if icon, err := walk.NewIconFromResourceId(id); err == nil {
			app.mw.SetIcon(icon)
		}
	}
	app.logTE.SetMaxLength(0)     // 取消默认字符上限，避免日志超限触发提示音
	app.baudCB.SetCurrentIndex(5) // 默认 250K
	app.flashBtn.SetEnabled(false)
	app.gvBtn.SetEnabled(false)
	app.rebootBtn.SetEnabled(false)
	app.refresh()
}

// appendLog 线程安全地追加一行带时间戳的日志（升级 goroutine 也会调用）。
func (app *App) appendLog(msg string) {
	line := fmt.Sprintf("[%s] %s\r\n", time.Now().Format("15:04:05"), msg)
	app.mw.Synchronize(func() {
		app.logTE.AppendText(line)
		n := app.logTE.TextLength()
		app.logTE.SetTextSelection(n, n) // 光标移到末尾（AppendText 会恢复旧选区）
		app.logTE.ScrollToCaret()        // 滚动跟随光标到底部
	})
}

// refresh 探测 PCAN-USB 设备并填充下拉框。
func (app *App) refresh() {
	chs, ok := app.mgr.Detect()
	if !ok {
		app.appendLog("缺少 PCANBasic.dll，可能未安装 PCAN 驱动，请安装驱动后重试")
		app.updateConnectBtn()
		return
	}
	app.channels = chs
	names := make([]string, len(chs))
	for i, c := range chs {
		names[i] = fmt.Sprintf("PCAN-USB: %xh", c)
	}
	app.channelCB.SetModel(names)
	if len(names) > 0 {
		app.channelCB.SetCurrentIndex(0)
	}
	app.updateConnectBtn()
}

// browse 选择固件文件。
func (app *App) browse() {
	dlg := new(walk.FileDialog)
	dlg.Title = "选择固件文件"
	dlg.Filter = "固件文件 (*.bin)|*.bin|所有文件 (*.*)|*.*"
	if ok, _ := dlg.ShowOpen(app.mw); ok {
		app.fileLE.SetText(dlg.FilePath)
		app.updateFlashBtn()
	}
}

// toggleConnect 连接 / 断开切换。
func (app *App) toggleConnect() {
	if app.connected {
		app.mgr.Disconnect()
		app.connected = false
		app.connectBtn.SetText("连接")
		app.setConnectedUI(false)
		return
	}
	idx := app.channelCB.CurrentIndex()
	baud := app.baudCB.CurrentIndex()
	if idx < 0 || idx >= len(app.channels) || baud < 0 {
		return
	}
	if app.mgr.Connect(app.channels[idx], baudRates[baud].code) {
		app.connected = true
		app.connectBtn.SetText("断开")
		app.setConnectedUI(true)
	} else {
		walk.MsgBox(app.mw, "提示", "连接失败，请检查设备", walk.MsgBoxIconWarning)
	}
}

func (app *App) setConnectedUI(conn bool) {
	app.channelCB.SetEnabled(!conn)
	app.baudCB.SetEnabled(!conn)
	app.refreshBtn.SetEnabled(!conn)
	app.gvBtn.SetEnabled(conn)
	app.rebootBtn.SetEnabled(conn)
	app.versionL.SetText("固件版本: 未获取")
	app.updateFlashBtn()
}

func (app *App) updateConnectBtn() {
	app.connectBtn.SetEnabled(len(app.channels) > 0 &&
		app.channelCB.CurrentIndex() >= 0 && !app.updating && !app.connected)
}

func (app *App) updateFlashBtn() {
	app.flashBtn.SetEnabled(app.connected && app.fileLE.Text() != "" && !app.updating)
}

// getVersion 异步查询版本，避免阻塞 UI。
func (app *App) getVersion() {
	app.gvBtn.SetEnabled(false)
	go func() {
		ver := app.mgr.Version()
		app.mw.Synchronize(func() {
			app.gvBtn.SetEnabled(true)
			if ver != 0 {
				app.versionL.SetText(fmt.Sprintf("固件版本: v%d.%d.%d",
					ver>>24, ver>>16&0xFF, ver>>8&0xFF))
			}
		})
	}()
}

// reboot 确认后重启板卡。
func (app *App) reboot() {
	if walk.MsgBox(app.mw, "确认", "确认重启板卡？", walk.MsgBoxOKCancel|walk.MsgBoxIconInformation) == walk.DlgCmdOK {
		if app.mgr.Reboot() {
			app.appendLog("等待重启完成")
		}
	}
}

func (app *App) clearLog() {
	app.logTE.SetText("")
}

// flash 后台执行固件升级，进度与结果通过 Synchronize 回 UI。
func (app *App) flash() {
	if app.updating || !app.connected {
		return
	}
	fn := app.fileLE.Text()
	if fn == "" {
		return
	}
	app.updating = true
	// 升级期间禁用所有会抢占 CAN 锁的交互，避免 UI 线程阻塞卡死
	app.flashBtn.SetEnabled(false)
	app.refreshBtn.SetEnabled(false)
	app.connectBtn.SetEnabled(false)
	app.gvBtn.SetEnabled(false)
	app.rebootBtn.SetEnabled(false)
	app.progress.SetValue(0)
	app.percentL.SetText("0%")

	test := app.testCB.Checked()
	onProg := func(p int) {
		app.mw.Synchronize(func() {
			app.progress.SetValue(p)
			app.percentL.SetText(fmt.Sprintf("%d%%", p))
		})
	}
	go func() {
		ok := app.mgr.Upgrade(fn, test, onProg)
		app.mw.Synchronize(func() {
			app.updating = false
			app.setConnectedUI(true) // 恢复连接态按钮
			app.connectBtn.SetEnabled(true)
			if ok {
				walk.MsgBox(app.mw, "成功", "固件升级完成！请重启板卡", walk.MsgBoxIconInformation)
			} else {
				walk.MsgBox(app.mw, "失败", "固件升级失败，请查看日志", walk.MsgBoxIconError)
			}
		})
	}()
}
