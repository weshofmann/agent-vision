#!/usr/bin/env python3
"""Exercise the opt-in configure contract without fetching C++ dependencies."""
import argparse
import pathlib
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('cmake')
    parser.add_argument('go')
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='av-core-config-') as temp:
        source = pathlib.Path(temp) / 'source'
        source.mkdir()
        shutil.copytree(root / 'core', source / 'core')
        (source / 'cmake').mkdir()
        shutil.copy(root / 'cmake/GoCore.cmake', source / 'cmake/GoCore.cmake')
        shutil.copy(root / 'cmake/verify_creack_source.py', source / 'cmake/verify_creack_source.py')
        (source / 'patches').mkdir()
        shutil.copy(root / 'patches/creack-pty-darwin-master-boundary.patch', source / 'patches')
        (source / 'third_party').mkdir()
        shutil.copytree(root / 'third_party/notices', source / 'third_party/notices')
        (source / '.probe').mkdir()
        for cache in ('go-cache', 'go-modcache'):
            (source / '.probe' / cache).symlink_to(root / '.probe' / cache, target_is_directory=True)
        (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(CoreConfig NONE)\ninclude(cmake/GoCore.cmake)\nagentvision_configure_core()\n')
        def configure(name, flags, success, reason=''):
            build = pathlib.Path(temp) / name
            result = subprocess.run([args.cmake, '-S', str(source), '-B', str(build), *flags], capture_output=True, text=True)
            assert (result.returncode == 0) == success, name
            assert not reason or reason in result.stdout + result.stderr, name
            return build
        off = configure('off', [], True)
        assert 'agentvision-core' not in (off / 'Makefile').read_text()
        configure('missing', ['-DAGENTVISION_BUILD_CORE=ON'], False, 'absolute')
        configure('relative', ['-DAGENTVISION_BUILD_CORE=ON', '-DAGENTVISION_GO_EXECUTABLE=go'], False, 'absolute')
        configure('target', ['-DAGENTVISION_BUILD_CORE=ON', '-DCMAKE_SYSTEM_NAME=Linux', '-DCMAKE_SYSTEM_PROCESSOR=x86_64', '-DAGENTVISION_GO_EXECUTABLE=' + args.go], False, 'Darwin arm64')
        fake = source / 'wrong-go'
        fake.write_text('#!/bin/sh\necho "go version go1.26.0 darwin/arm64"\n')
        fake.chmod(0o755)
        configure('version', ['-DAGENTVISION_BUILD_CORE=ON', '-DAGENTVISION_GO_EXECUTABLE=' + str(fake)], False, '1.27.0')
        build = configure('on', ['-DAGENTVISION_BUILD_CORE=ON', '-DAGENTVISION_GO_EXECUTABLE=' + args.go], True)
        subprocess.run([args.cmake, '--build', str(build)], check=True, capture_output=True)
        binary = build / 'agentvision-core'
        before = binary.stat().st_mtime_ns
        time.sleep(1.05)
        target = source / 'core/cmd/agentvision-core/main.go'
        target.write_text(target.read_text().replace('invalid startup arguments', 'synthetic rebuild probe'))
        subprocess.run([args.cmake, '--build', str(build)], check=True, capture_output=True)
        assert binary.stat().st_mtime_ns > before, 'Go source edit did not rebuild sibling'
        # Reviewed effective bytes stay constant: touching each guarded dependency
        # proves its rebuild edge without fabricating an accepted source mutation.
        for relative in ('core/third_party/creack-pty/pty_darwin.go',
                         'core/third_party/creack-pty/go.mod',
                         'core/third_party/creack-pty.pristine.json',
                         'core/third_party/creack-pty.downstream.json',
                         'patches/creack-pty-darwin-master-boundary.patch',
                         'cmake/verify_creack_source.py'):
            before = binary.stat().st_mtime_ns
            time.sleep(1.05)
            (source / relative).touch()
            subprocess.run([args.cmake, '--build', str(build)], check=True, capture_output=True)
            assert binary.stat().st_mtime_ns > before, relative + ' did not rebuild'
        for relative in ('core/third_party/creack-pty/unexpected.go',
                         'core/third_party/creack-pty/.unexpected'):
            unexpected = source / relative
            unexpected.write_text('package pty\n')
            before = binary.stat().st_mtime_ns
            result = subprocess.run([args.cmake, '--build', str(build)], capture_output=True)
            assert result.returncode != 0, 'unexpected replacement addition accepted'
            assert binary.stat().st_mtime_ns == before, 'built before integrity rejection'
            unexpected.unlink()
        assert (build / 'licenses/creack-pty.LICENSE').read_bytes() == (root / 'third_party/notices/creack-pty.LICENSE').read_bytes()
        print('PASS: opt-in/off, absolute pinned Go, target rejection, dependency rebuild, notices')

if __name__ == '__main__':
    main()
