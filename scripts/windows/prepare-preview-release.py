#!/usr/bin/env python3
"""Package only a passing, native CI preview; never publish or install anything."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-number', required=True, type=int)
    args = parser.parse_args()
    if sys.platform != 'win32' or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Package public evidence only in the native Windows GitHub runner.')
    repository = 'Yeeeee0-0/just-audio-plugin-bundle'
    if os.environ.get('GITHUB_REPOSITORY') != repository:
        raise SystemExit('Unexpected repository')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if commit != os.environ.get('GITHUB_SHA'):
        raise SystemExit('Checkout does not match workflow source commit')
    candidates = list((ROOT/'build/candidates').glob('*/candidate-manifest.json'))
    if len(candidates) != 1:
        raise SystemExit('Exactly one candidate is required in a clean runner')
    candidate_path = candidates[0]
    candidate = read(candidate_path)
    if candidate['status'] != 'WINDOWS_NATIVE_AUTOMATED_CANDIDATE_NOT_REAPER_ACCEPTED':
        raise SystemExit('Candidate was not staged after native validation')
    results = list((ROOT/'build/windows-evidence').glob('*/result.json'))
    if len(results) != 1 or read(results[0])['result'] != 'PASS_NATIVE_BUILD_AND_AUTOMATED_TESTS':
        raise SystemExit('Native test result is missing or failed')
    native = results[0].parent
    validations = read(native/'candidate-validation.json')
    if len(validations) != 10 or any(p['validatorExit'] or p['failed'] or p['machine'] != 'AMD64' for p in validations):
        raise SystemExit('All ten VST3 validators must pass')
    native_results = [read(p) for p in (native/'native').glob('*/result.json')]
    if len(native_results) != 10 or any(p['status'] != 'PASS' or p.get('combo_stress_cycles') != 16 or not p.get('concurrent_combo_audio_blocks') or p.get('button_dispatch_checks',0) < 100 or p.get('escape_dispatch_checks') != 12 or p.get('escape_routing_queries',0) < 1 for p in native_results):
        raise SystemExit('Concurrent combo/modal regression evidence required for all ten DLLs')
    regression = read(ROOT/'build/windows-evidence/combo-regression/negative-control.json')
    if regression['result'] != 'PASS_OLD_CANDIDATE_REJECTED' or regression.get('button_negative_control') != 'PASS_OLD_CANDIDATE_REJECTED':
        raise SystemExit('Previous candidate must fail the new lifetime regression')
    escape_regression = read(ROOT/'build/windows-evidence/escape-routing-regression/negative-control.json')
    if escape_regression['result'] != 'PASS_OLD_CANDIDATE_REJECTED':
        raise SystemExit('Previous candidate must fail modal Escape routing negotiation')
    junit = ET.parse(native/'ctest.xml').getroot()
    if int(junit.get('failures', '0')) or int(junit.get('errors', '0')):
        raise SystemExit('Native CTest failures')
    installer = ROOT/'build/installer/JUST-0.1.0-Windows-x64-preview-setup.exe'
    installer_build = read(ROOT/'build/installer/installer-build.json')
    installer_tests = read(ROOT/'build/windows-evidence/installer/installer-tests.json')
    installer_sha = digest(installer)
    if installer_tests['status'] != 'PASSED' or installer_tests['installerSHA256'] != installer_sha:
        raise SystemExit('Actual installer tests missing, failed, or for a different executable')
    if installer_build['sha256'] != installer_sha or installer_build['candidateManifestSHA256'] != digest(candidate_path):
        raise SystemExit('Installer provenance does not match this candidate')
    out = ROOT/'build/preview-release'
    out.mkdir(parents=True, exist_ok=False)
    suffix = f'0.1.0-Windows-x64-preview.{args.run_number}'
    setup = out/f'JUST-{suffix}-Setup.exe'
    portable = out/f'JUST-{suffix}-Portable.zip'
    source = out/f'JUST-{suffix}-Source.zip'
    evidence = out/f'JUST-{suffix}-Evidence.zip'
    shutil.copyfile(installer, setup)
    shutil.copyfile(Path(str(candidate_path.parent)+'.zip'), portable)
    subprocess.run(['git', 'archive', '--format=zip', '--prefix=JUST-Windows-preview-source/',
                    '--output='+str(source), commit], cwd=ROOT, check=True)
    # This is a clean GitHub-hosted runner: only task-generated diagnostics are shared.
    # Never use this script to collect a user's REAPER, preset, or license directories.
    with zipfile.ZipFile(evidence, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted((ROOT/'build/windows-evidence').rglob('*')):
            if path.is_file():
                archive.write(path, 'windows-evidence/'+path.relative_to(ROOT/'build/windows-evidence').as_posix())
        for name in ['installer-build.json', 'makensis.log']:
            archive.write(ROOT/'build/installer'/name, 'installer/'+name)
    for source_file, name in [
        (ROOT/'WINDOWS-CODEX-PROMPT-zh.txt', 'WINDOWS-CODEX-PROMPT-zh.txt'),
        (ROOT/'docs/windows/ACCEPTANCE-zh.md', 'ACCEPTANCE-zh.md'),
        (ROOT/'docs/windows/KNOWN-DIFFERENCES-zh.md', 'KNOWN-DIFFERENCES-zh.md'),
        (ROOT/'scripts/windows/verify-preview-download.py', 'verify-preview-download.py'),
    ]:
        shutil.copyfile(source_file, out/name)
    def asset(path):
        return {'file_name': path.name, 'sha256': digest(path), 'bytes': path.stat().st_size}
    tag = f'v0.1.0-windows-preview.{args.run_number}'
    run_url = f'https://github.com/{repository}/actions/runs/{os.environ["GITHUB_RUN_ID"]}'
    manifest = {
        'schema': 1, 'status': 'UNSIGNED_WINDOWS_PREVIEW_NOT_USER_REAPER_ACCEPTED',
        'tag': tag, 'version': '0.1.0', 'vendor': 'Yee Huang', 'plugin_architecture': 'AMD64 PE32+',
        'source_commit': commit, 'repository': repository, 'workflow_run_url': run_url,
        'signed': False, 'native_build_and_automated_tests': 'PASSED',
        'ctest_count': len(junit.findall('testcase')), 'installer_tests': 'PASSED',
        'installer_check_count': installer_tests['checkCount'],
        'user_machine_reaper': 'NOT_RUN', 'manual_visual_audio_acceptance': 'NOT_RUN',
        'remaining_manual_validation': 'Real REAPER visual/audio/input and assistive-technology acceptance; see KNOWN-DIFFERENCES-zh.md.',
        'cross_platform_limiter_golden': 'Numerical comparison within 8 double epsilons; Mac/Windows bit equality is not claimed. See CTest log and KNOWN-DIFFERENCES-zh.md.',
        'installer': asset(setup), 'portable': asset(portable), 'source': asset(source),
        'evidence': asset(evidence), 'plugins': candidate['plugins'],
        'dependencies': read(ROOT/'third_party/dependency-lock.json'),
        'environment': read(native/'environment.json'), 'nsis_version': installer_build['nsisVersion'],
        'github_runner_image': {'os': os.environ.get('ImageOS'), 'version': os.environ.get('ImageVersion')},
        'stable_mac_release_modified': False,
        'fix_candidate': {
            'issue': 'Modal Escape was intercepted by host keyboard routing and closed the entire editor',
            'base_commit': '7aedc756b90f3489a1cf2cc0e8e75816fac08b16',
            'received_patch_sha256': '59648ab2342f18f5cebb9d2d6d58bf6a9da1f94c7204e655749007bfdc8f6ed0',
            'product_file': 'common/ui/NativeEditorWindows.cpp',
            'retained_lifetime_fix': 'CI run16 captured native button dispatch resuming after Delete rebuilt its parent. Queue modal button and Escape actions on the surviving editor; invalidate stale queued actions by modal generation.',
            'prior_lifetime_diagnostic_run_url': 'https://github.com/Yeeeee0-0/just-audio-plugin-bundle/actions/runs/37841801452',
            'change': 'Request modal Escape from the host dialog router with WM_GETDLGCODE; close only the overlay using the existing deferred action; let an open native combo list dismiss first; retain preview.17 fixes',
            'reaper_7_41_at_175_percent_dpi_revalidation': 'NOT_RUN_FOR_THIS_CANDIDATE',
            'concurrent_combo_cycles_per_plugin': 16,
            'negative_control': regression,
            'escape_routing_negative_control': escape_regression,
            'escape_dispatch_checks_per_plugin': 12,
            'escape_route_scope': 'WM_GETDLGCODE on modal controls, editor root, name edit and preset selection; real REAPER remains NOT_RUN',
        },
    }
    write(out/'preview-manifest.json', manifest)
    notes = f'''# JUST 0.1.0 Windows x64 preview {args.run_number}

**Unsigned modal-Escape routing fix candidate; actual REAPER keyboard revalidation remains pending.**

Preview.17 physical-input testing found that Escape could close REAPER's entire floating plugin editor instead of only JUST's open overlay. This candidate starts from `7aedc756b90f3489a1cf2cc0e8e75816fac08b16`, requests Escape through `WM_GETDLGCODE` only while an overlay exists, and uses the existing deferred close action. An open native combo list retains its first-Escape dismissal. Other keys and product text are unchanged. The earlier native combo/button lifetime, sibling clipping and bypass grayscale fixes are retained; DSP, IDs, parameters, state formats and Mac code are unchanged.

The native regression now checks Escape negotiation and closure from the scale and language combos, close button, editor root, preset name and preset selector. It verifies that the editor/host remain alive, the overlay closes, Escape is no longer claimed afterward, and normal Tab routing remains available. Each plugin completes 12 Escape checks plus the existing 16 modal/audio cycles. The pinned preview.17 EQ DLL must fail the new routing assertion. These are hidden HWND protocol tests; physical input, REAPER accelerators and themed popup behavior still require user-machine validation.

本候选只修复浮层 Escape 的宿主键盘协商。请确认焦点后逐项复测关闭按钮、缩放／语言／预设下拉框、预设名称输入框：Escape 只关闭浮层，插件和 REAPER 工程仍打开；展开下拉列表时首次 Escape 先收起列表。继续核对前述缩放、预设和 Limiter 旁路颜色，不把 preview.17 的实机结果自动记为本包通过。实机确认前不作为正式版。

- Download `JUST-{suffix}-Setup.exe` for the selectable installer (all ten selected by default).
- `Portable.zip` contains the exact ten tested x64 VST3 bundles and installation scripts.
- `Source.zip` is the complete public source and runtime artwork. Fetch the pinned official SDK with `scripts/bootstrap-sdk.py` when rebuilding.
- Download `SHA256SUMS.txt`, `preview-manifest.json`, the Chinese Codex prompt, acceptance checklist, and known differences as well.

Native Windows build, {manifest['ctest_count']} CTest cases, ten official VST3 validator runs, and {installer_tests['checkCount']} isolated installer checks passed. See the [workflow evidence]({run_url}) and Evidence.zip.

Source: `{commit}`. Plugin version `0.1.0`; vendor `Yee Huang`; VST3 AMD64 only. The setup bootstrap may be a 32-bit NSIS executable; the plugin payload is x64.

The installer verifies payloads, backs up selected previous JUST bundles, and restores them after an installation failure. Save work and close REAPER normally before installation. No user preset, REAPER configuration, license, or project is edited by the installer.

This candidate has not yet been tested in the user's Windows REAPER session. Earlier preview.12/13 observations do not constitute acceptance of these rebuilt files. GUI/input, high-DPI, presets, automation, and audible DSP acceptance must be checked on the Windows computer. Custom rotary controls include a native UI Automation provider and a required native client test; real screen-reader acceptance remains pending. This preview is not a claim of Mac/Windows visual parity or production readiness.

The stable macOS `main` branch and `v0.1.0` release are unchanged. This is a prerelease and is not marked latest.
'''
    (out/'RELEASE-NOTES.md').write_text(notes, encoding='utf-8')
    paths = sorted(p for p in out.iterdir() if p.is_file())
    (out/'SHA256SUMS.txt').write_text(''.join(f'{digest(p)}  {p.name}\n' for p in paths), encoding='ascii')
    subprocess.run([sys.executable, str(ROOT/'scripts/windows/verify-preview-download.py'), str(out)], check=True)
    print(json.dumps({'status': 'READY_FOR_PRERELEASE', 'directory': str(out), 'tag': tag}))


if __name__ == '__main__':
    main()
