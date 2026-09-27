#!/usr/bin/env python3
"""Mutations must fail before a stale replacement can be built."""
import pathlib, shutil, subprocess, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Guard(unittest.TestCase):
 def test_effective_source_set(self):
  with tempfile.TemporaryDirectory() as d:
   root=pathlib.Path(d)
   shutil.copytree(ROOT/'core/third_party',root/'core/third_party')
   (root/'patches').mkdir()
   shutil.copy(ROOT/'patches/creack-pty-darwin-master-boundary.patch',root/'patches')
   def run():return subprocess.run([sys.executable,str(ROOT/'cmake/verify_creack_source.py'),str(root)],capture_output=True)
   self.assertEqual(run().returncode,0)
   source=root/'core/third_party/creack-pty'
   for name,mutation in [('unexpected.go',lambda p:p.write_text('package pty\n')),('.hidden',lambda p:p.write_text('unexpected')),('pty_darwin.go',lambda p:p.write_text(p.read_text()+'\n// change\n')),('go.mod',lambda p:p.write_text(p.read_text()+'\n// change\n')),('LICENSE',lambda p:p.unlink())]:
    with self.subTest(name=name):
     p=source/name;before=p.read_bytes() if p.exists() else None;mutation(p)
     self.assertNotEqual(run().returncode,0)
     if before is None:p.unlink()
     else:p.write_bytes(before)
   (source/'unexpected-link').symlink_to(source/'go.mod');self.assertNotEqual(run().returncode,0)
if __name__=='__main__':unittest.main()
