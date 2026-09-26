#!/usr/bin/env python3
"""Real outer-PTY acceptance checks. Publish only the whitelisted summary.
Raw synthetic screens stay under ignored .probe; no environment/process dumps.
"""
import argparse
import ctypes
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import secrets
import signal
import struct
import subprocess
import sys
import termios
import time
import pyte

class Screen(pyte.Screen):
    def set_margins(self, *args, private=False):
        if not private:
            super().set_margins(*args)

def size_response_present(screen, nonce, rows, cols):
    # A fresh response cannot be a prior viewport or the printf command echo.
    # Inspect reconstructed cells: the compositor may emit cursor movement in
    # place of the space between stty's two dimensions.
    return f'SCROLL_SIZE_{nonce}_{rows} {cols}' in '\n'.join(screen.display)

def resources(pid):
    """Only counts for the owned app PID; no process/FD names are captured."""
    if sys.platform != 'darwin':
        raise RuntimeError('Whole-app resource qualification currently requires macOS libproc')
    lib = ctypes.CDLL('/usr/lib/libproc.dylib', use_errno=True)
    lib.proc_pidinfo.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_uint64,
                                ctypes.c_void_p, ctypes.c_int]
    lib.proc_pidinfo.restype = ctypes.c_int
    # proc_taskinfo: six uint64_t values, followed by twelve int32_t values.
    # pti_threadnum is the tenth int32_t. Layout from the macOS SDK proc_info.h.
    task = ctypes.create_string_buffer(96)
    assert lib.proc_pidinfo(pid, 4, 0, task, len(task)) == len(task)
    threads = struct.unpack_from('i', task.raw, 48 + 9*4)[0]
    fd_bytes = lib.proc_pidinfo(pid, 1, 0, None, 0)
    assert fd_bytes > 0
    fds = ctypes.create_string_buffer(fd_bytes + 64)
    actual = lib.proc_pidinfo(pid, 1, 0, fds, len(fds))
    assert actual > 0 and actual % 8 == 0
    return {'threads': threads, 'fds': actual//8}

class Desktop:
    def __init__(self, binary, folder, ignored_sigchld=False):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        # These two owned result files cannot be reused across probe runs.
        for name in ('app-pid.txt', 'outer-result.json'):
            (folder/name).unlink(missing_ok=True)
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 0, 0))
        self.screen = Screen(120, 40)
        self.stream = pyte.ByteStream(self.screen)
        self.raw = bytearray()
        self.dsr = 0
        self.reaped = False
        self.pid = os.fork()
        if self.pid == 0:
            os.close(self.master)
            os.setsid()
            fcntl.ioctl(self.slave, termios.TIOCSCTTY, 0)
            for fd in (0, 1, 2): os.dup2(self.slave, fd)
            if self.slave > 2: os.close(self.slave)
            before = termios.tcgetattr(0)
            app = os.fork()
            if app == 0:
                if ignored_sigchld: signal.signal(signal.SIGCHLD, signal.SIG_IGN)
                os.chdir('/tmp')
                env = {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'SHELL': '/bin/sh',
                       'TERM': 'xterm-256color', 'LANG': 'en_US.UTF-8',
                       'HOME': '/nonexistent', 'ENV': '/dev/null', 'PS1': 'probe> '}
                os.execve(str(binary), [str(binary)], env)
            (folder / 'app-pid.txt').write_text(str(app))
            _, status = os.waitpid(app, 0)
            os.write(1, b'\r\nOUTER_READY\r\n')
            readable, _, _ = select.select([0], [], [], 5)
            recovered = os.read(0, 128) if readable else b''
            result = {'app_wait_status': status, 'cooked_input_restored': recovered == b'outer-check\n',
                      'termios_restored_after_input': before == termios.tcgetattr(0)}
            (folder / 'outer-result.json').write_text(json.dumps(result))
            os._exit(0 if status == 0 else 1)
        self.app_pid = None
        deadline = time.monotonic() + 5
        while not (folder / 'app-pid.txt').exists() and time.monotonic() < deadline:
            self.drain(.03)
        self.app_pid = int((folder / 'app-pid.txt').read_text())

    def drain(self, seconds=.1):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.master], [], [], min(.03, max(0, deadline-time.monotonic())))
            if ready:
                try: data = os.read(self.master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO: return
                    raise
                if not data: return
                self.raw.extend(data)
                self.stream.feed(data)
                queries = self.raw.count(b'\x1b[6n')
                while self.dsr < queries:
                    os.write(self.master, f'\x1b[{self.screen.cursor.y+1};{self.screen.cursor.x+1}R'.encode())
                    self.dsr += 1

    def text(self): return '\n'.join(self.screen.display)
    def wait(self, predicate, reason, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.drain(.04)
            if predicate(): return
        raise AssertionError(reason + '\n' + self.text())
    def contains(self, text): return text in self.text()
    def send(self, text):
        os.write(self.master, text.encode())
        self.drain(.08)
    def command(self, text): self.send(text+'\r')
    def menu(self, key):
        self.send('\x02')
        self.wait(lambda: self.contains('Close Term'), 'Ctrl-B menu did not open')
        self.send(key)
    def confirm(self, yes):
        self.send('\r' if yes else '\t\r')
        self.wait(lambda: not self.contains(' Confirm '), 'confirmation did not finish')
    def bounds(self, label):
        found = []
        def frame_complete():
            rows = self.screen.display
            try:
                top = next(y for y, row in enumerate(rows) if f'Terminal {label} [' in row)
                left = rows[top].index('╔')
                right = rows[top].index('╗')
                bottom = next(y for y in range(top+1, len(rows)) if rows[y][left] in ('╚', '└'))
                found[:] = [left, top, right+1, bottom+1]
                return True
            except (StopIteration, ValueError):
                return False  # A PTY read may end halfway through a screen draw.
        self.wait(frame_complete, 'focused frame did not finish drawing')
        return found
    def snapshot(self, name): (self.folder / (name+'.txt')).write_text(self.text()+'\n')
    def absent(self, pid):
        result = subprocess.run(['/bin/ps', '-o', 'pid=', '-p', str(pid)], capture_output=True, text=True)
        return not result.stdout.strip()
    def restore(self):
        self.wait(lambda: b'OUTER_READY' in self.raw, 'normal quit did not return outer terminal')
        self.send('outer-check\n')
        self.wait(lambda: (self.folder / 'outer-result.json').exists(), 'outer supervisor did not finish')
        result = json.loads((self.folder / 'outer-result.json').read_text())
        assert result['app_wait_status'] == 0
        assert result['cooked_input_restored'] and result['termios_restored_after_input']
        assert b'\x1b[?1049h' in self.raw and b'\x1b[?1049l' in self.raw
        deadline = time.monotonic()+2
        while time.monotonic()<deadline:
            waited, status = os.waitpid(self.pid, os.WNOHANG)
            if waited:
                self.reaped = True
                assert status == 0
                return result
            self.drain(.03)
        raise AssertionError('outer supervisor did not exit')
    def close(self):
        # Unconditional teardown even if local diagnostics cannot be written.
        try: (self.folder / 'outer-output.ansi').write_bytes(self.raw)
        finally:
            if not self.reaped:
                try: os.killpg(self.pid, signal.SIGKILL)
                except ProcessLookupError: pass
            for fd in (self.master, self.slave):
                try: os.close(fd)
                except OSError: pass
            if not self.reaped:
                deadline = time.monotonic()+2
                while time.monotonic()<deadline:
                    try: waited, _ = os.waitpid(self.pid, os.WNOHANG)
                    except ChildProcessError: break
                    if waited: break
                    time.sleep(.02)

def inner(bounds): return [bounds[3]-bounds[1]-2, bounds[2]-bounds[0]-2]
def shell_identity(desktop, label, cwd):
    desktop.command(f"label={label}; PS1='{label}> '; cd {cwd}; printf 'SESSION_%s PID_%s CWD_%s\\n' \"$label\" \"$$\" \"$PWD\"; tty")
    desktop.wait(lambda: re.search(f'SESSION_{label} PID_(\\d+) CWD_{re.escape(cwd)}', desktop.text()), 'shell identity/state missing')
    pid = int(re.search(f'SESSION_{label} PID_(\\d+) CWD_', desktop.text()).group(1))
    tty = re.search(r'/dev/ttys?\w+', desktop.text()).group(0)
    return pid, tty

def interaction(binary, folder):
    d = Desktop(binary, folder, ignored_sigchld=True)
    try:
        d.wait(lambda: d.contains('Terminal A [pid ') and d.contains('Terminal B [pid '), 'two initial visible terminal windows missing')
        initial_resources = resources(d.app_pid)
        initial_b = d.bounds('B')
        b_pid, b_tty = shell_identity(d, 'B', '/')
        d.menu('\t')
        initial_a = d.bounds('A')
        a_pid, a_tty = shell_identity(d, 'A', '/tmp')
        assert a_pid != b_pid and a_tty != b_tty
        d.menu('\t'); d.menu('r')
        d.send('\x1b[1;2D'*5 + '\x1b[1;2A'*3 + '\x1b[C'*3 + '\x1b[B'*2 + '\r')
        b = d.bounds('B'); assert b != initial_b
        d.command("printf 'FOCUS_%s SIZE_' \"$label\"; stty size")
        rows, cols = inner(b)
        d.wait(lambda: d.contains(f'FOCUS_B SIZE_{rows} {cols}'), 'B focus/PTY dimensions wrong')
        d.menu('\t')
        assert d.bounds('A') == initial_a
        d.menu('r'); d.send('\x1b[1;2D'*7 + '\x1b[1;2A'*4 + '\x1b[C'*2 + '\x1b[B' + '\r')
        a = d.bounds('A'); assert a != initial_a
        rows, cols = inner(a)
        d.command("printf 'FOCUS_%s SIZE_' \"$label\"; stty size")
        d.wait(lambda: d.contains(f'FOCUS_A SIZE_{rows} {cols}'), 'A independent resize/focus wrong')
        d.snapshot('both-moved')
        # Foreground job receives terminal interrupt through the actual input path.
        d.command("sleep 30; printf 'INTERRUPTED_%s\\n' \"$label\"")
        d.drain(.2); d.send('\x03')
        d.wait(lambda: d.contains('INTERRUPTED_A'), 'foreground interrupt did not return to shell')
        d.menu('\t')
        d.command("printf 'RETAIN_%s\\n' B_DONE; exit 7")
        d.wait(lambda: d.contains('Terminal B [exited 7]') and d.contains('RETAIN_B_DONE'), 'exact exit status/output missing')
        d.send('abc')
        assert d.contains('Terminal B [exited 7]') and d.contains('RETAIN_B_DONE')
        assert d.absent(b_pid), 'closed/exited B direct child still present'
        after_b = resources(d.app_pid)
        assert after_b == {'threads': initial_resources['threads']-2, 'fds': initial_resources['fds']-1}, 'B workers/master FD not released while exited view retained'
        d.snapshot('retained-exit')
        d.menu('\t'); d.command("printf 'SURVIVOR_%s CWD_%s\\n' \"$label\" \"$PWD\"")
        d.wait(lambda: d.contains('SURVIVOR_A CWD_/tmp'), 'exit-key fell through or survivor state lost')
        d.menu('\t'); d.menu('w')
        d.wait(lambda: not d.contains('Terminal B ['), 'explicit exited close failed')
        d.command("printf 'LIVE_%s\\n' \"$label\"")
        d.wait(lambda: d.contains('LIVE_A'), 'closing B damaged A')
        d.command('exit 0')
        d.wait(lambda: d.contains('Terminal A [exited 0]'), 'A exact status missing')
        d.send('z'); assert d.contains('Terminal A [exited 0]')
        after_a = resources(d.app_pid)
        assert after_a == {'threads': initial_resources['threads']-4, 'fds': initial_resources['fds']-2}, 'A workers/master FD not released while exited view retained'
        d.menu('w'); d.menu('q')
        restored = d.restore()
        assert d.absent(a_pid)
        return {'two_distinct_pids_and_ptys': True, 'independent_cwd_state_and_focus': True,
                'keyboard_moves_resize_z_order_overlap': True, 'a_inner_rows_cols': inner(a),
                'b_inner_rows_cols': inner(b), 'exited_output_status_retained': True,
                'arbitrary_exit_keys_swallowed': True, 'explicit_exited_close_survivor_usable': True,
                'foreground_interrupt_qualified': True, 'direct_children_absent_after_exit': True,
                'inherited_ignored_sigchld_handled': True,
                'owned_resources': {'initial': initial_resources, 'b_retained': after_b, 'both_retained': after_a}, **restored}
    finally: d.close()

def live_close_quit(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [pid ') and d.contains('Terminal B [pid '), 'two initial windows missing')
        pids = [int(re.search(f'Terminal {label} \\[pid (\\d+)\\]', d.text()).group(1)) for label in ('A','B')]
        initial_resources = resources(d.app_pid)
        d.menu('w')
        d.wait(lambda: d.contains('Terminate'), 'live close was not confirmed')
        d.confirm(False)  # Explicit No.
        assert d.contains('Terminal B [pid '), 'cancel close destroyed window'
        d.command("printf 'CANCEL_%s\\n' OK")
        d.wait(lambda: d.contains('CANCEL_OK'), 'cancel close changed live input routing')
        d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'second close confirmation missing')
        d.confirm(True)  # Explicit Yes, the default button.
        d.wait(lambda: not d.contains('Terminal B ['), 'confirmed live close did not close B')
        assert d.absent(pids[1]), 'live-closed B direct child remains'
        after_close = resources(d.app_pid)
        assert after_close == {'threads': initial_resources['threads']-2, 'fds': initial_resources['fds']-1}, 'live close leaked workers/master FD'
        d.command("printf 'SURVIVOR_%s\\n' OK")
        d.wait(lambda: d.contains('SURVIVOR_OK'), 'live close damaged survivor')
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'live quit was not confirmed')
        d.confirm(False); assert d.contains('Terminal A [pid '), 'cancel quit destroyed application'
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'second quit confirmation missing')
        d.confirm(True)
        restored = d.restore()
        assert all(d.absent(pid) for pid in pids), 'owned direct children remain after normal quit'
        return {'live_close_cancel_confirm': True, 'live_quit_cancel_confirm': True,
                'direct_children_absent_after_live_close_quit': True,
                'owned_resources': {'initial': initial_resources, 'after_live_close': after_close}, **restored}
    finally: d.close()

def quit_both(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [pid ') and d.contains('Terminal B [pid '), 'two initial windows missing')
        pids = [int(re.search(f'Terminal {label} \\[pid (\\d+)\\]', d.text()).group(1)) for label in ('A','B')]
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 2 live'), 'quit did not count both live shells')
        d.confirm(True)
        restored = d.restore()
        assert all(d.absent(pid) for pid in pids), 'two-live quit left a direct child'
        return {'confirmed_quit_both_live': True, 'both_direct_children_absent': True, **restored}
    finally: d.close()

def scrolling(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [pid ') and d.contains('Terminal B [pid '), 'two initial windows missing')
        pids = [int(re.search(f'Terminal {label} \\[pid (\\d+)\\]', d.text()).group(1)) for label in ('A','B')]
        initial = resources(d.app_pid)
        d.command("i=0; while [ \"$i\" -lt 80 ]; do printf 'ROW_%s\\n' \"$i\"; i=$((i+1)); done; printf 'SCROLL_%s\\n' DONE")
        # Observe the rendered output stream, not just a command echo or a
        # potentially older scrollback viewport. Missing markers alone are not
        # treated as deadlock proof; the deterministic C++ test proves R1.
        d.wait(lambda: b'SCROLL_DONE' in d.raw, 'finite scrolling output not rendered')
        d.command("printf 'AFTER_%s\\n' SCROLL_INPUT")
        d.wait(lambda: b'AFTER_SCROLL_INPUT' in d.raw, 'shell input after scroll failed')
        d.menu('r'); d.send('\x1b[C' + '\x1b[1;2D' + '\r')
        rows, cols = inner(d.bounds('B'))
        nonce = secrets.token_hex(4)
        d.command(f"printf 'SCROLL_SIZE_%s_' {nonce}; stty size")
        d.wait(lambda: size_response_present(d.screen, nonce, rows, cols),
               'scroll/resize child size mismatch')
        d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'scrolling live-close confirmation missing')
        d.confirm(True)
        d.wait(lambda: not d.contains('Terminal B ['), 'scrolling B close failed')
        after_close = resources(d.app_pid)
        assert after_close == {'threads': initial['threads']-2, 'fds': initial['fds']-1}
        assert d.absent(pids[1])
        d.command("printf 'SCROLL_SURVIVOR_%s\\n' OK")
        d.wait(lambda: d.contains('SCROLL_SURVIVOR_OK'), 'scrolling close damaged survivor')
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'scrolling survivor quit confirmation missing')
        d.confirm(True); restored = d.restore()
        assert all(d.absent(pid) for pid in pids)
        return {'finite_scrolling_output_followed_by_input': True,
                'menu_move_resize_after_scroll': True, 'child_size_after_scroll': [rows, cols],
                'confirmed_close_survivor_quit_cleanup': True,
                'owned_resources': {'initial': initial, 'after_close': after_close}, **restored}
    finally: d.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, default=Path('.probe/desktop'))
    args = parser.parse_args()
    assert args.binary.is_file(), 'AgentVision executable is not implemented'
    results = {'interaction': interaction(args.binary.resolve(), args.output/'interaction'),
               'live_close_quit': live_close_quit(args.binary.resolve(), args.output/'live-close-quit'),
               'quit_both': quit_both(args.binary.resolve(), args.output/'quit-both'),
               'scrolling': scrolling(args.binary.resolve(), args.output/'scrolling')}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))
