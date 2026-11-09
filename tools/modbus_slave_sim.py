"""Minimal Modbus RTU slave for the gateway demo (function 0x03 only).

    pip install pyserial
    python tools/modbus_slave_sim.py COM7 --temp 25.0 --hum 50.0

Runtime commands typed into this console:
    t 30.5   set temperature (C)
    h 60     set humidity (%)
    s 0      set status register
    q        quit
"""

import argparse
import struct
import sys
import threading

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class Registers:
    def __init__(self, temp: float, hum: float, status: int):
        self.lock = threading.Lock()
        self.values = [round(temp * 10), round(hum * 10), status]

    def set(self, index: int, value: int) -> None:
        with self.lock:
            self.values[index] = value

    def snapshot(self, count: int) -> list:
        with self.lock:
            return self.values[:count]


def console(regs: Registers, stop: threading.Event) -> None:
    for line in sys.stdin:
        parts = line.split()
        if not parts:
            continue
        cmd = parts[0].lower()
        if cmd == "q":
            stop.set()
            return
        try:
            if cmd == "t":
                regs.set(0, round(float(parts[1]) * 10))
            elif cmd == "h":
                regs.set(1, round(float(parts[1]) * 10))
            elif cmd == "s":
                regs.set(2, int(parts[1]))
            else:
                print("commands: t <C> | h <%> | s <n> | q")
                continue
            print(f"registers -> {regs.values}")
        except (IndexError, ValueError):
            print("bad value")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", help="CH340 port, e.g. COM7")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--slave", type=int, default=1)
    ap.add_argument("--temp", type=float, default=25.0)
    ap.add_argument("--hum", type=float, default=50.0)
    ap.add_argument("--status", type=int, default=1)
    args = ap.parse_args()

    regs = Registers(args.temp, args.hum, args.status)
    stop = threading.Event()
    threading.Thread(target=console, args=(regs, stop), daemon=True).start()

    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        print(f"listening on {args.port} @ {args.baud}, slave {args.slave}, "
              f"registers {regs.values}")
        buf = b""
        while not stop.is_set():
            buf += port.read(port.in_waiting or 1)
            while len(buf) >= 8:
                frame, buf = buf[:8], buf[8:]
                if crc16(frame[:6]) != (frame[6] | frame[7] << 8):
                    print(f"bad CRC: {frame.hex(' ')}")
                    continue
                addr, func = frame[0], frame[1]
                if addr != args.slave or func != 0x03:
                    continue
                start, count = struct.unpack(">HH", frame[2:6])
                if count < 1 or count > 16 or start + count > 3:
                    body = bytes([args.slave, 0x83, 0x02])
                else:
                    values = regs.snapshot(start + count)[start:]
                    payload = b"".join(struct.pack(">H", v & 0xFFFF) for v in values)
                    body = bytes([args.slave, 0x03, len(payload)]) + payload
                crc = crc16(body)
                port.write(body + bytes([crc & 0xFF, crc >> 8]))
                print(f"req {frame.hex(' ')} -> {body.hex(' ')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
