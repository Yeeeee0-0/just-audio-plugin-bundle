#!/usr/bin/env python3
"""Compare actual native combo pixels in new and pinned preview.17 Limiter DLLs.

Only the isolated test process shows its own non-activating HWND. The test
requests coloured native text for deterministic contrast, even without
ClearType. It reads only that control's existing pixels; it never repaints for
capture, changes system font settings, or accesses a user's host/desktop.
"""
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import urllib.request
import zipfile

BASE_COMMIT = '7aedc756b90f3489a1cf2cc0e8e75816fac08b16'
ARCHIVE_SHA = '488bfe54d72c6b01a8bfd526919dac59d965ce44f5cfc9b9615789a2a3f71cf1'
URL = 'https://github.com/Yeeeee0-0/just-audio-plugin-bundle/releases/download/v0.1.0-windows-preview.17/JUST-0.1.0-Windows-x64-preview.17-Portable.zip'
EXPECTED = 'bypassed native combo setter leaves only grayscale client pixels'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if sys.platform != 'win32':
        raise SystemExit('This regression requires native Windows.')
    build, evidence = [Path(x).resolve() for x in sys.argv[1:3]]
    hosts = [p for p in build.rglob('just_editor_host_windows.exe') if p.parent.name == 'Release']
    candidates = list((build.parent/'candidates').glob('*/candidate-manifest.json'))
    if len(hosts) != 1 or len(candidates) != 1:
        raise RuntimeError('Exactly one fresh test host and candidate required')
    candidate = json.loads(candidates[0].read_text(encoding='utf-8-sig'))
    current = next(x for x in candidate['plugins'] if x['slug'] == 'limiter')
    cache = build.parent/'escape-regression-input'
    cache.mkdir(exist_ok=True)
    archive = cache/'preview17-portable.zip'
    if not archive.exists() or digest(archive) != ARCHIVE_SHA:
        with urllib.request.urlopen(URL, timeout=45) as source, archive.open('wb') as out:
            while True:
                block = source.read(1024*1024)
                if not block:
                    break
                out.write(block)
    if digest(archive) != ARCHIVE_SHA:
        raise RuntimeError('Pinned old candidate digest mismatch')
    with zipfile.ZipFile(archive) as z:
        names = [i.orig_filename for i in z.infolist()]
        if any(PurePosixPath(n).is_absolute() or '..' in PurePosixPath(n).parts or '\\' in n or ':' in n for n in names):
            raise RuntimeError('Unsafe previous archive path')
        manifests = [n for n in names if n.endswith('/candidate-manifest.json')]
        if len(manifests) != 1:
            raise RuntimeError('Exactly one old manifest required')
        prefix = manifests[0].rsplit('/', 1)[0]+'/'
        manifest = json.loads(z.read(manifests[0]).decode('utf-8-sig'))
        previous = next(x for x in manifest['plugins'] if x['slug'] == 'limiter')
        for name in names:
            if name.startswith(prefix+previous['bundle']+'/') and not name.endswith('/'):
                relative = name[len(prefix):]
                data = z.read(name)
                if hashlib.sha256(data).hexdigest() != manifest['hashes'][relative[len('VST3/'):]]:
                    raise RuntimeError('Old Limiter bundle hash mismatch')
                out = cache/relative
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(data)
    output = evidence/'combo-gray-regression'
    output.mkdir(exist_ok=False)

    def probe(bundle, label):
        destination = output/label
        run = subprocess.run([str(hosts[0]), str(bundle), 'limiter', str(destination), '--combo-gray-only'],
                             capture_output=True, encoding='utf-8', errors='replace', timeout=120)
        (output/(label+'.log')).write_text(run.stdout+run.stderr, encoding='utf-8')
        result = json.loads((destination/'result.json').read_text(encoding='utf-8-sig'))
        if not result['actual_vst3_dll_loaded'] or not result['native_combo_live_probe']:
            raise RuntimeError('Actual DLL native paint probe did not run')
        return run.returncode, result

    code, result = probe(candidates[0].parent/current['bundle'], 'current-limiter')
    if code or result['status'] != 'PASS' or result['native_combo_gray_checks'] != 12 or result['native_combo_active_controls'] != 3 or result['native_combo_reopen_cycles'] != 3:
        raise RuntimeError('Current candidate failed actual native combo paint regression')
    old_code, old = probe(cache/previous['bundle'], 'preview17-limiter')
    if old_code != 1 or old['status'] != 'FAIL' or EXPECTED not in old['error'] or old['native_combo_active_controls'] < 1:
        raise RuntimeError('Old DLL did not fail the intended pixel assertion; regression detection unproven')
    report = {'result': 'PASS_CURRENT_AND_OLD_CANDIDATE_REJECTED', 'baseline_commit': BASE_COMMIT,
              'previous_archive_sha256': ARCHIVE_SHA, 'previous_limiter_sha256': previous['sha256'],
              'current_limiter_sha256': current['sha256'], 'test_host_sha256': digest(hosts[0]),
              'current_gray_checks': result['native_combo_gray_checks'], 'current_reopen_cycles': result['native_combo_reopen_cycles'],
              'current_active_positive_controls': result['native_combo_active_controls'],
              'old_candidate_exit': old_code, 'expected_failure': old['error'],
              'scope': result['native_combo_probe_scope'], 'actual_dll_loaded': True, 'real_reaper': 'NOT_RUN'}
    (output/'negative-control.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
