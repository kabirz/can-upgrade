# CAN 固件升级工具 - Go (Walk)

使用 Go 和 [Walk](https://github.com/lxn/walk)（Windows 原生 GUI 工具包）实现的 CAN 固件升级工具，功能等价于 `can-win32c`。

## 功能

- PCAN-USB 设备自动探测与连接（多通道、可选波特率）
- 固件升级（支持测试模式：第二次重启后恢复原固件）
- 固件版本查询
- 远程重启板卡
- 实时日志与进度显示（自动滚动、无字符上限）

## 特点

- **无第三方 PCAN 绑定**：通过 `syscall` 懒加载 `PCANBasic.dll`，等价于 C 版的动态加载，DLL 缺失时优雅降级提示安装驱动。
- **Windows 原生控件**：Walk 封装 Win32 API，单文件可执行，无运行时依赖。
- **线程安全**：固件升级运行在 goroutine，通过 `Synchronize` 回 UI 线程更新进度与日志。

## CAN 协议

与其它实现一致：

| CAN ID | 名称 | 方向 |
|--------|------|------|
| 0x101 | PLATFORM_RX | PC → 板卡 |
| 0x102 | PLATFORM_TX | 板卡 → PC |
| 0x103 | FW_DATA_RX | 固件数据帧 |

帧结构为 8 字节 `{code, val}`。命令码：`0` 开始升级 / `1` 确认 / `2` 查询版本 / `3` 重启。

## 依赖

- Go 1.26+
- Windows + [PCAN-Basic](https://www.peak-system.com/PCAN-Basic.535.0.html) 驱动（运行时需 `PCANBasic.dll`）

## 构建

`rsrc.syso` 已随仓库提交（内含 manifest 与图标），`go build` 会自动链接，**无需安装 rsrc 即可编译**。产物名 `can-walk.exe`（取自目录名）。

### 推荐：PowerShell 脚本（GUI 版，无控制台）

```powershell
./build.ps1          # 构建 → can-walk.exe（无黑框）
./build.ps1 run      # 构建并运行
./build.ps1 clean    # 清理构建产物
./build.ps1 syso     # 改动 manifest.xml/icon.ico 后重新生成 rsrc.syso
```

### 直接用 go 命令

```powershell
go build                                # 产物 can-walk.exe，但默认 console 子系统（带黑框）
go build -ldflags="-H windowsgui -s -w"  # GUI 版（无黑框），推荐
go clean                                 # 清理
```

> `-H windowsgui`（去黑框）无法写入 `go.mod`，这是 Go 的限制，故通过 `-ldflags` 或 `./build.ps1` 传入。

## 文件结构

| 文件 | 说明 | 对应 C 版 |
|------|------|-----------|
| `pcan.go` | PCANBasic.dll 动态绑定（syscall 懒加载） | `pcan_loader.c` |
| `can.go` | CAN 协议与管理器（连接/版本/重启/升级/探测） | `can_manager.c` |
| `main.go` | Walk GUI 主程序（界面、交互、升级 goroutine） | `main.c` |
| `manifest.xml` | ComCtl32 v6 视觉样式清单 | `resource.rc` |
| `icon.ico` | 应用图标（矢量生成，6 尺寸） | `resources/icon.ico` |
| `rsrc.syso` | 编译进 exe 的资源（manifest + 图标） | — |
| `build.ps1` | 构建脚本（封装 `go build` / `go clean`） | — |
| `genicon/` | 图标生成器（矢量绘制多尺寸 ICO，独立 module） | — |

改图标：`cd genicon && go run . ../icon.ico`，再 `./build.ps1 syso && ./build.ps1 build`。
