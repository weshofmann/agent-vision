#!/usr/bin/env python3
"""Validate effective pinned source before applying; never reset existing edits."""
from pathlib import Path
import subprocess
import sys

source, patch = (Path(arg).resolve() for arg in sys.argv[1:])
accepted = patch.read_bytes()

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args])

def effective_diff(repo):
    # diff trusts these index bits even if compiler-visible bytes changed.
    # Refuse flagged checkouts conservatively; never clear a user's flags/index.
    for entry in git(repo, 'ls-files', '-v', '-z').split(b'\0'):
        if entry and (entry[:1] == b'S' or entry[:1].islower()):
            sys.exit('Tracked assume-unchanged/skip-worktree entries prevent validation; '
                     'use a fully materialized checkout without these flags; source left unchanged')
    # HEAD is the baseline, not the index. Force dependency visibility and avoid
    # configured external/textconv output masking the source that will compile.
    return git(repo, 'diff', '--binary', '--no-color', '--no-ext-diff',
               '--no-textconv', '--ignore-submodules=none', 'HEAD')

def dependencies(repo):
    # Read gitlinks from the pinned commit, not from a potentially edited index.
    for record in git(repo, 'ls-tree', '-rz', 'HEAD').split(b'\0'):
        if not record: continue
        metadata, path = record.split(b'\t', 1)
        mode, _, expected = metadata.split()
        if mode != b'160000': continue
        child = repo / path.decode('utf-8', 'surrogateescape')
        top = Path(git(child, 'rev-parse', '--show-toplevel').decode().strip()).resolve()
        if top != child.resolve() or git(child, 'rev-parse', 'HEAD').strip() != expected:
            sys.exit('Dependency is uninitialized or differs from the pinned gitlink')
        if effective_diff(child):
            sys.exit('Unexpected tracked dependency edits; source left unchanged')
        dependencies(child)

dependencies(source)
actual = effective_diff(source)
if actual not in (b'', accepted):
    sys.exit('Unexpected tracked upstream edits; source left unchanged')

base = ['git', '-C', str(source), 'apply']
# Validate before applying, so rejection cannot add even the accepted patch.
check = base + (['--reverse'] if actual else []) + ['--check', str(patch)]
if subprocess.run(check, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
    sys.exit('Downstream patch does not match pinned source; source left unchanged')
if not actual:
    subprocess.run(base + [str(patch)], check=True)
if effective_diff(source) != accepted:
    sys.exit('Applied source does not equal the exact accepted patch')
