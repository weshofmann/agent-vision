#!/usr/bin/python3
"""Synthetic AVCP peer for actual desktop failure/confirmation boundaries.
No PTYs or shell children: real shell ownership remains desktop_pty's separate gate.
Only typed requests/counters are retained in local audit evidence.
"""
import json
import os
from pathlib import Path
import socket
import select
import struct

peer = socket.socket(fileno=3)
mode = os.environ['AV_DESKTOP_CASE']
audit = Path(os.environ['AV_DESKTOP_AUDIT'])
seen = []
created = 0
held_close = None
control = Path(os.environ.get('AV_DESKTOP_CONTROL', str(audit)+'.control'))
released_close_count = 0
a_output_sequence = 0

def save():
    audit.write_text(json.dumps({'requests': seen, 'created': created, 'released_close_count': released_close_count})+'\n')

def exact(n):
    data = b''
    while len(data) < n:
        chunk = peer.recv(n-len(data))
        if not chunk: raise EOFError
        data += chunk
    return data

def read():
    magic, version, kind, flags, reserved, count, request, session = struct.unpack('>4sHHHHIQQ', exact(32))
    assert magic == b'AVCP' and flags == reserved == 0 and count <= 65536
    body = exact(count)
    seen.append({'type': kind, 'session': session})
    save()
    return kind, request, session, body

def frame(kind, request=0, session=0, body=b'', version=1):
    return struct.pack('>4sHHHHIQQ', b'AVCP', version, kind, 0, 0, len(body), request, session)+body

def send(kind, request=0, session=0, body=b'', version=1):
    peer.sendall(frame(kind, request, session, body, version))

kind, request, _, body = read()
assert kind == 1 and body == b'\x00\x01\x00\x01'
send(2, request, body=struct.pack('>HII', 1, 65536, 0)+bytes(16), version=0)
try:
    while True:
        if control.exists():
            action = control.read_text().strip()
            control.unlink()
            if action == 'lose': break
            if action == 'closed':
                assert held_close is not None
                close_request, close_session = held_close
                if mode in ('close-drag', 'close-grab', 'close-mouse'):
                    send(10, session=close_session, body=struct.pack('>BIBQB', 1, 7, 0, 1, 7))
                send(11, close_request, close_session, struct.pack('>Q', 1))
                held_close = None
                released_close_count += 1
                save()
        readable, _, _ = select.select([peer], [], [], .02)
        if not readable: continue
        kind, request, session, body = read()
        if kind == 3:
            if mode == 'crash-before-created': break
            if mode == 'second-create-error' and created == 1:
                send(12, request, body=struct.pack('>HIH', 6, 0, 0))
                continue
            created += 1
            assert len(body) == 8
            send(4, request, created, body)
            save()
            if mode == 'crash-after-created': break
            if created == 2 and mode == 'second-created-loss': break
            if created == 2:
                send(9, session=2, body=struct.pack('>Q', 1)+b'FIXTURE_READY\r\n')
                if mode == 'exit-then-loss':
                    send(10, session=2, body=struct.pack('>BIBQB', 1, 7, 0, 1, 1))
        elif kind == 6:
            send(12, request, session, struct.pack('>HIH', 9, 0, 0))
        elif kind == 5:
            send(13, request, session, struct.pack('>H', 5))
            if session == 1:
                a_output_sequence += 1
                send(9, session=1, body=struct.pack('>Q', a_output_sequence)+b'A_PROGRESS\r\n')
                if held_close and mode == 'close-barrier':
                    close_request, close_session = held_close
                    send(11, close_request, close_session, struct.pack('>Q', 1))
                    held_close = None
        elif kind == 14:
            send(13, request, session, struct.pack('>H', 14))
        elif kind == 7:
            assert session == 2 and held_close is None
            if mode not in ('close-drag', 'close-grab', 'close-mouse'):
                send(10, session=2, body=struct.pack('>BIBQB', 1, 7, 0, 1, 7))
            held_close = (request, session)
        elif kind == 8:
            send(13, request, body=struct.pack('>H', 8))
            break
        else: raise AssertionError('unexpected desktop request')
except EOFError:
    pass
finally:
    save()
    peer.close()
