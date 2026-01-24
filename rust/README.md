# CAN 固件升级工具 - Rust

使用 Rust 和 Slint GUI 框架实现的现代化 CAN 固件升级工具。

## 功能

- 固件升级
- 版本查询
- 设备重启
- 跨平台 GUI 界面

## 依赖

- `slint` = "1.8.0" - GUI 框架
- `rfd` = "0.15.4" - 文件对话框
- `peak-can-sys` = "0.1.2" - PCAN 绑定 (Windows)

## 构建

```bash
cargo build --release
```

## 运行

```bash
cargo run
```

或直接运行编译产物：

```bash
./target/release/can-upgrade
```

## CAN 接口支持

- **Windows**: PEAK PCAN
- **Linux**: SocketCAN

## 许可证

请参阅项目根目录中的 LICENSE 文件。
