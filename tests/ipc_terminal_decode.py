#!/usr/bin/env python3
"""Offline cell regression for the native fixture's actual byte decoder."""
import argparse
import hashlib
from pathlib import Path
from ipc_terminal_pty import Screen, terminal_decoder

CAPTION='authority/contact lost'

def decode(parts):
    screen,feed=terminal_decoder()
    for data in parts:
        feed(data)
    return screen

def cells(screen):
    return (tuple(screen.display),screen.cursor.x,screen.cursor.y)

def split_regression():
    payloads=[b'\x1b[H'+'═X'.encode(),
              b'\x1b[?1;2r\x1b[H'+'┌═é界𝄞┐\r\nIPC [authority/contact lost]'.encode()]
    cuts=0
    for index,payload in enumerate(payloads):
        reference=decode([payload])
        if index==0:
            assert reference.display[0].startswith('═X')
        else:
            assert reference.display[0].startswith('┌═é界𝄞┐')
            assert reference.buffer[0][3].data=='界' and reference.buffer[0][4].data==''
            assert reference.buffer[0][5].data=='𝄞' and reference.buffer[0][6].data=='┐'
            assert CAPTION in reference.display[1]
        for split in range(1,len(payload)):
            actual=decode([payload[:split],payload[split:]])
            assert cells(actual)==cells(reference),('split byte',split,'row',actual.display[0][:20])
            cuts+=1
        assert cells(decode([payload[i:i+1] for i in range(len(payload))]))==cells(reference)
    print('PASS offline UTF-8 all',cuts,'one-cut byte boundaries and byte-at-a-time; exact cells/cursor/wide columns/caption')

class CaptionScreen(Screen):
    def __init__(self):
        super().__init__(120,40)
        self.caption=False
    def draw(self,data):
        super().draw(data)
        # Observe actual cells at draw boundaries, before any later erase in a chunk.
        self.caption |= CAPTION in '\n'.join(self.display)

def replay(path,expected):
    raw=path.read_bytes();screen=CaptionScreen();_,feed=terminal_decoder(screen)
    for start in range(0,len(raw),4096):
        feed(raw[start:start+4096])
    assert screen.caption==expected,(path.name,screen.caption,expected)
    print('PASS offline retained capture caption',screen.caption,'bytes',len(raw),'sha256',hashlib.sha256(raw).hexdigest())

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--capture',nargs=2,action='append',default=[])
    args=parser.parse_args();split_regression()
    for path,expected in args.capture:
        replay(Path(path),bool(int(expected)))
