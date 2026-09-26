#!/usr/bin/python3
# Synthetic exec fixture. Wire construction is independent of the C++ codec.
import os, socket, struct, sys, time, signal
if sys.argv[1:] != ['--ipc-fd=3', '--mode=frontend-spawned']:
    sys.exit(91)
s = socket.socket(fileno=3)
mode = os.environ.get('AV_CONNECTION_CASE')
if not mode:
    s.sendall(b'P'); s.close(); sys.exit(23)
def frame(t, r=0, sid=0, body=b'', version=1):
    return struct.pack('>4sHHHHIQQ', b'AVCP',version,t,0,0,len(body),r,sid)+body
def exact(n):
    b=b''
    while len(b)<n:
        v=s.recv(n-len(b))
        if not v: sys.exit(0)
        b+=v
    return b
def read():
    h=struct.unpack('>4sHHHHIQQ',exact(32))
    assert h[0]==b'AVCP' and h[3:5]==(0,0)
    return h[2],h[6],h[7],exact(h[5])
if mode=='hello-stall':
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    while True: time.sleep(10)
t,r,sid,body=read(); assert (t,r,sid,body)==(1,1,0,b'\x00\x01\x00\x01')
hello=frame(2,r,body=struct.pack('>HII',1,65536,0)+bytes(range(16)),version=0)
# Coalesced stream frames and fragmented handshake exercise real stream boundaries.
for b in hello: s.sendall(bytes([b]))
session=0
last_request=1
held=[]
ordinary=[]
while True:
    t,r,sid,body=read()
    assert r>last_request
    last_request=r
    if t==3:
        session+=1; assert body==b'\x00\x18\x00\x50\x00\x04\x00\x00'
        out=frame(4,r,session,body)
        data=b'A\x00\xff' if session==1 else b'Bxy'
        out+=frame(9,sid=session,body=struct.pack('>Q',1)+data)
        if session==1 and mode=='streams': out+=frame(10,sid=1,body=struct.pack('>BIBQB',1,7,0,1,1))
        s.sendall(out)
    elif t in (5,6,14):
        if mode=='reserve' and t in (5,6):
            ordinary.append((t,body))
            # 47 saturated user requests, reply1, reply2, later user + resize.
            if len(ordinary)==48:
                assert ordinary[:2]==[(5,b'u'*30720),(5,b'u'*30720)]
                assert ordinary[2:47]==[(6,b'\x00\x1e\x00\x64')]*45
                assert ordinary[47]==(5,b'r'*2048)
                time.sleep(0.2)
                for old_t,old_r,old_sid in held:
                    s.sendall(frame(13,old_r,old_sid,struct.pack('>H',old_t)))
                held=[]
            elif len(ordinary)>48:
                expected=[(5,b's'*2048),(5,b'z'),(6,b'\x00\x28\x00\x78')]
                assert ordinary[-1]==expected[len(ordinary)-49]
                if len(ordinary)==51:
                    s.sendall(frame(9,sid=sid,body=struct.pack('>Q',2)+b'ordered'))
            else:
                held.append((t,r,sid)); continue
        s.sendall(frame(13,r,sid,struct.pack('>H',t)))
    elif t==7:
        s.sendall(frame(11,r,sid,struct.pack('>Q',1)))
    elif t==8:
        s.sendall(frame(13,r,0,b'\x00\x08'))
        if mode in ('ack-stall','ack-close-stall','stopped-core'):
            signal.signal(signal.SIGTERM,signal.SIG_IGN)
            if mode=='ack-close-stall': s.close()
            if mode=='stopped-core': os.kill(os.getpid(),signal.SIGSTOP)
            time.sleep(5); sys.exit(88)  # bounded fixture fallback, not expected behavior
        s.close(); sys.exit(0)
    else: sys.exit(94)
