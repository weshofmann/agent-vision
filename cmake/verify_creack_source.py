#!/usr/bin/env python3
"""Validate all effective replacement files against pinned pristine + patch.
Read-only to retained files. No download or module-cache mutation is performed.
"""
import hashlib, json, pathlib, shutil, subprocess, sys, tempfile

def hashes(root):
 result={}
 for p in root.rglob('*'):
  if p.is_symlink():raise ValueError('replacement symlink rejected')
  if p.is_file():result[p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
 return result

def verify(root):
 source=root/'core/third_party/creack-pty'
 pristine=json.loads((root/'core/third_party/creack-pty.pristine.json').read_text())
 downstream=json.loads((root/'core/third_party/creack-pty.downstream.json').read_text())
 if pristine['commit']!='edfbf75025b0ba4ee17c19f52d9b600fad80a787' or pristine['version']!='v1.1.24':raise ValueError('wrong upstream pin')
 patch=root/'patches/creack-pty-darwin-master-boundary.patch'
 if hashlib.sha256(patch.read_bytes()).hexdigest()!=downstream['patch_sha256']:raise ValueError('patch digest mismatch')
 actual=hashes(source)
 expected=dict(pristine['files'])
 expected.update(downstream['changed_files'])
 for name in downstream['removed_files']:del expected[name]
 if actual!=expected:raise ValueError('effective replacement file set/digests mismatch')
 with tempfile.TemporaryDirectory(prefix='av-creack-guard-') as d:
  temp=pathlib.Path(d);tree=temp/'tree';shutil.copytree(source,tree)
  def apply(reverse=False):
   args=['git','apply','--whitespace=nowarn']
   if reverse:args.append('--reverse')
   subprocess.run([*args,str(patch.resolve())],cwd=tree,check=True,capture_output=True)
  apply(True)
  if hashes(tree)!=pristine['files']:raise ValueError('reverse patch does not reproduce complete pristine source')
  apply()
  if hashes(tree)!=actual:raise ValueError('forward patch does not reproduce effective source')
 print('PASS: complete creack/pty v1.1.24 pristine + reviewed Darwin patch')

if __name__=='__main__':
 try:verify(pathlib.Path(sys.argv[1]).resolve())
 except (ValueError,OSError,KeyError,subprocess.CalledProcessError) as e:
  print('creack source guard rejected: '+str(e),file=sys.stderr);sys.exit(1)
