#!/usr/bin/env python3
"""Independent AVCP client using literal schemas and synthetic inputs only."""
import argparse
import ctypes
import os
import pathlib
import signal
import socket
import struct
import subprocess
import tempfile
import time

HEADER = struct.Struct('!4sHHHHIQQ')
HELLO = bytes.fromhex('415643500000000100000000000000040000000000000001000000000000000000010001')


def resources(pid):
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    lib.proc_pidinfo.restype = ctypes.c_int
    task = ctypes.create_string_buffer(96)
    assert lib.proc_pidinfo(pid, 4, 0, task, len(task)) == len(task)
    size = lib.proc_pidinfo(pid, 1, 0, None, 0)
    descriptors = ctypes.create_string_buffer(size + 128)
    actual = lib.proc_pidinfo(pid, 1, 0, descriptors, len(descriptors))
    assert actual > 0 and actual % 8 == 0
    return actual // 8, struct.unpack_from('i', task.raw, 84)[0], len(children(pid))


def children(pid):
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_listchildpids.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    lib.proc_listchildpids.restype = ctypes.c_int
    storage = ctypes.create_string_buffer(128)
    count = lib.proc_listchildpids(pid, storage, len(storage))
    assert 0 <= count <= 32
    return struct.unpack_from('=' + 'i' * count, storage.raw) if count else ()


def absent(pid):
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_int]
    lib.proc_pidinfo.restype = ctypes.c_int
    bsd = ctypes.create_string_buffer(256)
    return lib.proc_pidinfo(pid, 3, 0, bsd, len(bsd)) == 0



class Client:
    def __init__(self, binary, shell='/bin/sh'):
        self.socket, endpoint = socket.socketpair()
        self.socket.settimeout(5)
        # The backend endpoint is dup'd to FD3 after closing the frontend end.
        source = endpoint.fileno()
        if source == 3:
            source = os.dup(source)
        actions = [(os.POSIX_SPAWN_CLOSE, self.socket.fileno()),
                   (os.POSIX_SPAWN_DUP2, source, 3),
                   (os.POSIX_SPAWN_CLOSE, source)]
        env = {'PATH': '/usr/bin:/bin', 'SHELL': shell, 'HOME': '/nonexistent',
               'ENV': '/dev/null', 'PS1': '', 'TERM': 'xterm-256color', 'LANG': 'C'}
        self.pid = os.posix_spawn(str(binary), [str(binary), '--ipc-fd=3', '--mode=frontend-spawned'], env, file_actions=actions)
        if source != endpoint.fileno():
            os.close(source)
        endpoint.close()
        self.request = 1
        self.frames = []
        self.outputs = {}
        self.sequence = {}
        self.terminal = set()
        self.socket.sendall(HELLO)
        kind, request, session, body = self.read()
        assert (kind, request, session) == (2, 1, 0)
        assert body[:10] == bytes.fromhex('00010001000000000000') and len(body) == 26 and any(body[10:])
        self.reaped = False

    def send(self, kind, session=0, body=b''):
        self.request += 1
        self.socket.sendall(HEADER.pack(b'AVCP', 1, kind, 0, 0, len(body), self.request, session) + body)
        return self.request

    def exact(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                raise EOFError('core stream ended before expected completion')
            data.extend(chunk)
        return bytes(data)

    def read(self):
        magic, version, kind, flags, reserved, size, request, session = HEADER.unpack(self.exact(32))
        assert magic == b'AVCP' and flags == reserved == 0 and size <= 65536
        assert version == (0 if kind == 2 else 1)
        body = self.exact(size)
        if kind == 9:
            assert request == 0 and session not in self.terminal and 9 <= size <= 32776
            sequence = struct.unpack('!Q', body[:8])[0]
            assert sequence == self.sequence.get(session, 0) + 1
            self.sequence[session] = sequence
            self.outputs.setdefault(session, bytearray()).extend(body[8:])
            self.send(14, session, struct.pack('!I', size - 8))
        elif kind in (10, 11):
            last = struct.unpack('!Q', body[6:14] if kind == 10 else body)[0]
            assert last == self.sequence.get(session, 0)
            self.terminal.add(session)
        frame = kind, request, session, body
        self.frames.append(frame)
        return frame

    def await_request(self, request):
        while True:
            frame = self.read()
            if frame[1] == request:
                return frame

    def create(self):
        request = self.send(3, body=bytes.fromhex('0018005000040000'))
        kind, _, session, body = self.await_request(request)
        assert kind == 4 and session != 0 and body == bytes.fromhex('0018005000040000')
        return session

    def input(self, session, data):
        request = self.send(5, session, data)
        assert self.await_request(request)[0::3] == (13, b'\x00\x05')

    def marker(self, session, marker):
        while marker not in self.outputs.get(session, b''):
            self.read()

    def close_session(self, session):
        request = self.send(7, session)
        assert self.await_request(request)[0] == 11

    def shutdown(self):
        request = self.send(8)
        assert self.await_request(request)[0::3] == (13, b'\x00\x08')
        assert self.socket.recv(1) == b''
        self.socket.close()
        pid, status = os.waitpid(self.pid, 0)
        self.reaped = True
        assert pid == self.pid and os.waitstatus_to_exitcode(status) == 0

    def cleanup(self):
        self.socket.close()
        if not self.reaped:
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                pid, status = os.waitpid(self.pid, os.WNOHANG)
                if pid:
                    self.reaped = True
                    return status
                time.sleep(.01)
            os.kill(self.pid, signal.SIGKILL)
            os.waitpid(self.pid, 0)
            raise AssertionError('core did not exit after contact loss')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    args = parser.parse_args()
    binary = pathlib.Path(args.binary).resolve()
    client = Client(binary)
    try:
        # Warm the Go netpoller/PTY wait resources, then compare stable idle counts.
        warm = client.create()
        client.close_session(warm)
        time.sleep(.05)
        baseline = resources(client.pid)
        assert baseline[2] == 0, 'warm direct child remains'
        a, b = client.create(), client.create()
        assert resources(client.pid)[2] == 2, 'two direct children not observed'
        client.input(a, b"stty -echo; printf 'INPUT_OK\\n'\n")
        client.marker(a, b'INPUT_OK\r\n')
        resize = client.send(6, a, bytes.fromhex('00210061'))
        assert client.await_request(resize)[0::3] == (13, b'\x00\x06')
        client.input(a, b"stty size; printf '\\000\\377RAW_OK\\n'; printf 'TAIL_OK\\n'; exit 7\n")
        exited = None
        while exited is None:
            frame = client.read()
            if frame[0] == 10 and frame[2] == a:
                exited = frame
        assert exited[3][:6] == bytes.fromhex('010000000700')
        assert b'33 97\r\n' in client.outputs[a]
        assert b'\x00\xffRAW_OK\r\n' in client.outputs[a] and b'TAIL_OK\r\n' in client.outputs[a]
        client.close_session(a)
        client.input(b, b"exec /bin/sh -c 'kill -TERM $$'\n")
        while True:
            frame = client.read()
            if frame[0] == 10 and frame[2] == b:
                assert frame[3][:6] == bytes.fromhex('020000000f00')
                break
        client.close_session(b)
        time.sleep(.05)
        after = resources(client.pid)
        assert after[2] == 0, 'direct-child baseline not restored'
        assert after[0] == baseline[0], 'owned IPC/PTY/wake FD baseline not restored'
        # Go runtime threads can grow lazily; stable kernel counts are observed,
        # while exact userspace worker joins are asserted by native Go tests.
        assert after[1] <= baseline[1] + 2, 'unexpected kernel worker growth'
        live = client.create()
        client.input(live, b"printf 'LIVE_OK\\n'\n")
        client.marker(live, b'LIVE_OK\r\n')
        owned = children(client.pid)
        assert len(owned) == 1
        client.shutdown()
        assert all(absent(pid) for pid in owned), 'Shutdown left observed direct child present'
    finally:
        client.cleanup()
    # A native exec fixture inventories descriptors above stderr and checks the
    # controlling terminal, no-login argv/env and resize signal at exec boundary.
    root = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='av-core-child-') as temp:
        fixture = pathlib.Path(temp) / 'tty-child'
        subprocess.run(['/usr/bin/clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        str(root / 'core/internal/session/testdata/tty-child.c'), '-o', str(fixture)], check=True)
        client = Client(binary, str(fixture))
        try:
            session = client.create()
            client.marker(session, b'TTY_OK 24 80')
            request = client.send(6, session, bytes.fromhex('00210061'))
            assert client.await_request(request)[0] == 13
            client.marker(session, b'WINCH 33 97')
            client.input(session, b'exit\n')
            while True:
                frame = client.read()
                if frame[0] == 10:
                    assert frame[3][:6] == bytes.fromhex('010000001100')
                    break
            client.close_session(session)
            client.shutdown()
        finally:
            client.cleanup()
    for failure in ('endpoint-eof', 'core-sigterm', 'malformed'):
        client = Client(binary)
        try:
            session = client.create()
            client.input(session, b"printf 'OWNED_OK\\n'\n")
            client.marker(session, b'OWNED_OK\r\n')
            owned = children(client.pid)
            assert len(owned) == 1
            if failure == 'core-sigterm':
                os.kill(client.pid, signal.SIGTERM)
            elif failure == 'malformed':
                client.socket.sendall(HEADER.pack(b'AVCP', 1, 8, 1, 0, 0, client.request + 1, 0))
            else:
                client.socket.shutdown(socket.SHUT_WR)
            while client.socket.recv(65536):
                pass
            status = client.cleanup()
            assert (os.waitstatus_to_exitcode(status) == 0) == (failure == 'endpoint-eof'), (failure, os.waitstatus_to_exitcode(status))
            assert all(absent(pid) for pid in owned), 'failure cleanup left observed direct child present'
        finally:
            client.cleanup()
    print('PASS: literal golden, input/resize, arbitrary raw bytes, tail/exit7, SIGTERM, Close/Shutdown, FD baseline, native exec FD/tty inventory, endpoint/core-signal/malformed cleanup')

if __name__ == '__main__':
    main()
