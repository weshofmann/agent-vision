#!/usr/bin/env python3
"""Outer-terminal qualification for the opt-in real IPC presentation harness."""
import argparse,errno,fcntl,json,os,pty,select,signal,struct,sys,termios,time
from pathlib import Path
import pyte

class Screen(pyte.Screen):
    def set_margins(self, *args, private=False):
        if not private:
            super().set_margins(*args)


def terminal_decoder(screen=None):
    screen=screen if screen is not None else Screen(120,40)
    stream=pyte.ByteStream(screen)
    return screen, stream.feed


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
    screen,feed=terminal_decoder();caption=False;caption_ack=False;outer_seen=False
    while time.monotonic()<deadline:
        readable,_,_=select.select([master],[],[],.02)
        if readable:
            try:data=os.read(master,65536)
            except OSError as e:
                if e.errno!=errno.EIO:raise
                data=b''
            raw.extend(data)
            feed(data)
            if not outer_seen and 'authority/contact lost' in '\n'.join(screen.display):
                caption=True
            if caption and not caption_ack and mode!='premature-loss':
                os.write(master,b'~');caption_ack=True
            outer_seen |= b'OUTER_READY' in raw
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
    (folder/'display.json').write_text(json.dumps({'caption_visible_before_restoration':caption}))
    assert result['termios_restored'] and result['cooked_input'],result
    native=(folder/'native.log').read_text()
    if mode in ('premature-loss','no-caption'):
        assert result['status']!=0, 'negative qualification falsely accepted'
        reason='premature restoration/contact loss' if mode=='premature-loss' else 'loss caption publication acknowledgment missing'
        assert reason in native and 'COMPLETION contact=1 core=1 value=9 graceful=0' in native
        assert 'PASS actual presentation' not in native
        assert not caption if mode=='no-caption' else True
    else:
        assert status==0 and result['status']==0,result
        assert 'WORKLOAD completed=1 intentional_loss='+str(int(mode=='loss'))+' cycles=16 credits=16 A_completed=16' in native
        assert native.count('CREDIT_COMPLETE cycle=')==16
        assert native.count('WORKERS joined=2 cycle=')==17
    if mode=='loss':
        assert caption, 'authority/contact loss caption was never displayed'
    print('PASS',mode,'qualification oracle, actual termios and cooked-input restoration')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('--output',type=Path,required=True);p.add_argument('--cases',nargs='+',default=['normal','loss']);a=p.parse_args()
    # Evidence folders are unique per invocation; never overwrite failed captures.
    folder=a.output/str(time.time_ns());folder.mkdir(parents=True)
    for mode in a.cases:run(a.binary.resolve(),mode,folder/mode)
