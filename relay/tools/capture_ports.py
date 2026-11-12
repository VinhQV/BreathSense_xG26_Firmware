"""Capture both kits without changing firmware; optionally reset relay first."""
import argparse
import subprocess
import threading
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=30)
parser.add_argument('--prefix', default='capture')
parser.add_argument('--reset', choices=['node', 'relay', 'both'])
args = parser.parse_args()

def listen(port):
    try:
        with serial.Serial(port, 115200, timeout=1) as p:
            with Path(f'{args.prefix}-{port}.log').open('w', encoding='utf-8') as f:
                end = time.monotonic() + args.seconds
                while time.monotonic() < end:
                    line = p.readline().decode(errors='replace')
                    if line:
                        print(f'{port}: {line}', end='', flush=True)
                        f.write(line)
                        f.flush()
    except Exception as error:
        print(port, error, flush=True)

threads = [threading.Thread(target=listen, args=(port,)) for port in ('COM14', 'COM5')]
for thread in threads:
    thread.start()
for name, serial_number in [('node', '440345913'), ('relay', '440364792')]:
    if args.reset in (name, 'both'):
        subprocess.run(['C:/SiliconLabs/Commander/commander.exe', 'device', 'reset',
                        '--serialno', serial_number], capture_output=True, check=True)
for thread in threads:
    thread.join()
