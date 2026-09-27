#!/usr/bin/env python3
"""Run the qualified Go test child once and retain its actual -work executables.
Caller supplies durable ignored evidence storage and owns the outer watchdog.
"""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tarfile
import time

ENV_KEYS=('GOTOOLCHAIN','GOENV','GOWORK','GOFLAGS','GOOS','GOARCH','CGO_ENABLED',
          'GOCACHE','GOMODCACHE','GOTMPDIR','AGENTVISION_DARWIN_COMPARISON',
          'GODEBUG','GOGC')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(go, source, evidence, selection):
    # Every invocation owns a fresh location; prior passed/failed evidence stays.
    invocation=evidence/('run-'+str(time.time_ns()))
    invocation.mkdir(parents=True)
    temp=invocation/'tmp'
    temp.mkdir()
    environment={**os.environ,'GOTMPDIR':str(temp)}
    command=[go,'test','-mod=readonly','-race','-work','-count=1',*selection]
    files=sorted(p for p in (source/'core').rglob('*') if p.is_file())
    for relative in ('patches/creack-pty-darwin-master-boundary.patch',
                     'cmake/GoCore.cmake','cmake/verify_creack_source.py',
                     'tests/retained_go_test.py'):
        path=source/relative
        if path.is_file():files.append(path)
    identities={p.relative_to(source).as_posix():digest(p) for p in files}
    (invocation/'source-identities.json').write_text(json.dumps(identities,indent=2)+'\n')
    with tarfile.open(invocation/'source.tar.gz','w:gz') as archive:
        for path in files:archive.add(path,arcname=path.relative_to(source).as_posix())
    head=subprocess.run(['git','-C',str(source),'rev-parse','HEAD'],capture_output=True,text=True)
    version=subprocess.run([go,'version'],capture_output=True,text=True,env=environment)
    result={'command':command,'cwd':str(source/'core'),
            'head':head.stdout.strip() if head.returncode==0 else None,
            'environment':{k:environment[k] for k in ENV_KEYS if k in environment},
            'go_sha256':digest(pathlib.Path(go)),
            'go_version':version.stdout.strip(),
            'source_identities_sha256':digest(invocation/'source-identities.json'),
            'source_archive_sha256':digest(invocation/'source.tar.gz'),
            'work':None,'test_binaries_sha256':{}}
    (invocation/'started.json').write_text(json.dumps(result,indent=2)+'\n')
    work=None
    retention_error=None
    started=time.monotonic()
    print('Retained Go test evidence: '+str(invocation),flush=True)
    with (invocation/'output.txt').open('wb') as log:
        child=subprocess.Popen(command,cwd=source/'core',env=environment,
                               stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in child.stdout:
            log.write(line)
            log.flush()
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()
            if line.startswith(b'WORK='):
                observed=pathlib.Path(line[5:].decode().strip()).resolve()
                if not observed.is_relative_to(temp):
                    retention_error='Go WORK escaped the invocation GOTMPDIR'
                else:
                    work=observed
                    result['work']=str(work)
                    (invocation/'started.json').write_text(json.dumps(result,indent=2)+'\n')
        child.stdout.close()
        result['exit']=child.wait()
    result['seconds']=time.monotonic()-started
    result['output_sha256']=digest(invocation/'output.txt')
    if work is None:retention_error=retention_error or 'Go did not report a retained WORK directory'
    else:
        result['test_binaries_sha256']={p.relative_to(work).as_posix():digest(p)
                                       for p in sorted(work.rglob('*.test')) if p.is_file()}
        if result['exit']==0 and not result['test_binaries_sha256']:
            retention_error=retention_error or 'successful qualification retained no test executable'
    result['retention_error']=retention_error
    (invocation/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Retained result: '+str(invocation/'result.json'),flush=True)
    return result['exit'] if result['exit']!=0 else (1 if retention_error else 0)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--go',required=True)
    parser.add_argument('--source-root',required=True,type=pathlib.Path)
    parser.add_argument('--evidence-root',required=True,type=pathlib.Path)
    parser.add_argument('selection',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    selection=args.selection[1:] if args.selection[:1]==['--'] else args.selection
    sys.exit(run(str(pathlib.Path(args.go).resolve()),args.source_root.resolve(),
                 args.evidence_root.resolve(),selection))
