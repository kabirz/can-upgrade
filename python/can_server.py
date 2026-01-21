import can
import struct
import tqdm
from enum import IntEnum, UNIQUE, verify, auto

PLATFORM_RX=0x101
PLATFORM_TX=0x102
FW_DATA_RX=0x103

@verify(UNIQUE)
class Board(IntEnum):
    BOARD_START_UPDATE = 0
    BOARD_CONFIRM = auto()
    BOARD_VERSION = auto()
    BOARD_REBOOT = auto()

@verify(UNIQUE)
class FwCode(IntEnum):
    FW_CODE_OFFSET = 0
    FW_CODE_UPDATE_SUCCESS = auto()
    FW_CODE_VERSION = auto()
    FW_CODE_CONFIRM = auto()
    FW_CODE_FLASH_ERROR = auto()
    FW_CODE_TRANFER_ERROR = auto()

bus = can.Bus('can0', interface='socketcan')
try:
    total_size = 0
    wf = None
    APPVERSION=0x01020300
    for msg in bus:
        if msg.arbitration_id == PLATFORM_RX:
            code, val = struct.unpack('<2I', msg.data)
            if code == Board.BOARD_VERSION:
                data = struct.pack('<2I', FwCode.FW_CODE_VERSION, APPVERSION)
                APPVERSION += 1 << 8
            elif code == Board.BOARD_REBOOT:
                print('Recive reboot command')
                continue
            elif code == Board.BOARD_START_UPDATE:
                total_size = val
                bar = tqdm.tqdm(total=total_size)
                if wf:
                    wf.close()
                wf = open('/tmp/a.bin', 'wb')
                data = struct.pack('<2I', FwCode.FW_CODE_OFFSET, bar.n)
            elif code == Board.BOARD_CONFIRM:
                data = struct.pack('<2I', FwCode.FW_CODE_CONFIRM, 0x55AA55AA)
                if val == 0:
                    print('Test mode')
            s_msg = can.Message(arbitration_id=PLATFORM_TX, data=data, is_extended_id=False)
            bus.send(s_msg)
        elif msg.arbitration_id == FW_DATA_RX:
            if not wf:
                print('Not found write file')
                break
            bar.update(msg.dlc)
            wf.write(msg.data)
            if bar.n == total_size:
                wf.close()
                bar.close()
                wf = None
                data = struct.pack('<2I', FwCode.FW_CODE_UPDATE_SUCCESS, bar.n)
                bus.send(s_msg)
            elif bar.n % 64 == 0:
                data = struct.pack('<2I', FwCode.FW_CODE_OFFSET, bar.n)
                bus.send(s_msg)
except BaseException:
    pass
finally:
    bus.shutdown()
