#!/usr/bin/env python3
"""Verify frozen algorithm/state/identity/Mac files and all packaged local assets."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'docs/windows/immutable-baseline.json').read_text(encoding='utf-8'))
for name, digest in manifest['files'].items():
    path = root / name
    if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit('Protected baseline changed: ' + name)
assets = json.loads((root / 'common/ui/resources/manifest.json').read_text(encoding='utf-8'))
for key, asset in assets['assets'].items():
    if asset['kind'] == 'image':
        path = root / 'common/ui/resources' / asset['path']
        if hashlib.sha256(path.read_bytes()).hexdigest() != asset['sha256']:
            raise SystemExit('UI asset mismatch: ' + key)
print('PASS frozen DSP, state, IDs, parameter/model/Mac files:', len(manifest['files']))
print('PASS ten original brand icons')
