# CAN 固件升级工具

基于 WebView2 的 CAN 总线固件升级工具，通过 PCAN 硬件与目标板卡通信，支持固件烧录、版本查询、板卡重启等操作。

## 功能

- 自动检测 PCAN 设备通道
- 支持多种波特率（10K ~ 1M）
- 固件升级（.bin 文件烧录）与进度显示
- 查询板卡固件版本
- 远程重启板卡
- 测试模式（仅校验不烧录）

## 环境要求

- Windows 10 / 11
- [Visual Studio 2022](https://visualstudio.microsoft.com/)（含 C++ 桌面开发工作负载）
- [CMake](https://cmake.org/) ≥ 3.25
- WebView2 Runtime（Win11 已预装，Win10 需单独安装）
- PCAN 硬件及驱动

## 构建

```bash
# 配置（首次会自动下载 WebView2 SDK）
cmake --preset vs

# 编译 Release
cmake --build out --config Release
```

或使用 workflow preset 一键完成：

```bash
cmake --workflow --preset vs-release
```

输出文件：`out/Release/can-webview2.exe`（约 103KB，单文件部署）

## 项目结构

```
├── CMakeLists.txt          # CMake 构建配置
├── CMakePresets.json       # CMake 预设（MSVC）
├── compile_flags.txt       # clangd 诊断配置
├── src/
│   ├── main.cpp            # WebView2 宿主、窗口、JS↔C++ 桥接
│   └── CanManager.cpp      # CAN 管理器实现
├── include/
│   ├── CanManager.h        # CanManager 类声明
│   ├── PCANBasic.h         # PCAN SDK 类型定义
│   └── resource.h          # 资源 ID 定义
├── resources/
│   ├── index.html          # Web UI（HTML/CSS/JS）
│   └── resource.rc         # Windows 资源脚本
└── docs/
    └── architecture.html   # 技术架构文档
```

## 技术栈

- **前端**：HTML + CSS + JavaScript（嵌入 exe，通过 WebView2 加载）
- **后端**：C++17 / Win32 API / WebView2 SDK
- **通信**：PCAN SDK（动态加载 DLL）
- **构建**：CMake + MSVC，静态链接 WebView2LoaderStatic.lib

## 工作原理

1. 启动时创建 Win32 窗口，内嵌 WebView2 控件加载 HTML 界面
2. 前端通过 `window.chrome.webview.postMessage()` 发送操作指令
3. C++ 端解析 JSON 消息，调用 CanManager 执行对应操作
4. 工作线程通过 `PostMessage()` 回调 UI 线程，UI 线程通过 `PostWebMessageAsJson()` 更新前端状态

## 许可

私有项目，未公开授权。
