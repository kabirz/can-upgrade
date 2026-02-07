import argparse
import os
import struct
import sys
import tqdm
import serial
import serial.tools.list_ports
import time

# 响应码定义
FW_CODE_OFFSET = 0
FW_CODE_UPDATE_SUCCESS = 1
FW_CODE_VERSION = 2
FW_CODE_CONFIRM = 3
FW_CODE_FLASH_ERROR = 4
FW_CODE_TRANFER_ERROR = 5

# 命令码定义
BOARD_START_UPDATE = 0
BOARD_CONFIRM = 1
BOARD_VERSION = 2
BOARD_REBOOT = 3

# UART协议帧格式定义
FRAME_HEAD = 0xAA
FRAME_TAIL = 0x55
FRAME_CMD = 0x01
FRAME_DATA = 0x02


def calc_crc16(data):
    """计算CRC16-CCITT"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_frame(type, data):
    """构建UART帧"""
    frame = bytearray()
    frame.append(FRAME_HEAD)
    frame.append(type)
    frame.extend(struct.pack('>H', len(data)))  # 大端序长度
    frame.extend(data)
    crc = calc_crc16(data)
    frame.extend(struct.pack('>H', crc))  # 大端序CRC
    frame.append(FRAME_TAIL)
    return bytes(frame)


def parse_frame(buffer):
    """解析UART帧，返回(frame_type, data, consumed_bytes)或(None, None, consumed_bytes)"""
    if len(buffer) < 4:  # 最小帧长度: HEAD(1) + TYPE(1) + LEN(2) + CRC(2) + TAIL(1)
        return None, None, 0

    # 查找帧头
    head_idx = buffer.find(FRAME_HEAD)
    if head_idx == -1:
        return None, None, len(buffer)  # 丢弃所有缓冲区
    if head_idx > 0:
        return None, None, head_idx  # 丢弃帧头之前的所有数据

    # 检查是否有足够的数据读取长度
    if len(buffer) < 4:
        return None, None, 0

    # 读取帧类型和长度
    frame_type = buffer[1]
    data_len = struct.unpack('>H', buffer[2:4])[0]

    # 检查数据长度是否有效
    if data_len > 8:
        return None, None, 1  # 丢弃无效的帧头，尝试下一个字节

    # 计算完整帧长度
    total_len = 1 + 1 + 2 + data_len + 2 + 1  # HEAD + TYPE + LEN + DATA + CRC + TAIL

    # 检查是否接收到完整的帧
    if len(buffer) < total_len:
        return None, None, 0

    # 提取数据部分
    data_start = 4
    data_end = data_start + data_len
    data = bytes(buffer[data_start:data_end])

    # 校验CRC
    crc_start = data_end
    crc_end = crc_start + 2
    received_crc = struct.unpack('>H', buffer[crc_start:crc_end])[0]
    calculated_crc = calc_crc16(data)

    if received_crc != calculated_crc:
        return None, None, 1  # CRC错误，丢弃帧头

    # 检查帧尾
    if buffer[crc_end] != FRAME_TAIL:
        return None, None, 1  # 帧尾错误，丢弃帧头

    return frame_type, data, total_len


def uart_recv(ser: serial.Serial, timeout=5):
    """接收UART响应"""
    buffer = bytearray()
    start_time = time.time()

    while True:
        if time.time() - start_time > timeout:
            raise BaseException("UART receive timeout")

        # 读取可用数据
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            # 尝试解析帧
            frame_type, frame_data, consumed = parse_frame(buffer)

            if frame_type is not None:
                # 成功解析帧
                buffer = buffer[consumed:]
                # 解析数据: 2个uint32_t
                if len(frame_data) == 8:
                    code, value = struct.unpack('<2I', frame_data)
                    return code, value
            else:
                # 移除已消费的数据
                if consumed > 0:
                    buffer = buffer[consumed:]

        time.sleep(0.001)


def firmware_upgrade(ser, file_name, test=False):
    """固件升级"""
    with open(file_name, 'rb') as f:
        total_size = os.path.getsize(file_name)

        # 发送开始升级命令
        cmd_data = struct.pack('<2I', BOARD_START_UPDATE, total_size)
        frame = build_frame(FRAME_CMD, cmd_data)
        ser.write(frame)

        # 等待响应
        bar = tqdm.tqdm(total=total_size, unit='B', unit_scale=True)
        code, offset = uart_recv(ser)
        if code != FW_CODE_OFFSET or offset != 0:
            raise BaseException(f"Flash erase error: code({code}), offset({offset})")

        # 发送固件数据
        while True:
            chunk = f.read(8)
            if not chunk:
                break

            # 发送数据帧
            frame = build_frame(FRAME_DATA, chunk)
            ser.write(frame)
            bar.update(len(chunk))

            # 每64字节或发送完毕后接收确认
            if bar.n % 64 != 0 and bar.n != total_size:
                continue

            code, offset = uart_recv(ser)
            if code == FW_CODE_UPDATE_SUCCESS and offset == bar.n:
                break
            if code != FW_CODE_OFFSET:
                raise BaseException(f"Firmware upload error: code({code}), offset({bar.n}, {offset})")

        bar.close()

        # 发送确认命令
        cmd_data = struct.pack('<2I', BOARD_CONFIRM, 0 if test else 1)
        frame = build_frame(FRAME_CMD, cmd_data)
        ser.write(frame)

        code, value = uart_recv(ser, timeout=30)
        if code == FW_CODE_CONFIRM and value == 0x55AA55AA:
            print(f"Image {file_name} upload finished! Please reboot board for upgrade.")
        elif code == FW_CODE_TRANFER_ERROR:
            print("Download Failed")


def firmware_version(ser):
    """获取固件版本"""
    cmd_data = struct.pack('<2I', BOARD_VERSION, 0)
    frame = build_frame(FRAME_CMD, cmd_data)
    ser.write(frame)

    code, version = uart_recv(ser)
    if code == FW_CODE_VERSION:
        ver1, ver2, ver3 = (version >> 24) & 0xff, (version >> 16) & 0xff, (version >> 8) & 0xff
        print(f"Version: v{ver1}.{ver2}.{ver3}")


def board_reboot(ser):
    """重启开发板"""
    cmd_data = struct.pack('<2I', BOARD_REBOOT, 0)
    frame = build_frame(FRAME_CMD, cmd_data)
    ser.write(frame)


def list_serial_ports():
    """列出可用的串口"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("No serial ports found")
        return []

    print("Available serial ports:")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port.device} - {port.description}")

    return ports


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UART Firmware Upgrade Tool")
    parser.add_argument('-p', '--port', type=str, help='Serial port (e.g., COM3, /dev/ttyUSB0)')
    parser.add_argument('-b', '--baudrate', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('-l', '--list', action='store_true', help='List available serial ports')

    subparser = parser.add_subparsers(dest='command')
    flash_parser = subparser.add_parser('flash', help='Firmware upgrade')
    flash_parser.add_argument('file', help='Firmware file path')
    flash_parser.add_argument('-t', '--test', action='store_true', help='Upgrade only for test, will revert next reboot')

    board_parser = subparser.add_parser('board', help='Board operations')
    board_parser.add_argument('-r', '--reboot', action='store_true', help='Reboot board')
    board_parser.add_argument('-v', '--version', action='store_true', help='Get board version')

    args = parser.parse_args()

    # 列出可用串口
    if args.list:
        list_serial_ports()
        sys.exit(0)

    # 确定串口
    port = args.port
    if not port:
        # 尝试自动检测串口
        ports = list_serial_ports()
        if len(ports) == 1:
            port = ports[0].device
            print(f"Auto-selected port: {port}")
        elif len(ports) > 1:
            print("\nPlease specify the port using -p option")
            sys.exit(1)
        else:
            sys.exit(1)

    try:
        # 打开串口
        ser = serial.Serial(port, args.baudrate, timeout=1, write_timeout=1)
        print(f"Connected to {port} at {args.baudrate} baud")

        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        if args.command == 'flash':
            firmware_upgrade(ser, args.file, test=args.test)
        elif args.command == 'board':
            if args.reboot:
                board_reboot(ser)
            elif args.version:
                firmware_version(ser)
        else:
            parser.print_help()

        ser.close()

    except serial.SerialException as e:
        print(f"Serial port error: {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        if 'ser' in locals() and ser.is_open:
            ser.close()
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        if 'ser' in locals() and ser.is_open:
            ser.close()
        sys.exit(1)
