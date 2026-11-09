"""主机侧自检：校验 README/文档里用到的 Modbus 示例帧。

用法：python tools/selftest.py
"""

import sys


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def main() -> int:
    # 读从站 1 的 3 个保持寄存器：01 03 0000 0003 + CRC
    request = bytes.fromhex("01 03 00 00 00 03 05 CB")
    # 应答：01 03 06 00FA 01F4 0001 + CRC，即 25.0 ℃ / 50.0 % / 状态 1
    response = bytes.fromhex("01 03 06 00 FA 01 F4 00 01 78 AF")

    for frame in (request, response):
        want = frame[-2] | (frame[-1] << 8)
        got = crc16(frame[:-2])
        assert got == want, f"{frame.hex(' ')} CRC {got:04X} != {want:04X}"

    assert response[3:5] == b"\x00\xFA", "温度应为 250"
    assert response[5:7] == b"\x01\xF4", "湿度应为 500"
    assert response[7:9] == b"\x00\x01", "状态应为 1"

    print("modbus example frames: CRC and values OK")
    print("selftest passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())