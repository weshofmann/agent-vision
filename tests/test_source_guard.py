#!/usr/bin/env python3
"""R2: real Git fixtures; staged changes cannot evade the source guard.
Every rejection must preserve effective files, HEAD and staged entries.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(sys.argv.pop(1)).resolve()
ENV = {**os.environ, 'GIT_OPTIONAL_LOCKS': '0'}

def git(repo, *args):
    return subprocess.check_output(['git', '-C', str(repo), *args], env=ENV,
                                   stderr=subprocess.DEVNULL)

def init(repo):
    repo.mkdir(parents=True)
    git(repo, 'init', '-q')
    git(repo, 'config', 'user.name', 'Synthetic Fixture')
    git(repo, 'config', 'user.email', 'fixture@example.invalid')
    git(repo, 'config', 'commit.gpgsign', 'false')
    git(repo, 'config', 'core.hooksPath', '/dev/null')

class SourceGuard(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='agentvision-guard-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)/'source'
        init(self.root)
        self.dep = self.root/'deps/fixture'
        init(self.dep)
        self.nested = self.dep/'nested'
        init(self.nested)
        (self.nested/'nested.cc').write_text('nested baseline\n')
        git(self.nested, 'add', '.'); git(self.nested, 'commit', '-qm', 'baseline')
        (self.dep/'dependency.cc').write_text('dependency baseline\n')
        git(self.dep, 'add', '.'); git(self.dep, 'commit', '-qm', 'baseline')
        (self.root/'allowed.cc').write_text('before patch\n')
        (self.root/'source.cc').write_text('unrelated baseline\n')
        git(self.root, 'add', '.'); git(self.root, 'commit', '-qm', 'pinned fixture')
        (self.root/'allowed.cc').write_text('accepted patch\n')
        self.patch = Path(self.tmp.name)/'accepted.patch'
        self.patch.write_bytes(git(self.root, 'diff', '--binary', '--no-color', '--no-ext-diff', 'HEAD'))
        (self.root/'allowed.cc').write_text('before patch\n')

    def applied(self): (self.root/'allowed.cc').write_text('accepted patch\n')
    def snapshot(self):
        # Compare index meaning, not Git's optional stat-cache metadata.
        return tuple((git(repo, 'rev-parse', 'HEAD'), git(repo, 'ls-files', '--stage', '-z'), git(repo, 'ls-files', '-v', '-z'),
                      git(repo, 'diff', '--binary', '--no-ext-diff', 'HEAD'),
                      tuple((p.relative_to(repo).as_posix(), p.read_bytes())
                            for p in sorted(repo.rglob('*'))
                            if p.is_file() and '.git' not in p.relative_to(repo).parts))
                     for repo in (self.root, self.dep, self.nested))
    def run_guard(self):
        return subprocess.run([sys.executable, str(HELPER), str(self.root), str(self.patch), *map(str, getattr(self, "extra", []))],
                              env=ENV, capture_output=True, timeout=10)
    def rejected_unchanged(self):
        before = self.snapshot()
        # Snapshot's recursive Git diff can refresh stat cache even with optional
        # locks disabled. Compare raw bytes immediately around the guard itself,
        # before any second diagnostic reader can refresh that cache.
        indexes = [repo/'.git/index' for repo in (self.root, self.dep, self.nested)]
        raw_before = [index.read_bytes() for index in indexes]
        result = self.run_guard()
        self.assertEqual(raw_before, [index.read_bytes() for index in indexes],
                         'guard mutated raw index bytes')
        self.assertNotEqual(result.returncode, 0, 'guard accepted modified tracked source')
        self.assertEqual(before, self.snapshot(), 'rejected fixture was mutated')

    def test_clean_pinned_source_applies_accepted_patch(self):
        self.assertEqual(self.run_guard().returncode, 0)
        self.assertEqual((self.root/'allowed.cc').read_text(), 'accepted patch\n')
        self.assertEqual(git(self.root, 'diff', '--binary', '--no-ext-diff', 'HEAD'), self.patch.read_bytes())
    def test_exact_already_applied_patch_is_unchanged(self):
        self.applied(); before = self.snapshot()
        self.assertEqual(self.run_guard().returncode, 0)
        self.assertEqual(before, self.snapshot())
    def test_exact_staged_patch_is_effectively_the_same_source(self):
        self.applied(); git(self.root, 'add', 'allowed.cc'); before = self.snapshot()
        self.assertEqual(self.run_guard().returncode, 0)
        self.assertEqual(before, self.snapshot())
    def test_unrelated_staged_source_edit_rejected_unchanged(self):
        self.applied(); (self.root/'source.cc').write_text('unexpected staged edit\n')
        git(self.root, 'add', 'source.cc'); self.rejected_unchanged()
    def test_unrelated_unstaged_source_edit_rejected_before_apply(self):
        (self.root/'source.cc').write_text('unexpected unstaged edit\n')
        self.rejected_unchanged()
    def test_dependency_staged_edit_rejected_before_apply(self):
        (self.dep/'dependency.cc').write_text('dependency staged edit\n')
        git(self.dep, 'add', 'dependency.cc'); self.rejected_unchanged()
    def test_dependency_unstaged_edit_rejected_before_apply(self):
        (self.dep/'dependency.cc').write_text('dependency unstaged edit\n')
        self.rejected_unchanged()
    def test_dependency_edit_cannot_hide_behind_git_ignore_config(self):
        git(self.root, 'config', 'diff.ignoreSubmodules', 'all')
        (self.dep/'dependency.cc').write_text('ignored dependency edit\n')
        self.rejected_unchanged()
    def test_dependency_head_mismatch_rejected_unchanged(self):
        (self.dep/'dependency.cc').write_text('different dependency revision\n')
        git(self.dep, 'add', 'dependency.cc'); git(self.dep, 'commit', '-qm', 'different')
        self.rejected_unchanged()
    def composition(self):
        (self.root/'source.cc').write_text('second accepted patch\n')
        second = Path(self.tmp.name)/'second.patch'
        second.write_bytes(git(self.root, 'diff', '--binary', '--no-color', '--no-ext-diff', 'HEAD', '--', 'source.cc'))
        (self.root/'source.cc').write_text('unrelated baseline\n')
        self.extra = [second]
    def test_composed_pristine_and_already_applied(self):
        self.composition()
        self.assertEqual(self.run_guard().returncode, 0)
        self.assertEqual((self.root/'source.cc').read_text(), 'second accepted patch\n')
        before=self.snapshot(); self.assertEqual(self.run_guard().returncode,0)
        self.assertEqual(before,self.snapshot())
    def test_composed_staged(self):
        self.composition(); self.applied(); (self.root/'source.cc').write_text('second accepted patch\n')
        git(self.root,'add','.'); before=self.snapshot()
        self.assertEqual(self.run_guard().returncode,0); self.assertEqual(before,self.snapshot())
    def test_partial_composition_rejected(self):
        self.composition(); self.applied(); self.rejected_unchanged()
    def test_conflicting_composition_rejected(self):
        self.extra=[self.patch]; self.rejected_unchanged()
    def test_composed_hidden_flags_rejected(self):
        self.composition(); self.flagged_rejected(self.root,'source.cc','--skip-worktree')
    def test_composed_gitlink_edit_rejected(self):
        self.composition(); (self.dep/'dependency.cc').write_text('unexpected\n')
        git(self.dep,'add','.'); self.rejected_unchanged()

    def flagged_rejected(self, repo, name, flag, changed=True):
        # Before application: a rejection must not add even the accepted patch.
        git(repo, 'update-index', flag, name)
        if changed: (repo/name).write_text('hidden effective edit\n')
        self.rejected_unchanged()

    def test_root_assume_unchanged_edit_rejected_unchanged(self):
        self.flagged_rejected(self.root, 'source.cc', '--assume-unchanged')
    def test_root_skip_worktree_edit_rejected_unchanged(self):
        self.flagged_rejected(self.root, 'source.cc', '--skip-worktree')
    def test_dependency_assume_unchanged_edit_rejected_unchanged(self):
        self.flagged_rejected(self.dep, 'dependency.cc', '--assume-unchanged')
    def test_dependency_skip_worktree_edit_rejected_unchanged(self):
        self.flagged_rejected(self.dep, 'dependency.cc', '--skip-worktree')
    def test_nested_assume_unchanged_edit_rejected_unchanged(self):
        self.flagged_rejected(self.nested, 'nested.cc', '--assume-unchanged')
    def test_nested_skip_worktree_edit_rejected_unchanged(self):
        self.flagged_rejected(self.nested, 'nested.cc', '--skip-worktree')
    def test_clean_assume_unchanged_entry_refused_unchanged(self):
        self.flagged_rejected(self.root, 'source.cc', '--assume-unchanged', changed=False)
    def test_clean_skip_worktree_entry_refused_unchanged(self):
        self.flagged_rejected(self.root, 'source.cc', '--skip-worktree', changed=False)
    def test_nested_dependency_tracked_edit_rejected_before_apply(self):
        (self.nested/'nested.cc').write_text('nested dependency edit\n')
        self.rejected_unchanged()

if __name__ == '__main__': unittest.main()
