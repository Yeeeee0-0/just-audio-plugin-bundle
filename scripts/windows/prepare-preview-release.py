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
    if len(native_results) != 10 or any(p['status'] != 'PASS' or p.get('combo_stress_cycles') != 16 or not p.get('concurrent_combo_audio_blocks') or p.get('button_dispatch_checks',0) < 100 or p.get('escape_dispatch_checks') != 8 for p in native_results):
        raise SystemExit('Concurrent combo/modal regression evidence required for all ten DLLs')
    regression = read(ROOT/'build/windows-evidence/combo-regression/negative-control.json')
    if regression['result'] != 'PASS_OLD_CANDIDATE_REJECTED' or regression.get('button_negative_control') != 'PASS_OLD_CANDIDATE_REJECTED':
        raise SystemExit('Previous candidate must fail the new lifetime regression')
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
            'issue': 'Native combo/button sender lifetime and bypassed native control repaint',
            'base_commit': 'd0abf4f71c5a4b56e68a1441edf4dfc11dee8d04',
            'received_patch_sha256': '3a698767f3b60292d4deae7a96feae47af0051206eb350f2287ff119517da7d0',
            'product_file': 'common/ui/NativeEditorWindows.cpp',
            'additional_reviewed_fix': 'CI run16 captured native button dispatch resuming after Delete rebuilt its parent. Queue modal button and Escape actions on the surviving editor; invalidate stale queued actions by modal generation.',
            'diagnostic_run_url': 'https://github.com/Yeeeee0-0/just-audio-plugin-bundle/actions/runs/37841801452',
            'change': 'Update scale, language and preset selection in place; preserve native combo lifetime; defer destructive modal button/Escape actions until dispatch returns; repaint bypassed native controls after direct setters; retain preview.13 sibling clipping',
            'reaper_7_41_at_175_percent_dpi_revalidation': 'NOT_RUN_FOR_THIS_CANDIDATE',
            'concurrent_combo_cycles_per_plugin': 16,
            'negative_control': regression,
        },
    }
    write(out/'preview-manifest.json', manifest)
    notes = f'''# JUST 0.1.0 Windows x64 preview {args.run_number}

**Unsigned Windows UI fix candidate; real REAPER playback/scale and bypass-color revalidation remains pending.**

Preview.13 has a reported REAPER 7.41 access violation in COMCTL32.dll while changing EQ overlay scale during playback. This candidate is based on `d0abf4f71c5a4b56e68a1441edf4dfc11dee8d04` and keeps scale/language/preset combo HWNDs alive through selection notifications, updating their UI in place. CI additionally captured a native button crash after Delete rebuilt the modal inside its click notification. Destructive modal button/Escape actions are now queued on the surviving editor window, with stale-action generation checks. The native grayscale wrapper also repaints bypassed controls after direct native state/text setters, addressing the reported blue Limiter checkbox. The earlier sibling-clipping fix is retained. DSP, plugin IDs, parameters, state formats and Mac code are unchanged.

The native host now observes sender destruction (including handle reuse), verifies real common-control keyboard selection and host resize rejection, exercises user/factory preset selection, and performs 16 modal/view/scale cycles per plugin while actual float32/float64 DLL audio processing continues on a separate thread with unchanged twin output. Every native button click is also observed for sender lifetime, and eight Escape callbacks per plugin must preserve their sender until returning. The same new test host rejects the pinned preview.13 EQ DLL at both combo and explicit button-notification lifetime assertions. Limiter additionally verifies native checkbox state, text, lifetime and unchanged automation after setters, with immediate and post-refresh offscreen captures. These are hidden-window native tests, not physical input or real REAPER acceptance; live native themed animation/color remains unverified.

本包修复候选针对 preview.13 播放中切换 EQ 浮层缩放时的 COMCTL32.dll 崩溃：下拉框选择通知改为原位更新，不在其回调尚未返回时销毁控件。CI 另定位到按钮点击中同步重建浮层的同类崩溃，现将破坏性浮层按钮与 Escape 操作排到回调返回后执行，并过滤过期操作。另补上 Limiter 等原生控件设置状态后的旁路灰度重绘。请在 REAPER 7.41、175% DPI 下重新检查十款播放中缩放、语言／预设切换、浮层开关和重复操作，以及 Limiter 勾选框旁路后立即、悬停、点击和等待刷新时的黑白显示；保留旧测试证据，单独记录本包结果。实机确认前不作为正式版。

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
