# CAN 固件升级工具 - Python

命令行版本的 CAN 总线固件升级工具，支持 Windows (PCAN) 和 Linux (SocketCAN)。

## 功能

- 固件升级
- 版本查询
- 设备重启
- 临时固件升级测试

## 依赖

```
pydantic-settings==2.12.0
python-can==4.6.1
rich-argparse==1.7.2
tqdm==4.67.1
```

## 安装

```bash
pip install -r requirements.txt
```

## 使用方法

### 查看帮助

```bash
python can_upgrade_new.py --help
```

### 典型命令

```bash
# 查询版本
python can_upgrade_new.py --version

# 升级固件
python can_upgrade_new.py --update firmware.bin

# 重启设备
python can_upgrade_new.py --reboot
```

## 支持的 CAN 接口

- **Windows**: PCAN (需要安装 PCAN-Basic 驱动)
- **Linux**: SocketCAN (如 can0, vcan0)
