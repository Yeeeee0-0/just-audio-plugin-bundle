#!/usr/bin/env python3
"""Repeat only failed native editor cases for diagnostics; never changes a gate."""
import json
from pathlib import Path
import subprocess
import sys

if sys.platform != 'win32':
    raise SystemExit('Native Windows diagnostics only')
build, evidence = (Path(p).resolve() for p in sys.argv[1:3])
hosts = [p for p in build.rglob('just_editor_host_windows.exe') if p.parent.name == 'Release']
if len(hosts) != 1:
    print('No unique built editor host; preserve original failure without rerun.')
    raise SystemExit(0)
failed = set()
for case in evidence.glob('*/native/*'):
    if not case.is_dir():
        continue
    result = case/'result.json'
    if not result.is_file() or json.loads(result.read_text(encoding='utf-8-sig'))['status'] != 'PASS':
        failed.add(case.name.rsplit('-', 2)[0])
for slug in sorted(failed):
    bundle = build/'VST3/Release'/('Just_'+slug+'.vst3')
    if not bundle.is_dir():
        continue
    for attempt in range(3):
        target = evidence/'failure-diagnostics'/(slug+'-'+str(attempt+1))
        run = subprocess.run([str(hosts[0]), str(bundle), slug, str(target)],
                             capture_output=True, timeout=120, encoding='utf-8', errors='replace')
        target.mkdir(parents=True, exist_ok=True)
        (target/'process.log').write_text(run.stdout+run.stderr, encoding='utf-8')
        print(f'DIAGNOSTIC {slug} attempt={attempt+1} exit={run.returncode}; original CTest gate remains FAILED', flush=True)
