"""Exercise Windows progress jobs against an owned Xbox scratch folder."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--broker', required=True)
args = parser.parse_args()
broker = Path(args.broker).resolve(strict=True)
test_id = uuid.uuid4().hex[:12]
remote = 'HDD:\\CH-Native-' + test_id
assert re.fullmatch(r'HDD:\\CH-Native-[a-f0-9]{12}', remote)
local = broker.parent / ('native-test-' + test_id)
local.mkdir()
owner = str(uuid.uuid4())
(local / 'owner.txt').write_text(owner, encoding='ascii')

def rpc(*arguments):
    result = subprocess.run([str(broker), '--rpc', *arguments], capture_output=True, text=True,
                            timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode: raise RuntimeError(result.stdout + result.stderr)
    return result.stdout

def text(value):
    encoded = value.encode('utf-8')
    return struct.pack('<I', len(encoded)) + encoded

def native(operation, path, argument):
    payload = text(str(argument)) + b'\0'
    frame = bytearray(struct.pack('<IIIQ', 0x484b5243, 1, 0, uuid.uuid4().int & ((1 << 64)-1)))
    frame += text('rgh') + text(operation) + text(path) + struct.pack('<I',len(payload)) + payload
    struct.pack_into('<I',frame,8,len(frame))
    request_file = local / (operation+'.request')
    request_file.write_bytes(frame)
    result = subprocess.run([str(broker),'--shell-job',str(request_file)], timeout=30,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode: raise RuntimeError('Native job failed: '+operation)
    assert not request_file.exists(), 'Job request was not consumed'

owned = success = False
try:
    rpc('mkdir', remote)
    rpc('upload', remote+'\\owner.txt', str(local/'owner.txt'))
    owned = True
    source, destination = local/'source.bin', local/'download.bin'
    source.write_bytes(os.urandom(1048576))
    native('job_upload',remote+'\\payload.bin',source)
    native('job_download',remote+'\\payload.bin',destination)
    assert hashlib.sha256(source.read_bytes()).digest() == hashlib.sha256(destination.read_bytes()).digest()
    rpc('download',remote+'\\owner.txt',str(local/'owner-confirmation.txt'))
    assert (local/'owner-confirmation.txt').read_text(encoding='ascii') == owner
    success = True
    print('PASS: Windows progress upload/download, 1 MiB SHA256 roundtrip; no transfer UI host.')
finally:
    if owned and success:
        rpc('delete',remote)
        print('Owned remote scratch folder removed.')
    elif owned:
        print('Failed job: remote scratch folder retained for inspection:',remote)
