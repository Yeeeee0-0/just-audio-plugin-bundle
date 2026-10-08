#!/usr/bin/env python3
"""Prove the native combo-lifetime regression rejects the pinned preview.13 DLL."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import time
import urllib.request
import zipfile

BASE_COMMIT = 'd0abf4f71c5a4b56e68a1441edf4dfc11dee8d04'
ARCHIVE_SHA = 'edc84f4c49ebd17df2fa8d84ed592460d710d9f4f4926385839e38b8bb782726'
URL = 'https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-windows-preview.13/JUST-0.1.0-Windows-x64-preview.13-Portable.zip'
EXPECTED = 'selection notification preserves native combo lifetime'
BUTTON_EXPECTED = 'button dispatch preserves native sender until callback returns'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if sys.platform != 'win32':
        raise SystemExit('This regression must run on native Windows.')
    build, evidence_root = [Path(x).resolve() for x in sys.argv[1:3]]
    hosts = [p for p in build.rglob('just_editor_host_windows.exe') if p.parent.name == 'Release']
    if len(hosts) != 1:
        raise RuntimeError('Exactly one freshly built native test host is required')
    cache = build.parent/'combo-regression-input'
    cache.mkdir(exist_ok=True)
    archive = cache/'preview13-portable.zip'
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
    output = evidence_root/'combo-regression'
    output.mkdir(exist_ok=False)
    result_dir = output/'preview13-eq'
    run = subprocess.run([str(hosts[0]), str(cache/eq['bundle']), 'eq', str(result_dir)],
                         capture_output=True, timeout=120, encoding='utf-8', errors='replace')
    (output/'previous-candidate.log').write_text(run.stdout+run.stderr, encoding='utf-8')
    result = json.loads((result_dir/'result.json').read_text(encoding='utf-8-sig'))
    if run.returncode != 1 or result['status'] != 'FAIL' or not result['actual_vst3_dll_loaded'] or EXPECTED not in result['error']:
        raise RuntimeError('Old candidate did not fail the intended lifetime assertion; do not claim regression detection')
    button_dir = output/'preview13-button'
    button_run = subprocess.run([str(hosts[0]), str(cache/eq['bundle']), 'eq', str(button_dir), '--button-lifetime-only'],
                                capture_output=True, timeout=120, encoding='utf-8', errors='replace')
    (output/'previous-button-candidate.log').write_text(button_run.stdout+button_run.stderr, encoding='utf-8')
    button_result = json.loads((button_dir/'result.json').read_text(encoding='utf-8-sig'))
    if button_run.returncode != 1 or button_result['status'] != 'FAIL' or not button_result['actual_vst3_dll_loaded'] or BUTTON_EXPECTED not in button_result['error']:
        raise RuntimeError('Old candidate did not fail the intended button lifetime assertion')
    report = {'result': 'PASS_OLD_CANDIDATE_REJECTED', 'baseline_commit': BASE_COMMIT,
              'previous_archive_sha256': ARCHIVE_SHA, 'previous_eq_sha256': eq['sha256'],
              'test_host_sha256': digest(hosts[0]), 'expected_failure': result['error'],
              'old_candidate_exit': run.returncode, 'actual_dll_loaded': True,
              'old_button_candidate_exit': button_run.returncode, 'expected_button_failure': button_result['error'],
              'button_negative_control': 'PASS_OLD_CANDIDATE_REJECTED',
              'real_reaper': 'NOT_RUN'}
    (output/'negative-control.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
