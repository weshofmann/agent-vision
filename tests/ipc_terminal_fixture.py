#!/usr/bin/python3
"""Synthetic session executable: no shell/host data in presentation evidence."""
import os
import struct
import termios
import fcntl

def out(data):
    while data:
        n = os.write(1, data)
        data = data[n:]

def line():
    data = b''
    while not data.endswith(b'\n'):
        data += os.read(0, 1)
    return data.strip()

out(b'IPC_READY\r\n')
while True:
    command = line()
    if command == b'heavy':
        block = b'X' * 1024 + b'\r\n'
        for _ in range(1024):
            out(block)
        out(b'HEAVY_BARRIER\r\n')
        # Remain alive until the frontend has validated/consumed/published data.
    elif command == b'size':
        rows, cols, _, _ = struct.unpack('HHHH', fcntl.ioctl(0, termios.TIOCGWINSZ, b'\0'*8))
        out(('GEOMETRY_%d_%d\r\n' % (rows, cols)).encode())
    elif command == b'dsr':
        old = termios.tcgetattr(0)
        raw = termios.tcgetattr(0)
        raw[3] &= ~(termios.ICANON | termios.ECHO)
        raw[6][termios.VMIN] = 1
        raw[6][termios.VTIME] = 0
        termios.tcsetattr(0, termios.TCSANOW, raw)
        out(b'\x1b[H\x1b[6n')
        received = b''
        while len(received) < 7:
            received += os.read(0, 7-len(received))
        termios.tcsetattr(0, termios.TCSANOW, old)
        out(b'CPR_' + received.hex().encode() + b'\r\n')
    elif command == b'ping':
        out(b'PONG\r\n')
    elif command == b'exit':
        out(b'FINAL_RETAINED\r\n')
        os._exit(7)
