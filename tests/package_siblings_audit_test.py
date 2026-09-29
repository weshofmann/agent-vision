#!/usr/bin/env python3
"""An old successful peer audit cannot qualify a new peer that dies before replying."""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

import package_siblings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('frontend', type=Path)
    parser.add_argument('core', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='rerun-', dir=args.output.resolve()))
    package = folder/'package with spaces'
    binary_dir = package/'bin'
    binary_dir.mkdir(parents=True)
    shutil.copy2(args.frontend, binary_dir/'agentvision')
    shutil.copy2(args.core, binary_dir/'agentvision-core')
    case = folder/'version'
    env = {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin'}

    first = package_siblings.rejection(package, case, env, 'version')
    assert first['owned_peer_reaped']
    stale = json.loads((case/'peer-pid.txt').read_text())
    assert stale['hello_checked'] and stale['reply_sent'] and stale['mode'] == 'version'

    marker = case/'faulting-peer-executed'
    def fail_before_audit(path, mode, audit):
        assert mode == 'version' and audit == case/'peer-pid.txt'
        path.write_text('#!'+sys.executable+'\n'
                        'from pathlib import Path\n'
                        f'Path({str(marker)!r}).touch()\n'
                        'raise SystemExit(67)\n')
        path.chmod(0o755)
        # The exact current fixture is retained independently of the sibling
        # bytes restored by rejection(), and record_case hashes it prelaunch.
        (case/'faulting-peer-source.py').write_bytes(path.read_bytes())

    prior = package_siblings.handshake_peer
    package_siblings.handshake_peer = fail_before_audit
    try:
        try:
            package_siblings.rejection(package, case, env, 'version')
        except AssertionError as failure:
            assert 'current adjacent peer audit was not recorded' in str(failure), str(failure)
        else:
            raise AssertionError('stale peer audit was accepted for a current peer that sent no reply')
    finally:
        package_siblings.handshake_peer = prior
    assert marker.is_file(), 'faulting peer was not executed'
    assert not (case/'peer-pid.txt').exists(), 'old audit survived the new run'
    print('PASS: prior valid audit rejected after current peer exits before writing')


if __name__ == '__main__':
    main()
