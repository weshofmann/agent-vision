#!/usr/bin/env python3
"""Install and relocate the shipping siblings on the qualified Darwin host.
Synthetic handshake peers are used only for protocol rejection; all positive and
stopped-core cases execute the installed real Go core. Raw evidence stays private.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from desktop_pty import Desktop, ownership, stopped_quit

NOTICES = ('tvterm.COPYRIGHT', 'tvision.COPYRIGHT', 'libvterm.LICENSE',
           'creack-pty.LICENSE', 'Go.LICENSE', 'Go.PATENTS')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record_case(package, folder, env):
    folder.mkdir(parents=True, exist_ok=True)
    (folder/'inputs.json').write_text(json.dumps({
        'argv': [str(package/'bin/agentvision')], 'cwd': '/tmp',
        'env_overrides': env,
        'executables': {str(p): digest(p) for p in (package/'bin').iterdir()
                        if p.is_file()},
    }, indent=2)+'\n')


def positive(package, folder, env):
    record_case(package, folder, env)
    d = Desktop(package/'bin/agentvision', folder, extra_env=env)
    try:
        d.wait(lambda: d.contains('Terminal A [live]') and d.contains('Terminal B [live]'),
               'installed frontend did not create two real sessions')
        core, shells = ownership(d)
        d.command("label=B; cd /; printf 'PACKAGE_%s_%s\\n' \"$label\" \"$PWD\"")
        d.wait(lambda: d.contains('PACKAGE_B_/'), 'installed B input/state missing')
        d.menu('\t')
        d.command("label=A; cd /tmp; printf 'PACKAGE_%s_%s\\n' \"$label\" \"$PWD\"")
        d.wait(lambda: d.contains('PACKAGE_A_/tmp'), 'installed A input/state missing')
        d.menu('\t')
        d.command("printf 'SURVIVOR_%s_%s\\n' \"$label\" \"$PWD\"")
        d.wait(lambda: d.contains('SURVIVOR_B_/'), 'installed sessions shared shell state')
        d.menu('q'); d.wait(lambda: d.contains('Terminate 2 live'), 'quit confirmation missing')
        d.confirm(True)
        restored = d.restore()
        assert d.absent(core) and all(d.absent(pid) for pid in shells), 'owned package processes leaked'
        assert not (folder.parent/'path-executed').exists(), 'PATH core impostor executed'
        return {'two_sessions_independent': True, 'owned_core_and_shells_absent': True, **restored}
    finally:
        d.close()


def handshake_peer(path, mode, audit):
    # Literal wire fields independent of the implementation codec. Both peers
    # stay alive after rejection, so cleanup must reap the owned child.
    path.write_text('#!'+sys.executable+'\n'+f"""import json, os, pathlib, socket, struct, time
pathlib.Path({str(audit)!r}).write_text(str(os.getpid()))
peer=socket.socket(fileno=3)
def exact(n):
 data=b''
 while len(data)<n:
  chunk=peer.recv(n-len(data))
  if not chunk: raise EOFError
  data+=chunk
 return data
header=exact(32)
magic,version,kind,flags,reserved,count,request,session=struct.unpack('>4sHHHHIQQ',header)
assert (magic,version,kind,flags,reserved,count,session)==(b'AVCP',0,1,0,0,4,0)
assert exact(count)==b'\\x00\\x01\\x00\\x01'
body=struct.pack('>HII',2,65536,0)+bytes(16) if {mode!r}=='version' else b'\\x00'
peer.sendall(struct.pack('>4sHHHHIQQ',b'AVCP',0,2,0,0,len(body),request,0)+body)
pathlib.Path({str(audit)!r}).write_text(json.dumps(dict(pid=os.getpid(), hello_checked=True, reply_sent=True, mode={mode!r})))
while True: time.sleep(1)
""")
    path.chmod(0o755)


def rejection(package, folder, env, mode):
    sibling = package/'bin/agentvision-core'
    audit = folder/'peer-pid.txt'
    folder.mkdir(parents=True, exist_ok=True)
    saved = sibling.read_bytes()
    try:
        if mode == 'missing': sibling.unlink()
        elif mode == 'nonexecutable': sibling.chmod(0o600)
        else: handshake_peer(sibling, mode, audit)
        record_case(package, folder, env)
        d = Desktop(package/'bin/agentvision', folder, extra_env=env)
        try:
            result = d.restore(expected_exit=1)
            assert d.raw.count(b'Go core ') == 1, 'expected exactly one startup diagnostic'
            assert b'Terminal A [live]' not in d.raw and b'Terminal B [live]' not in d.raw
            assert not (folder.parent/'path-executed').exists(), 'PATH core impostor executed'
            if mode in ('version', 'malformed'):
                assert audit.exists(), 'adjacent peer was not actually executed'
                peer = json.loads(audit.read_text())
                assert peer['hello_checked'] and peer['reply_sent'] and peer['mode'] == mode, 'intended rejection frame was not sent'
                assert d.absent(peer['pid']), 'owned rejected peer was not reaped'
            return {'one_startup_diagnostic': True, 'no_path_fallback': True,
                    'owned_peer_reaped': True if mode in ('version', 'malformed') else None, **result}
        finally: d.close()
    finally:
        sibling.write_bytes(saved)
        sibling.chmod(0o755)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('cmake')
    parser.add_argument('build', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Fail before executing anything if the install rules do not ship siblings.
    installed = output/'installed'
    shutil.rmtree(installed, ignore_errors=True)
    subprocess.run([args.cmake, '--install', str(args.build.resolve()), '--prefix', str(installed)], check=True)
    for name in ('agentvision', 'agentvision-core'):
        binary = installed/'bin'/name
        assert binary.is_file() and os.access(binary, os.X_OK), 'CMake install omitted executable sibling '+name
    licenses = installed/'share/agentvision/licenses'
    for name in NOTICES:
        assert (licenses/name).read_bytes() == (root/'third_party/notices'/name).read_bytes(), 'incomplete notice '+name
    assert (licenses/'THIRD_PARTY_NOTICES.md').read_bytes() == (root/'THIRD_PARTY_NOTICES.md').read_bytes()
    (output/'installed-identities.json').write_text(json.dumps({str(p.relative_to(installed)): digest(p)
        for p in installed.rglob('*') if p.is_file()}, indent=2)+'\n')
    impostor = output/'path impostor'
    impostor.mkdir(exist_ok=True)
    marker = output/'path-executed'
    marker.unlink(missing_ok=True)
    fake = impostor/'agentvision-core'
    fake.write_text('#!'+sys.executable+'\nimport pathlib\npathlib.Path('+repr(str(marker))+').touch()\n')
    fake.chmod(0o755)
    env = {'PATH': str(impostor)+':/usr/bin:/bin:/usr/sbin:/sbin'}
    results = {'installed': positive(installed, output/'installed-check', env)}
    relocated = output/'relocated package with spaces'
    shutil.rmtree(relocated, ignore_errors=True)
    shutil.move(str(installed), relocated)
    results['relocated'] = positive(relocated, output/'relocated-check', env)
    for mode in ('missing', 'nonexecutable', 'version', 'malformed'):
        results[mode] = rejection(relocated, output/mode, env, mode)
    record_case(relocated, output/'stopped', env)
    results['stopped'] = stopped_quit(relocated/'bin/agentvision', output/'stopped', extra_env=env)
    assert not marker.exists(), 'PATH core impostor executed'
    (output/'summary.json').write_text(json.dumps(results, indent=2)+'\n')
    print('PASS: CMake installed/relocated shipping siblings, complete notices, independent sessions, failure matrix, restoration/owned cleanup')


if __name__ == '__main__':
    main()
