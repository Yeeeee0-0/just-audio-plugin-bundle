#!/usr/bin/env python3
"""Inspect actual PE files, validate resources, then run official SDK validator."""
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

if sys.platform != 'win32':
    raise SystemExit('Windows native validation only. No cross-build pass is possible here.')
root = Path(__file__).resolve().parents[2]
build, evidence = (Path(x).resolve() for x in sys.argv[1:3])
identities = json.loads((root/'common/vst3/plugin-identities.json').read_text(encoding='utf-8'))
validators = list((build/'bin/Release').glob('validator.exe'))
if len(validators) != 1:
    raise SystemExit('Exactly one built SDK validator.exe required under bin/Release')
results = []
for identity in identities:
    stem = 'Just_' + identity['slug']
    bundle = build/'VST3/Release'/(stem+'.vst3')
    binary = bundle/'Contents/x86_64-win'/(stem+'.vst3')
    data = binary.read_bytes()
    if data[:2] != b'MZ' or len(data) < 64:
        raise SystemExit(f'Not a PE file: {binary}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', data, offset+4)[0] != 0x8664:
        raise SystemExit(f'Not AMD64 PE: {binary}')
    if struct.unpack_from('<H', data, offset+24)[0] != 0x20b:
        raise SystemExit(f'Not PE32+: {binary}')
    asset_root = bundle/'Contents/Resources/JustUI'
    source_assets = root/'common/ui/resources'
    for asset in source_assets.rglob('*'):
        if asset.is_file() and asset.name != '.DS_Store':
            target = asset_root/asset.relative_to(source_assets)
            if not target.is_file() or target.read_bytes() != asset.read_bytes():
                raise SystemExit('Missing/mismatched packaged UI asset: '+str(target))
    if not (bundle/'Contents/Resources/Licenses/VST3-SDK-MIT.txt').is_file():
        raise SystemExit('SDK license missing: '+stem)
    run = subprocess.run([str(validators[0]), str(bundle)], capture_output=True, timeout=180,
                         encoding='utf-8', errors='replace')
    output = run.stdout + run.stderr
    (evidence/(stem+'-validator.log')).write_text(output, encoding='utf-8')
    counts = re.search(r'Result:\s*(\d+) tests passed,\s*(\d+) tests failed', output)
    entry = {'slug':identity['slug'], 'binary':str(binary), 'sha256':hashlib.sha256(data).hexdigest(),
             'machine':'AMD64', 'format':'PE32+', 'validatorExit':run.returncode,
             'passed':int(counts[1]) if counts else None, 'failed':int(counts[2]) if counts else None}
    results.append(entry)
    (evidence/'candidate-validation.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(entry), flush=True)
    if run.returncode or not counts or int(counts[2]):
        raise SystemExit('SDK validator failed: '+stem)
if len(results) != 10:
    raise SystemExit('Expected ten candidates')
print('PASS ten AMD64 PE files, packaged assets, official VST3 validation. See native editor tests for runtime class metadata.')
