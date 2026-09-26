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
        v=s.recv(min(n-len(b),997) if mode=='partial-write' else n-len(b))
        if mode=='partial-write' and n>32: time.sleep(0.0005)
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
if mode=='slow-drip':
    s.sendall(b'A'); time.sleep(5); sys.exit(88)
if mode=='oversize':
    s.sendall(struct.pack('>4sHHHHIQQ',b'AVCP',1,9,0,0,65537,0,1)); time.sleep(5); sys.exit(88)
if mode=='wrong-ack':
    s.sendall(frame(13,55,1,b'\x00\x05')); time.sleep(5); sys.exit(88)
session=0
credit_held=None
last_request=1
held=[]
ordinary=[]
reply_held=None
partial_inputs=0
control_held=[]
control_credit=set()
control_close=set()
control_resizes=0
while True:
    t,r,sid,body=read()
    assert r>last_request
    last_request=r
    if t==3:
        session+=1
        if mode=='stale': session=1  # deliberate fully-retired ID reuse to challenge old handles
        assert body==b'\x00\x18\x00\x50\x00\x04\x00\x00'
        out=frame(4,r,session,body)
        data=b'A\x00\xff' if session==1 else b'Bxy'
        if mode not in ('blocked-writer','partial-write'):
            out+=frame(9,sid=session,body=struct.pack('>Q',1)+data)
        if mode=='credit-detach' and session==1:
            out+=frame(9,sid=1,body=struct.pack('>Q',2)+b'tail')
        if mode=='overflow-output':
            out=b''.join([frame(4,r,session,body)]+[frame(9,sid=session,body=struct.pack('>Q',i)+b'x') for i in range(1,130)])
        if mode=='malformed-status':
            out+=frame(10,sid=session,body=struct.pack('>BIBQB',1,0,0,2,1))
        if (session==1 and mode=='streams') or mode in ('flush-close','stale'): out+=frame(10,sid=1,body=struct.pack('>BIBQB',1,7,0,1,1))
        s.sendall(out)
        if mode=='blocked-writer' and session==16:
            signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(5); sys.exit(88)
    elif t in (5,6,14):
        if mode=='early-shutdown' and t==6:
            s.sendall(frame(9,sid=sid,body=struct.pack('>Q',2)+b'held')); continue
        if mode=='control-lanes':
            assert t in (6,14)
            control_held.append((t,r,sid))
            if t==6:
                control_resizes+=1
                if control_resizes==47: s.sendall(frame(9,sid=1,body=struct.pack('>Q',2)+b'full'))
            else:
                assert sid not in control_credit and body==b'\x00\x00\x00\x03'
                control_credit.add(sid)
            continue
        if mode=='unexpected-credit-error' and t==14:
            s.sendall(frame(12,r,sid,b'\x00\x03'+bytes(6))); continue
        if mode=='zero-consumed' and t==6:
            s.sendall(frame(9,sid=sid,body=struct.pack('>Q',2)+b'next'))
        if mode=='partial-write' and t==5:
            assert body==bytes([sid])*30720
            partial_inputs+=1
            if partial_inputs==32: s.sendall(frame(9,sid=16,body=struct.pack('>Q',1)+b'complete'))
        if mode=='credit-detach' and t==14 and sid==1:
            assert body in (b'\x00\x00\x00\x03',b'\x00\x00\x00\x04')
            if credit_held is None: credit_held=(r,sid); continue
            s.sendall(frame(12,r,sid,b'\x00\x03'+bytes(6))); continue
        if mode=='credit-detach' and t==6:
            s.sendall(frame(9,sid=sid,body=struct.pack('>Q',2)+b'alive'))
        if mode=='reserve' and t==6 and sid==2:
            assert reply_held is not None
            s.sendall(frame(9,sid=2,body=struct.pack('>Q',3)+b'progress'))
            old_t,old_r,old_sid=reply_held
            s.sendall(frame(13,old_r,old_sid,struct.pack('>H',old_t)))
            reply_held=None
            s.sendall(frame(13,r,sid,struct.pack('>H',t))); continue
        if mode=='reserve' and t in (5,6):
            ordinary.append((t,body))
            # 47 saturated user requests, reply1, reply2, later user + resize.
            if len(ordinary)==48:
                assert ordinary[:2]==[(5,b'u'*30720),(5,b'u'*30720)]
                assert ordinary[2:47]==[(6,b'\x00\x1e\x00\x64')]*45
                assert ordinary[47]==(5,b'r'*2048)
                for old_t,old_r,old_sid in held:
                    s.sendall(frame(13,old_r,old_sid,struct.pack('>H',old_t)))
                held=[]
                reply_held=(t,r,sid)
                s.sendall(frame(9,sid=2,body=struct.pack('>Q',2)+b'ready'))
                continue
            elif len(ordinary)>48:
                expected=[(5,b's'*2048),(5,b'z'),(6,b'\x00\x28\x00\x78')]
                assert ordinary[-1]==expected[len(ordinary)-49]
                if len(ordinary)==51:
                    s.sendall(frame(9,sid=sid,body=struct.pack('>Q',2)+b'ordered'))
            else:
                held.append((t,r,sid)); continue
        s.sendall(frame(13,r,sid,struct.pack('>H',t)))
    elif t==7:
        if mode=='control-lanes':
            assert sid not in control_close
            control_close.add(sid); control_held.append((t,r,sid)); continue
        s.sendall(frame(11,r,sid,struct.pack('>Q',2 if mode=='credit-detach' else 0 if mode=='partial-write' else 1)))
        if mode=='credit-detach':
            assert credit_held is not None
            old_r,old_sid=credit_held
            s.sendall(frame(12,old_r,old_sid,b'\x00\x03'+bytes(6)))
    elif t==8:
        if mode=='control-lanes':
            assert control_resizes==47 and len(control_credit)==16 and len(control_close)==16
            for old_t,old_r,old_sid in sorted(control_held,key=lambda x:x[0]!=7):
                if old_t==7: s.sendall(frame(11,old_r,old_sid,struct.pack('>Q',2 if old_sid==1 else 1)))
                else: s.sendall(frame(13,old_r,old_sid,struct.pack('>H',old_t)))
        if mode=='partial-write': assert partial_inputs==32
        s.sendall(frame(13,r,0,b'\x00\x08'))
        if mode in ('ack-stall','ack-close-stall','stopped-core'):
            signal.signal(signal.SIGTERM,signal.SIG_IGN)
            if mode=='ack-close-stall': s.close()
            if mode=='stopped-core': os.kill(os.getpid(),signal.SIGSTOP)
            time.sleep(5); sys.exit(88)  # bounded fixture fallback, not expected behavior
        s.close(); sys.exit(0)
    else: sys.exit(94)
