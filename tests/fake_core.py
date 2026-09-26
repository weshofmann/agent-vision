#!/usr/bin/python3
# Synthetic native exec fixture; no environment or terminal data is emitted.
import os, socket, sys
if sys.argv[1:] != ['--ipc-fd=3', '--mode=frontend-spawned']:
    sys.exit(91)
s = socket.socket(fileno=3)
s.sendall(b'P')
s.close()
sys.exit(23)
