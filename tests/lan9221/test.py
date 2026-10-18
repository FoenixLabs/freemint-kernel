#!/usr/bin/env python3
"""Compile and run the actual LAN9221 core against emulated hardware."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    binary = str(Path(tmp)/'lan9221')
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-DLAN9221_HOST_TEST',
        '-I'+str(root/'sys/sockets/xif/lan9221'),
        str(root/'tests/lan9221/core.c'),
        str(root/'sys/sockets/xif/lan9221/hw.c'),
        '-o',binary],check=True)
    subprocess.run([binary],check=True)
