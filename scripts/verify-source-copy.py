#!/usr/bin/env python3
"""Validate a read-only copied SDK source tree against reviewed source hashes."""
import json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'third_party/sdk-source-hashes.json').read_text())
sdk=root/'third_party/vst3sdk'
for name,expected in manifest['files'].items():
    p=sdk/name
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
        raise SystemExit('SDK source mismatch: '+name)
print('PASS SDK source copy:',manifest['commit'],len(manifest['files']),'files')
