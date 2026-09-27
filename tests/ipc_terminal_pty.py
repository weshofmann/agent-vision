#!/usr/bin/env python3
"""Outer-terminal qualification for the opt-in real IPC presentation harness."""
import argparse,errno,fcntl,json,os,pty,select,signal,struct,sys,termios,time
from pathlib import Path

def run(binary,mode,folder):
    folder.mkdir(parents=True,exist_ok=False)
    master,slave=pty.openpty()
    fcntl.ioctl(slave,termios.TIOCSWINSZ,struct.pack('HHHH',40,120,0,0))
    before=termios.tcgetattr(slave)
    pid=os.fork()
    if pid==0:
        os.close(master);os.setsid();fcntl.ioctl(slave,termios.TIOCSCTTY,0)
        for fd in (0,1,2):os.dup2(slave,fd)
        if slave>2:os.close(slave)
        env={'PATH':'/usr/bin:/bin','TERM':'xterm-256color','LANG':'en_US.UTF-8','HOME':'/nonexistent'}
        for key in ('GOGC','GODEBUG'):
            if key in os.environ:env[key]=os.environ[key]
        app=os.fork()
        if app==0:os.execve(str(binary),[str(binary),mode,str(folder/'native.log')],env)
        _,status=os.waitpid(app,0)
        after=termios.tcgetattr(0)
        restored=after==before
        (folder/'termios-diagnostic.json').write_text(json.dumps({'before':str(before),'after':str(after)}))
        os.write(1,b'\r\nOUTER_READY\r\n')
        readable,_,_=select.select([0],[],[],5)
        cooked=os.read(0,128) if readable else b''
        restored=termios.tcgetattr(0)==before
        (folder/'outer.json').write_text(json.dumps({'status':status,'termios_restored':restored,'cooked_input':cooked==b'outer-check\n'}))
        os._exit(0 if status==0 and restored and cooked==b'outer-check\n' else 1)
    raw=bytearray();deadline=time.monotonic()+90;sent=False;status=None
    while time.monotonic()<deadline:
        readable,_,_=select.select([master],[],[],.02)
        if readable:
            try:data=os.read(master,65536)
            except OSError as e:
                if e.errno!=errno.EIO:raise
                data=b''
            raw.extend(data)
            if b'\x1b[6n' in data:os.write(master,b'\x1b[1;1R')
            if not sent and b'OUTER_READY' in raw:os.write(master,b'outer-check\n');sent=True
        done,status=os.waitpid(pid,os.WNOHANG)
        if done:break
    else:
        # Only outer fixture owner; never inventory or signal terminal sessions.
        os.kill(pid,signal.SIGTERM);os.waitpid(pid,0)
        (folder/'screen.bin').write_bytes(raw)
        raise RuntimeError('native outer deadline; source/log preserved')
    (folder/'screen.bin').write_bytes(raw);os.close(master);os.close(slave)
    result=json.loads((folder/'outer.json').read_text())
    assert status==0 and result=={'status':0,'termios_restored':True,'cooked_input':True},result
    print('PASS',mode,'actual termios and cooked-input restoration')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    # Evidence folders are unique per invocation; never overwrite failed captures.
    folder=a.output/str(time.time_ns());folder.mkdir(parents=True)
    for mode in ('normal','loss'):run(a.binary.resolve(),mode,folder/mode)
