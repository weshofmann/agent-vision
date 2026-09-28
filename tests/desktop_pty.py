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
import shutil
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
    def __init__(self, binary, folder, ignored_sigchld=False, extra_env=None):
        self.folder = folder
        folder.mkdir(parents=True, exist_ok=True)
        # These two owned result files cannot be reused across probe runs.
        for name in ('app-pid.txt', 'outer-result.json'):
            (folder/name).unlink(missing_ok=True)
        self.master, self.slave = pty.openpty()
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', 40, 120, 0, 0))
        self.outer_before = termios.tcgetattr(self.slave)
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
                env.update(extra_env or {})
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
    def command(self, text):
        # Send typed text and the explicit Return separately. Turbo Vision may
        # classify a burst of several text events, including Return, as paste.
        self.send(text)
        self.send('\r')
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
    def restore(self, expected_exit=0):
        self.wait(lambda: b'OUTER_READY' in self.raw, 'normal quit did not return outer terminal')
        self.send('outer-check\n')
        self.wait(lambda: (self.folder / 'outer-result.json').exists(), 'outer supervisor did not finish')
        result = json.loads((self.folder / 'outer-result.json').read_text())
        assert result['app_wait_status'] == expected_exit << 8
        assert result['cooked_input_restored'] and result['termios_restored_after_input']
        assert b'\x1b[?1049h' in self.raw and b'\x1b[?1049l' in self.raw
        assert not self.screen.cursor.hidden, 'outer cursor not restored'
        deadline = time.monotonic()+2
        while time.monotonic()<deadline:
            waited, status = os.waitpid(self.pid, os.WNOHANG)
            if waited:
                self.reaped = True
                assert status == expected_exit << 8
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

def ownership(desktop):
    # Assert direct ownership, not a process dump or a caption-derived identity.
    def children(parent):
        rows = subprocess.run(['/bin/ps', '-axo', 'pid=,ppid='], capture_output=True,
                              text=True, check=True).stdout.splitlines()
        return [int(row.split()[0]) for row in rows
                if len(row.split()) == 2 and int(row.split()[1]) == parent]
    cores = children(desktop.app_pid)
    assert len(cores) == 1, 'frontend must own exactly the Go core child'
    shells = children(cores[0])
    assert len(shells) == 2, 'Go core must own two direct shell children'
    assert '[pid ' not in desktop.text(), 'raw shell PID leaked into a public caption'
    return cores[0], sorted(shells)

def interaction(binary, folder):
    d = Desktop(binary, folder, ignored_sigchld=True)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'), 'two initial visible terminal windows missing')
        core_pid, _ = ownership(d)
        initial_resources = resources(d.app_pid)
        initial_core_fds = resources(core_pid)['fds']
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
        assert resources(core_pid)['fds'] == initial_core_fds-4, f'Go B master FD baseline: {initial_core_fds} -> {resources(core_pid)["fds"]}'
        assert after_b == {'threads': initial_resources['threads']-2, 'fds': initial_resources['fds']}, 'B presentation workers/shared IPC FD not released while exited view retained'
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
        assert resources(core_pid)['fds'] == initial_core_fds-8, f'Go A master FD baseline: {initial_core_fds} -> {resources(core_pid)["fds"]}'
        assert after_a == {'threads': initial_resources['threads']-4, 'fds': initial_resources['fds']}, 'A presentation workers/shared IPC FD not released while exited view retained'
        d.menu('w'); d.menu('q')
        restored = d.restore()
        assert d.absent(core_pid), 'frontend did not reap its direct core child'
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
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'), 'two initial windows missing')
        core_pid, pids = ownership(d)
        initial_resources = resources(d.app_pid)
        d.menu('w')
        d.wait(lambda: d.contains('Terminate'), 'live close was not confirmed')
        d.confirm(False)  # Explicit No.
        assert d.contains('Terminal B [live]'), 'cancel close destroyed window'
        d.command("printf 'CANCEL_%s\\n' OK")
        d.wait(lambda: d.contains('CANCEL_OK'), 'cancel close changed live input routing')
        d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'second close confirmation missing')
        d.confirm(True)  # Explicit Yes, the default button.
        d.wait(lambda: not d.contains('Terminal B ['), 'confirmed live close did not close B')
        assert d.absent(pids[1]), 'live-closed B direct child remains'
        after_close = resources(d.app_pid)
        assert after_close == {'threads': initial_resources['threads']-2, 'fds': initial_resources['fds']}, 'live close leaked presentation workers/shared IPC FD'
        d.command("printf 'SURVIVOR_%s\\n' OK")
        d.wait(lambda: d.contains('SURVIVOR_OK'), 'live close damaged survivor')
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'live quit was not confirmed')
        d.confirm(False); assert d.contains('Terminal A [live]'), 'cancel quit destroyed application'
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'second quit confirmation missing')
        d.confirm(True)
        restored = d.restore()
        assert d.absent(core_pid), 'frontend did not reap its direct core child'
        assert all(d.absent(pid) for pid in pids), 'owned direct children remain after normal quit'
        return {'live_close_cancel_confirm': True, 'live_quit_cancel_confirm': True,
                'direct_children_absent_after_live_close_quit': True,
                'owned_resources': {'initial': initial_resources, 'after_live_close': after_close}, **restored}
    finally: d.close()

def quit_both(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'), 'two initial windows missing')
        core_pid, pids = ownership(d)
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 2 live'), 'quit did not count both live shells')
        d.confirm(True)
        restored = d.restore()
        assert d.absent(core_pid), 'frontend did not reap its direct core child'
        assert all(d.absent(pid) for pid in pids), 'two-live quit left a direct child'
        return {'confirmed_quit_both_live': True, 'both_direct_children_absent': True, **restored}
    finally: d.close()

def core_loss(binary, folder, during_modal=False, continuous=False):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'two initial Go windows missing')
        core_pid, shells = ownership(d)
        initial = resources(d.app_pid)
        d.command("i=0; while [ \"$i\" -lt 80 ]; do printf 'LOSS_ROW_%s\\n' \"$i\"; i=$((i+1)); done; printf 'LOSS_TAIL_%s\\n' READY")
        d.wait(lambda: b'LOSS_TAIL_READY' in d.raw, 'loss fixture output not rendered')
        if during_modal:
            d.menu('w')
            d.wait(lambda: d.contains('Terminate'), 'loss modal did not open')
        os.kill(core_pid, signal.SIGKILL)
        if continuous:
            for _ in range(30):
                d.send('x')
        d.wait(lambda: d.raw.count(b'\x1b[?1049l') >= 1 and
                          d.raw.count(b'\x1b[?1049h') >= 2,
               'runtime loss did not restore outer terminal before resuming retained desktop')
        if during_modal:
            assert d.contains('Terminate'), 'loss destroyed the active confirmation stack'
            d.confirm(False)
        d.wait(lambda: d.contains('backend lost'), 'core loss fabricated a shell status')
        assert d.contains('LOSS_TAIL_READY'), 'loss discarded retained cells'
        after_loss = resources(d.app_pid)
        assert after_loss['threads'] == initial['threads']-9, f'loss worker baseline: {initial} -> {after_loss}'
        # Post-finish local event pump still handles selection and modal movement.
        d.menu('s')
        d.wait(lambda: d.contains('Copy'), 'lost view selection controls unavailable')
        d.send('\x1b')
        d.menu('r'); d.send('\x1b[C' + '\r')
        assert d.contains('backend lost')
        d.menu('q')
        restored = d.restore()
        assert d.absent(core_pid)
        return {'runtime_restore_then_resume': True, 'lost_output_retained': True,
                'modal_loss_preserved': during_modal, 'continuous_input_serviced': continuous,
                'lost_local_selection_move': True, **restored}
    finally: d.close()

def loss_modal(binary, folder): return core_loss(binary, folder, during_modal=True)
def loss_continuous(binary, folder): return core_loss(binary, folder, continuous=True)

def restored_termios(actual, before):
    # Darwin sets PENDIN when cooked processing resumes. It describes pending
    # input retyping, not a terminal mode; the supervisor's cooked input read
    # clears it and restore() still compares every field exactly after that read.
    actual = list(actual)
    expected = list(before)
    actual[3] &= ~termios.PENDIN
    expected[3] &= ~termios.PENDIN
    return actual == expected

def stopped_quit(binary, folder, extra_env=None):
    d = Desktop(binary, folder, extra_env=extra_env)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'initial Go windows missing')
        core_pid, _ = ownership(d)
        os.kill(core_pid, signal.SIGSTOP)
        d.menu('q'); d.wait(lambda: d.contains('Terminate 2 live'), 'stopped core quit did not confirm')
        d.confirm(True)
        def outer_restored():
            actual = termios.tcgetattr(d.slave)
            observation = {'before': d.outer_before, 'actual': actual,
                           'alternate_screen_left': b'\x1b[?1049l' in d.raw,
                           'cursor_visible': not d.screen.cursor.hidden,
                           'stopped_core_state': subprocess.run(
                               ['/bin/ps', '-o', 'state=', '-p', str(core_pid)],
                               capture_output=True, text=True, check=True).stdout.strip()}
            (folder/'outer-restoration-observation.json').write_text(
                json.dumps(observation, default=lambda value: value.hex())+'\n')
            return (observation['alternate_screen_left'] and observation['cursor_visible'] and
                    restored_termios(actual, d.outer_before))
        d.wait(outer_restored, 'outer terminal was not restored while core was stopped', timeout=1)
        state = subprocess.run(['/bin/ps', '-o', 'state=', '-p', str(core_pid)],
                               capture_output=True, text=True, check=True).stdout.strip()
        assert 'T' in state, 'test did not observe restoration while the owned core was still stopped'
        assert restored_termios(termios.tcgetattr(d.slave), d.outer_before), 'outer termios not restored while core was stopped'
        assert not d.screen.cursor.hidden, 'outer cursor not restored while core was stopped'
        restored = d.restore()
        assert d.absent(core_pid)
        assert b'cleanup was not graceful' in d.raw, 'forced cleanup was reported as graceful'
        return {'outer_restored_while_core_stopped': True, 'stopped_core_reaped': True, **restored}
    finally: d.close()

def synthetic_desktop(binary, folder, mode):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit = folder/'audit.json'
    control = folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': mode, 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    try:
        if mode == 'second-created-loss':
            d.wait(lambda: b'OUTER_READY' in d.raw or d.raw.count(b'\x1b[?1049h') >= 2,
                   'second Created/loss left ready frontend suspended without recovery')
            if b'OUTER_READY' in d.raw:
                restored = d.restore(expected_exit=1)
                assert d.raw.count(b'Go core ') == 1, 'lost startup did not report one diagnostic'
                return {'second_created_loss_boundary': 'restored-startup-failure', **restored}
            d.wait(lambda: d.contains('backend lost'), 'ready startup race did not resume retained loss UI')
            d.menu('q'); restored = d.restore()
            return {'second_created_loss_boundary': 'ready-before-loss-resumed', **restored}
        if mode in ('crash-before-created', 'crash-after-created', 'second-create-error'):
            restored = d.restore(expected_exit=1)
            assert b'Terminal B [live]' not in d.raw, 'partial startup claimed both ready windows'
            assert d.raw.count(b'Go core ') == 1, 'startup failure lacked one diagnostic'
            record = json.loads(audit.read_text())
            assert record['created'] == (0 if mode == 'crash-before-created' else 1)
            if mode == 'second-create-error':
                assert record['requests'][-1]['type'] == 8, 'partial Create failure did not Shutdown core'
            return {'single_diagnostic': True, 'partial_create_never_ready': True, **restored}
        d.wait(lambda: d.contains('Terminal A [live]') and
                          (d.contains('Terminal B [live]') or (mode == 'exit-then-loss' and d.contains('Terminal B [exited 7]'))) and
                          d.contains('FIXTURE_READY'), 'synthetic desktop did not become ready')
        if mode in ('close-drag', 'close-grab', 'close-mouse'):
            initial_bounds = d.bounds('B')
            d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'interaction Close confirmation missing')
            d.confirm(True)
            d.wait(lambda: d.contains('Terminal B [closing]'), 'Close was not independently held')
            if mode == 'close-drag':
                d.menu('r')
                d.wait(lambda: d.contains('Arrows') and d.contains('Move'), 'keyboard drag did not start')
            elif mode == 'close-grab':
                d.menu('m'); d.send('g')
                d.wait(lambda: d.contains('Release Input'), 'modal input grab did not start')
            else:
                left, top, _, _ = initial_bounds
                d.send(f'\x1b[<0;{left+9};{top+1}M')
                d.send(f'\x1b[<32;{left+11};{top+2}M')
            control.write_text('closed')
            d.wait(lambda: json.loads(audit.read_text())['released_close_count'] == 1,
                   'independent Closed release did not occur')
            d.drain(.2)
            if mode != 'close-grab':
                assert d.contains('Terminal B ['), 'Closed destroyed a view on its active drag stack'
                if mode == 'close-drag': d.send('\x1b[C' + '\r')
                else: d.send(f'\x1b[<0;{left+11};{top+2}m')
            # End also lets input grab unwind through the accepted adapter.
            d.wait(lambda: not d.contains('Terminal B ['), 'Closed did not destroy after interaction unwound')
            rows = json.loads(audit.read_text())
            assert rows['released_close_count'] == 1 and sum(row['type'] == 7 for row in rows['requests']) == 1
            d.command('progress')
            d.wait(lambda: d.contains('A_PROGRESS'), 'interaction Close damaged survivor')
            d.menu('q'); d.wait(lambda: d.contains('Terminate 1 live'), 'interaction survivor quit missing')
            d.confirm(True); restored = d.restore()
            return {'closed_released_during_interaction': True, 'eventual_single_close': True,
                    'survivor_after_interaction_close': True, 'interaction': mode, **restored}
        if mode in ('exit-then-loss', 'close-then-loss'):
            if mode == 'close-then-loss':
                d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'pending loss Close confirmation missing')
                d.confirm(True)
            d.wait(lambda: d.contains('Terminal B [exited 7]'), 'known exit before loss missing')
            before = json.loads(audit.read_text())['requests']
            control.write_text('lose')
            d.wait(lambda: d.raw.count(b'\x1b[?1049h') >= 2, 'known-exit loss did not restore/resume')
            assert d.contains('Terminal B [exited 7]'), 'contact loss replaced authoritative exit result'
            d.menu('w')
            d.wait(lambda: not d.contains('Terminal B ['), 'known exited/pending Close window could not dismiss after loss')
            assert json.loads(audit.read_text())['requests'] == before, 'lost window attempted a backend request'
            d.menu('q'); restored = d.restore()
            return {'known_exit_preserved_after_loss': True, 'explicit_local_dismiss_after_loss': True,
                    'no_request_after_actual_loss': True, 'pending_close_retired': mode == 'close-then-loss', **restored}
        # Cancellation sends neither Close nor Shutdown; use audit only for protocol effects.
        d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'fake close confirmation missing')
        d.confirm(False)
        assert not any(row['type'] == 7 for row in json.loads(audit.read_text())['requests'])
        d.menu('q'); d.wait(lambda: d.contains('Terminate'), 'fake quit confirmation missing')
        d.confirm(False)
        assert not any(row['type'] == 8 for row in json.loads(audit.read_text())['requests'])
        d.menu('r'); d.send('\x1b[1;2D' + '\r')
        d.wait(lambda: d.contains('resize failed'), 'typed resize failure did not reach caption')
        d.menu('w'); d.wait(lambda: d.contains('Terminate'), 'confirmed fake close missing')
        d.confirm(True)
        d.wait(lambda: d.contains('Terminal B [exited 7]'), 'pending Close final status missing')
        assert d.contains('Terminal B ['), 'presentation destroyed before held correlated Closed'
        before = sum(row['type'] == 5 for row in json.loads(audit.read_text())['requests'])
        d.send('suppressed')
        after = sum(row['type'] == 5 for row in json.loads(audit.read_text())['requests'])
        assert before == after, 'confirmed Close admitted additional input'
        d.menu('\t'); d.command('progress')
        d.wait(lambda: d.contains('A_PROGRESS') and not d.contains('Terminal B ['),
               'Close did not allow A progress before correlated destruction')
        d.menu('q'); d.wait(lambda: d.contains('Terminate 1 live'), 'fake survivor quit missing')
        d.confirm(True); restored = d.restore()
        return {'cancel_close_quit_send_nothing': True, 'resize_error_visible': True,
                'held_close_retains_view_and_suppresses_input': True,
                'unrelated_session_progress_then_correlated_destroy': True, **restored}
    finally: d.close()

def startup_before(binary, folder): return synthetic_desktop(binary, folder, 'crash-before-created')
def startup_after(binary, folder): return synthetic_desktop(binary, folder, 'crash-after-created')
def startup_second(binary, folder): return synthetic_desktop(binary, folder, 'second-create-error')
def close_barrier(binary, folder): return synthetic_desktop(binary, folder, 'close-barrier')
def exit_then_loss(binary, folder): return synthetic_desktop(binary, folder, 'exit-then-loss')
def close_then_loss(binary, folder): return synthetic_desktop(binary, folder, 'close-then-loss')
def close_drag(binary, folder): return synthetic_desktop(binary, folder, 'close-drag')
def close_grab(binary, folder): return synthetic_desktop(binary, folder, 'close-grab')
def close_mouse(binary, folder): return synthetic_desktop(binary, folder, 'close-mouse')
def startup_final(binary, folder): return synthetic_desktop(binary, folder, 'second-created-loss')

def scrolling(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'), 'two initial windows missing')
        core_pid, pids = ownership(d)
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
        assert after_close == {'threads': initial['threads']-2, 'fds': initial['fds']}
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

def dynamic_basic(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'two initial windows missing')
        d.menu('n')
        d.wait(lambda: d.contains('Terminal C [live]'), 'New Terminal did not create C')
        d.menu('n')
        d.wait(lambda: d.contains('Terminal D [live]'), 'second New Terminal did not create D')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 4 live'), 'Quit did not count four live terminals')
        d.confirm(True)
        restored = d.restore()
        return {'four_distinct_windows': True, 'four_live_quit': True, **restored}
    finally: d.close()

def dynamic_four_shells(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'initial pair missing')
        d.menu('n')
        d.wait(lambda: d.contains('Terminal C [live]'), 'C missing')
        c_pid, c_tty = shell_identity(d, 'C', '/tmp')
        d.menu('n')
        d.wait(lambda: d.contains('Terminal D [live]'), 'D missing')
        d_pid, d_tty = shell_identity(d, 'D', '/')
        assert c_pid != d_pid and c_tty != d_tty, 'C and D share shell identity'
        def children(parent):
            rows = subprocess.run(['/bin/ps', '-axo', 'pid=,ppid='], capture_output=True,
                                  text=True, check=True).stdout.splitlines()
            return [int(row.split()[0]) for row in rows
                    if len(row.split()) == 2 and int(row.split()[1]) == parent]
        cores = children(d.app_pid)
        assert len(cores) == 1 and len(children(cores[0])) == 4, \
            'four views did not own four distinct direct Go-core shells'
        initial_d = d.bounds('D')
        d.menu('r')
        d.send('\x1b[1;2D'*4 + '\x1b[1;2A'*2 + '\r')
        resized_d = d.bounds('D')
        assert resized_d != initial_d, 'D resize did not change presentation bounds'
        rows, cols = inner(resized_d)
        d.command("printf 'D_SIZE_'; stty size")
        d.wait(lambda: d.contains(f'D_SIZE_{rows} {cols}'), 'D PTY size did not follow resize')
        d.command("printf 'D_TAIL\\n'; exit 7")
        d.wait(lambda: d.contains('Terminal D [exited 7]') and d.contains('D_TAIL'),
               'D exact exit status/output was not retained')
        assert d.absent(d_pid), 'D child remained after natural exit'
        for _ in range(3): d.menu('\t')
        d.command("printf 'C_STATE_%s_%s\\n' \"$label\" \"$PWD\"")
        d.wait(lambda: d.contains('C_STATE_C_/tmp'), 'C state changed after D exit')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 3 live'), 'retained D was counted as live')
        d.confirm(True)
        restored = d.restore()
        assert d.absent(cores[0]) and d.absent(c_pid), 'owned core/shell remained after Quit'
        return {'four_direct_shells': True, 'independent_resize': [rows, cols],
                'retained_exact_exit': True, 'independent_c_state': True, **restored}
    finally: d.close()

def dynamic_modal(binary, folder):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit, control = folder/'audit.json', folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': 'dynamic-held', 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    def creates():
        return sum(row['type'] == 3 for row in json.loads(audit.read_text())['requests'])
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'synthetic initial pair missing')
        d.menu('n')
        d.wait(lambda: creates() == 3, 'asynchronous third Create was not sent')
        d.menu('w')
        d.wait(lambda: d.contains('Terminate live terminal'), 'close confirmation missing')
        control.write_text('created')
        for _ in range(20): d.send('x')
        d.wait(lambda: any(row['type'] == 14 and row['session'] == 3
                           for row in json.loads(audit.read_text())['requests']),
               'prepared C did not consume output while modal and input were active')
        assert d.contains('Terminate live terminal') and not d.contains('Terminal C ['), \
            'Created stole modal focus or inserted early'
        d.confirm(False)
        d.wait(lambda: d.contains('Terminal C [live]') and d.contains('DYNAMIC_READY'),
               'prepared C was not inserted after modal cancellation')
        d.menu('n')
        d.wait(lambda: creates() == 4, 'fourth Create was not sent')
        control.write_text('error')
        d.wait(lambda: d.contains('Cannot launch terminal shell'),
               'Create Error did not explain launch failure')
        d.send('\r')
        d.wait(lambda: not d.contains('Cannot launch terminal shell'),
               'Create Error dialog did not dismiss')
        d.menu('n')
        d.wait(lambda: creates() == 5, 'Error did not free pending admission')
        control.write_text('created')
        d.wait(lambda: d.contains('Terminal E [live]'), 'replacement reused failed view label')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 4 live'), 'Quit count lost dynamic sessions')
        d.confirm(True)
        restored = d.restore()
        return {'modal_prepared_output': True, 'error_releases_pending': True,
                'stable_labels': True, **restored}
    finally: d.close()

def dynamic_drag(binary, folder):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit, control = folder/'audit.json', folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': 'dynamic-held', 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'synthetic initial pair missing')
        d.menu('n')
        d.wait(lambda: sum(row['type'] == 3 for row in json.loads(audit.read_text())['requests']) == 3,
               'pending third Create missing')
        original = d.bounds('B')
        d.menu('r')
        d.wait(lambda: d.contains('Arrows') and d.contains('Move'), 'keyboard drag did not begin')
        control.write_text('created')
        for _ in range(12): d.send('\x1b[C')
        d.wait(lambda: any(row['type'] == 14 and row['session'] == 3
                           for row in json.loads(audit.read_text())['requests']),
               'prepared controller did not consume output during drag')
        assert not d.contains('Terminal C ['), 'Created inserted during drag'
        d.wait(lambda: any('Terminal B [live]' in row and
                           row.find('┌') != original[0] for row in d.screen.display),
               'B did not move within its drag stack')
        d.send('\r')
        d.wait(lambda: d.contains('Terminal C [live]'), 'C did not insert after drag')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 3 live'), 'Quit count after drag wrong')
        d.confirm(True)
        restored = d.restore()
        return {'drag_defers_insertion': True, 'prepared_output_during_drag': True,
                'drag_movement_preserved': True, **restored}
    finally: d.close()

def dynamic_error_modal(binary, folder):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit, control = folder/'audit.json', folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': 'dynamic-held', 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'synthetic initial pair missing')
        d.menu('n'); d.menu('n')
        d.wait(lambda: sum(row['type'] == 3 for row in json.loads(audit.read_text())['requests']) == 4,
               'two pending Create requests were not sent')
        control.write_text('error')
        d.wait(lambda: d.contains('Cannot launch terminal shell'),
               'first Create Error did not open dialog')
        control.write_text('created')
        for _ in range(20): d.send('x')
        d.wait(lambda: any(row['type'] == 14 and row['session'] == 3
                           for row in json.loads(audit.read_text())['requests']),
               'second Created was not serviced inside Create-error dialog')
        assert d.contains('Cannot launch terminal shell') and not d.contains('Terminal D ['), \
            'second Created stole error-dialog focus'
        d.send('\r')
        d.wait(lambda: d.contains('Terminal D [live]') and d.contains('DYNAMIC_READY'),
               'prepared D did not insert after error dialog')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 3 live'), 'Quit count after Error wrong')
        d.confirm(True)
        restored = d.restore()
        return {'error_dialog_keeps_event_service': True,
                'prepared_adoption_after_dialog': True, **restored}
    finally: d.close()

def dynamic_unrelated_close_modal(binary, folder):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit, control = folder/'audit.json', folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': 'close-drag', 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'synthetic initial pair missing')
        b_left, _, _, b_bottom = d.bounds('B')
        d.menu('w')
        d.wait(lambda: d.contains('Terminate live terminal B'), 'B close confirmation missing')
        d.confirm(True)
        d.wait(lambda: d.contains('Terminal B [closing]'), 'B Close was not held')
        d.menu('\t')
        _, _, _, a_bottom = d.bounds('A')
        assert b_bottom - 1 >= a_bottom, 'B corner is not below A frame'
        d.menu('w')
        d.wait(lambda: d.contains('Terminate live terminal A'), 'A modal did not open')
        def b_corner(): return d.screen.display[b_bottom - 1][b_left]
        assert b_corner() == '└', 'B corner is not exposed beneath A modal'
        control.write_text('closed')
        for _ in range(12): d.send('x')
        d.wait(lambda: json.loads(audit.read_text())['released_close_count'] == 1,
               'B Closed was not released')
        d.wait(lambda: b_corner() == '░' and d.contains('Terminate live terminal A'),
               'B corner did not clear safely beneath A confirmation')
        d.confirm(False)
        d.wait(lambda: not d.contains('Terminal B ['), 'B did not retire after A modal')
        assert d.contains('Terminal A [live]'), 'A was lost after canceling confirmation'
        d.command('progress')
        d.wait(lambda: d.contains('A_PROGRESS'), 'A survivor stopped handling input')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 1 live'), 'survivor Quit count wrong')
        d.confirm(True)
        restored = d.restore()
        record = json.loads(audit.read_text())
        assert sum(row['type'] == 7 for row in record['requests']) == 1, \
            'B Close was issued more than once'
        return {'unrelated_modal_survives_close': True,
                'single_retirement': True, 'survivor_input': True, **restored}
    finally: d.close()

def dynamic_capacity(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'initial pair missing')
        for index in range(2, 16):
            label = chr(ord('A') + index)
            d.menu('n')
            d.wait(lambda: d.contains(f'Terminal {label} [live]'),
                   f'New Terminal did not create {label}')
        d.menu('n')
        d.wait(lambda: d.contains('Terminal capacity is 16 windows'),
               '17th New did not report the local view cap')
        d.send('\r')
        d.wait(lambda: not d.contains('Terminal capacity is 16 windows'),
               'capacity dialog did not dismiss')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 16 live'), 'capacity changed Quit live count')
        d.confirm(True)
        restored = d.restore()
        return {'bounded_sixteen_views': True, 'truthful_capacity': True, **restored}
    finally: d.close()

def dynamic_cycles(binary, folder):
    d = Desktop(binary, folder)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'initial pair missing')
        for label in ('B', 'A'):
            d.menu('w')
            d.wait(lambda: d.contains('Terminate live terminal'), 'live close did not confirm')
            d.confirm(True)
            d.wait(lambda: not d.contains(f'Terminal {label} ['), f'{label} did not retire')
        for label in ('C', 'D', 'E', 'F'):
            d.menu('n')
            d.wait(lambda: d.contains(f'Terminal {label} [live]'),
                   f'New after last close did not create {label}')
            d.menu('w')
            d.wait(lambda: d.contains('Terminate live terminal'), 'cycle close did not confirm')
            d.confirm(True)
            d.wait(lambda: not d.contains(f'Terminal {label} ['),
                   f'{label} did not retire before next cycle')
        d.menu('q')
        restored = d.restore()
        return {'close_last_create_again': True, 'finite_create_close_cycles': 4, **restored}
    finally: d.close()

def dynamic_pending_quit(binary, folder):
    package = folder/'package'
    package.mkdir(parents=True, exist_ok=True)
    frontend = package/'agentvision'
    shutil.copy2(binary, frontend)
    shutil.copy2(Path(__file__).with_name('desktop_core_fixture.py'), package/'agentvision-core')
    (package/'agentvision-core').chmod(0o700)
    audit, control = folder/'audit.json', folder/'control'
    d = Desktop(frontend.resolve(), folder/'outer', extra_env={
        'AV_DESKTOP_CASE': 'dynamic-held', 'AV_DESKTOP_AUDIT': str(audit.resolve()),
        'AV_DESKTOP_CONTROL': str(control.resolve())})
    def creates():
        return sum(row['type'] == 3 for row in json.loads(audit.read_text())['requests'])
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'synthetic initial pair missing')
        for label in ('B', 'A'):
            d.menu('w')
            d.wait(lambda: d.contains('Terminate live terminal'), 'live close confirmation missing')
            d.confirm(True)
            d.wait(lambda: not d.contains(f'Terminal {label} ['),
                   f'{label} did not retire before pending-only Quit')
        d.menu('n')
        d.wait(lambda: creates() == 3, 'pending C Create was not sent')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 1 live/starting'),
               'pending-only Quit did not count Create')
        d.confirm(False)
        control.write_text('created')
        d.wait(lambda: d.contains('Terminal C [live]'),
               'canceled Quit discarded pending Create')
        d.menu('w')
        d.wait(lambda: d.contains('Terminate live terminal'), 'C close confirmation missing')
        d.confirm(True)
        d.wait(lambda: not d.contains('Terminal C ['), 'C did not retire')
        d.menu('n')
        d.wait(lambda: creates() == 4, 'pending D Create was not sent')
        d.menu('q')
        d.wait(lambda: d.contains('Terminate 1 live/starting'),
               'second pending-only Quit did not count Create')
        d.confirm(True)
        restored = d.restore()
        record = json.loads(audit.read_text())
        assert record['created'] == 4 and not d.contains('Terminal D ['), \
            'late Created after confirmed Quit became a view'
        return {'pending_quit_cancel_preserves_create': True,
                'confirmed_quit_rejects_late_created': True, **restored}
    finally: d.close()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--output', type=Path, default=Path('.probe/desktop'))
    parser.add_argument('--cases', nargs='+', choices=['interaction', 'live-close-quit', 'quit-both', 'scrolling', 'dynamic-basic', 'dynamic-four-shells', 'dynamic-modal', 'dynamic-drag', 'dynamic-error-modal', 'dynamic-unrelated-close-modal', 'dynamic-capacity', 'dynamic-cycles', 'dynamic-pending-quit', 'loss-modal', 'loss-continuous', 'stopped-quit', 'startup-before', 'startup-after', 'startup-second', 'close-barrier', 'exit-then-loss', 'close-drag', 'close-grab', 'close-mouse', 'startup-final'])
    args = parser.parse_args()
    assert args.binary.is_file(), 'AgentVision executable is not implemented'
    cases = {'interaction': interaction, 'live-close-quit': live_close_quit,
             'quit-both': quit_both, 'scrolling': scrolling, 'dynamic-basic': dynamic_basic,
             'dynamic-four-shells': dynamic_four_shells,
             'dynamic-modal': dynamic_modal, 'dynamic-drag': dynamic_drag,
             'dynamic-error-modal': dynamic_error_modal,
             'dynamic-unrelated-close-modal': dynamic_unrelated_close_modal,
             'dynamic-capacity': dynamic_capacity,
             'dynamic-cycles': dynamic_cycles, 'dynamic-pending-quit': dynamic_pending_quit,
             'loss-modal': loss_modal,
             'loss-continuous': loss_continuous, 'stopped-quit': stopped_quit,
             'startup-before': startup_before, 'startup-after': startup_after,
             'startup-second': startup_second, 'close-barrier': close_barrier,
             'exit-then-loss': exit_then_loss, 'close-then-loss': close_then_loss,
             'close-drag': close_drag, 'close-grab': close_grab, 'close-mouse': close_mouse,
             'startup-final': startup_final}
    results = {name: cases[name](args.binary.resolve(), args.output/name)
               for name in (args.cases or list(cases))}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print(json.dumps(results, indent=2))
