#!/usr/bin/env python3
"""Prove modal Escape routing rejects the pinned preview.17 DLL."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
import urllib.request
import zipfile

BASE_COMMIT = '7aedc756b90f3489a1cf2cc0e8e75816fac08b16'
ARCHIVE_SHA = '488bfe54d72c6b01a8bfd526919dac59d965ce44f5cfc9b9615789a2a3f71cf1'
URL = 'https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-windows-preview.17/JUST-0.1.0-Windows-x64-preview.17-Portable.zip'
EXPECTED = 'modal controls request Escape from the host dialog keyboard router'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if sys.platform != 'win32':
        raise SystemExit('This regression must run on native Windows.')
    build, evidence_root = [Path(x).resolve() for x in sys.argv[1:3]]
    hosts = [p for p in build.rglob('just_editor_host_windows.exe') if p.parent.name == 'Release']
    if len(hosts) != 1:
        raise RuntimeError('Exactly one freshly built native test host is required')
    cache = build.parent/'escape-regression-input'
    cache.mkdir(exist_ok=True)
    archive = cache/'preview17-portable.zip'
    if not archive.exists() or digest(archive) != ARCHIVE_SHA:
        pending = cache/'download.part'
        for attempt in range(3):
            try:
                with urllib.request.urlopen(URL, timeout=45) as source, pending.open('wb') as target:
                    while True:
                        block = source.read(1024 * 1024)
                        if not block:
                            break
                        target.write(block)
                if digest(pending) != ARCHIVE_SHA:
                    raise RuntimeError('Pinned previous-candidate SHA256 mismatch')
                pending.replace(archive)
                break
            except Exception:
                if attempt == 2:
                    raise
                time.sleep(2)
    with zipfile.ZipFile(archive) as z:
        names = [i.orig_filename for i in z.infolist()]
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
                raise RuntimeError('Unsafe previous-candidate ZIP name')
        manifests = [n for n in names if n.endswith('/candidate-manifest.json')]
        if len(manifests) != 1:
            raise RuntimeError('Expected one previous-candidate manifest')
        prefix = manifests[0].rsplit('/', 1)[0]+'/'
        old = json.loads(z.read(manifests[0]).decode('utf-8-sig'))
        eq = next(p for p in old['plugins'] if p['slug'] == 'eq')
        for name in names:
            if name.startswith(prefix+eq['bundle']+'/') and not name.endswith('/'):
                relative = name[len(prefix):]
                data = z.read(name)
                if hashlib.sha256(data).hexdigest() != old['hashes'][relative[len('VST3/'):]]:
                    raise RuntimeError('Previous EQ bundle asset mismatch')
                target = cache/relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
    output = evidence_root/'escape-routing-regression'
    output.mkdir(exist_ok=False)
    result_dir = output/'preview17-eq'
    run = subprocess.run([str(hosts[0]), str(cache/eq['bundle']), 'eq', str(result_dir), '--escape-routing-only'],
                         capture_output=True, timeout=120, encoding='utf-8', errors='replace')
    (output/'previous-candidate.log').write_text(run.stdout+run.stderr, encoding='utf-8')
    result = json.loads((result_dir/'result.json').read_text(encoding='utf-8-sig'))
    if run.returncode != 1 or result['status'] != 'FAIL' or not result['actual_vst3_dll_loaded'] or EXPECTED not in result['error']:
        raise RuntimeError('Old candidate did not fail the intended Escape routing assertion; do not claim regression detection')
    report = {'result': 'PASS_OLD_CANDIDATE_REJECTED', 'baseline_commit': BASE_COMMIT,
              'previous_archive_sha256': ARCHIVE_SHA, 'previous_eq_sha256': eq['sha256'],
              'test_host_sha256': digest(hosts[0]), 'expected_failure': result['error'],
              'old_candidate_exit': run.returncode, 'actual_dll_loaded': True,
              'routing_scope': 'WM_GETDLGCODE negotiation plus direct native key dispatch; not actual REAPER', 'real_reaper': 'NOT_RUN'}
    (output/'negative-control.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
