#!/usr/bin/env python3
"""No Go/native execution: a fake child proves retention on success and failure."""
import hashlib, json, os, pathlib, subprocess, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
HELPER=ROOT/'tests/retained_go_test.py'
class Retention(unittest.TestCase):
 def test_actual_work_binary_retained_on_success_and_failure(self):
  for code in (0,1):
   with self.subTest(code=code),tempfile.TemporaryDirectory() as d:
    source=pathlib.Path(d).resolve()/'source';(source/'core').mkdir(parents=True)
    (source/'core/go.mod').write_text('module synthetic.fixture\n')
    fake=source/'fake-go'
    fake.write_text('#!'+sys.executable+'\n'+'''import json,os,pathlib,sys,tempfile
if sys.argv[1:]==['version']:
 print('go version go1.27.0 darwin/arm64');sys.exit(0)
args=sys.argv[1:]
assert args==['test','-mod=readonly','-race','-work','-count=1','-v','./...','-timeout=120s'],args
base=pathlib.Path(os.environ['GOTMPDIR']);assert base.is_dir()
work=pathlib.Path(tempfile.mkdtemp(prefix='go-build',dir=base))
print('WORK='+str(work),file=sys.stderr,flush=True)
(work/'b001').mkdir();(work/'b001/session.test').write_bytes(b'actual synthetic executed binary')
print('synthetic qualification output',flush=True)
sys.exit(int(os.environ['SYNTHETIC_EXIT']))
''');fake.chmod(0o755)
    evidence=source/'.probe/go-test-evidence'
    run=subprocess.run([sys.executable,str(HELPER),'--go',str(fake),'--source-root',str(source),'--evidence-root',str(evidence),'--','-v','./...','-timeout=120s'],env={**os.environ,'SYNTHETIC_EXIT':str(code),'GOTOOLCHAIN':'local','GOENV':'off','GOWORK':'off','CGO_ENABLED':'1'},capture_output=True)
    self.assertEqual(run.returncode,code,run.stdout+run.stderr)
    runs=list(evidence.iterdir());self.assertEqual(len(runs),1)
    result=json.loads((runs[0]/'result.json').read_text())
    self.assertEqual(result['exit'],code)
    self.assertEqual(result['command'][0],str(fake))
    self.assertEqual(result['environment']['GOTMPDIR'],str(runs[0]/'tmp'))
    work=pathlib.Path(result['work']);self.assertTrue(work.is_relative_to(runs[0]/'tmp'))
    self.assertEqual((work/'b001/session.test').read_bytes(),b'actual synthetic executed binary')
    self.assertEqual(result['test_binaries_sha256']['b001/session.test'],hashlib.sha256(b'actual synthetic executed binary').hexdigest())
    self.assertIn('WORK=',(runs[0]/'output.txt').read_text())
    self.assertIn('synthetic qualification output',(runs[0]/'output.txt').read_text())
    self.assertTrue((runs[0]/'source-identities.json').exists())
    self.assertTrue((runs[0]/'source.tar.gz').exists())
if __name__=='__main__':unittest.main()
