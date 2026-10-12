#!/usr/bin/env python3
"""Package frozen bytes only. Never installs, signs, launches apps, or edits sources."""
from pathlib import Path
import argparse, hashlib, json, os, plistlib, shutil, stat, subprocess, zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
FROZEN = None
HEAD = 'b99abd3be4ad89b21e3a0c3caf1f12904503f168'
VERSION = '0.1.0'
PKG_ID = 'audio.just.plugin-bundle'
NAME = 'JUST-Audio-Plugin-Bundle-0.1.0-macOS-arm64-b99abd3-selectable-unsigned.pkg'
DISPLAY_NAMES = {'eq':'EQ','reverb':'Reverb','delay':'Delay','tremolo':'Tremolo','compressor':'Compressor','limiter':'Limiter','gate':'Gate','flanger':'Flanger','fake_stereo':'Fake Stereo','distortion':'Distortion'}
BUILD = None
EVIDENCE = None
OUT = None

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def run(args, log=None, check=True):
    result = subprocess.run([str(x) for x in args], capture_output=True, text=True)
    if log:
        (EVIDENCE / log).write_text(result.stdout + result.stderr)
    if check and result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}): {result.stderr}')
    return result

def file_map(root):
    return {str(p.relative_to(root)): sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    global FROZEN, BUILD, EVIDENCE, OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen-root', required=True, type=Path,
                        help='Frozen b99abd3 handoff directory, including its manifest, archive and VST3 bundles')
    parser.add_argument('--output-dir', required=True, type=Path,
                        help='Output directory for the PKG, build staging and evidence')
    args = parser.parse_args()
    FROZEN = args.frozen_root.expanduser().resolve()
    OUT = args.output_dir.expanduser().resolve()
    BUILD = OUT / 'build'
    EVIDENCE = OUT / 'evidence'
    for d in (BUILD, EVIDENCE, OUT): d.mkdir(parents=True, exist_ok=True)
    stage = BUILD / 'payload'
    if stage.exists():
        raise RuntimeError('Use a fresh build directory; refusing to overwrite prior staging.')
    stage.mkdir()
    handoff = json.loads((FROZEN/'release-handoff.json').read_text())
    assert handoff['head'] == HEAD
    candidates = handoff['candidates']
    assert len(candidates) == 10
    archive = FROZEN / 'JUST-0.1.0-mac-arm64-b99abd3.zip'
    zip_record = next(x for x in handoff['artifacts'] if Path(x['path']).name == archive.name)
    recorded_frozen = Path(zip_record['path']).parent
    assert sha(archive) == zip_record['sha256']
    expected = {}
    reports = []
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            path = Path(item.filename)
            assert not path.is_absolute() and '..' not in path.parts
            assert not stat.S_ISLNK(item.external_attr >> 16)
            if item.filename.startswith('__MACOSX/') or item.is_dir(): continue
            assert item.filename.startswith('VST3/')
            dest = stage / item.filename.removeprefix('VST3/')
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(z.read(item))
        for candidate in candidates:
            src = FROZEN / Path(candidate['bundle_path']).relative_to(recorded_frozen)
            bundle = stage / src.name
            actual = file_map(bundle)
            assert actual == candidate['file_hashes'] == file_map(src), src.name
            expected.update({f'{src.name}/{p}': h for p,h in actual.items()})
            info = plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
            assert info['CFBundleIdentifier'] == candidate['bundle_id']
            assert all(info[k] == VERSION for k in candidate['version_fields'])
            binary = bundle/'Contents/MacOS'/info['CFBundleExecutable']
            arch = run(['/usr/bin/lipo','-archs',binary]).stdout.strip()
            assert arch == 'arm64'
            load = run(['/usr/bin/xcrun','vtool','-show-build',binary]).stdout
            assert 'minos 11.0' in load and 'platform MACOS' in load
            details = run(['/usr/bin/codesign','-dvvv',bundle]).stderr
            assert 'Signature=adhoc' in details and 'TeamIdentifier=not set' in details
            dependencies = run(['/usr/bin/otool','-L',binary]).stdout
            assert all(line.strip().startswith(('/System/Library/','/usr/lib/')) for line in dependencies.splitlines()[1:])
            for p in [bundle, *bundle.rglob('*')]:
                p.chmod(0o755 if p.is_dir() or p == binary else 0o644)
            run(['/usr/bin/codesign','--verify','--deep','--strict','--verbose=2',bundle], src.stem+'-signature.log')
            (EVIDENCE/(src.stem+'-binary.log')).write_text(load+'\n'+details+'\n'+dependencies)
            slug = candidate['slug']
            reports.append({'slug':slug,'title':DISPLAY_NAMES[slug],'choice_id':'just-'+slug,'package_id':PKG_ID+'.vst3.'+slug,'component_package':'JUST-'+slug+'.pkg','bundle':src.name,'bundle_id':info['CFBundleIdentifier'],'version':VERSION,'architecture':arch,'min_os':'11.0','sdk':'27.0','binary_sha256':sha(binary),'source_byte_match':True,'signature':'ad-hoc','strict_signature_valid':True,'files':len(actual)})
    assert file_map(stage) == expected
    assert len(list(stage.iterdir())) == 10
    component_config = BUILD/'components'
    component_config.mkdir(exist_ok=True)
    for r in reports:
        component_stage = BUILD/'component-roots'/r['slug']
        component_stage.mkdir(parents=True)
        shutil.copytree(stage/r['bundle'], component_stage/r['bundle'])
        components = [{'RootRelativeBundlePath':r['bundle'], 'BundleIsRelocatable':False, 'BundleIsVersionChecked':True, 'BundleHasStrictIdentifier':True, 'BundleOverwriteAction':'upgrade'}]
        components_path = component_config/(r['slug']+'.plist')
        components_path.write_bytes(plistlib.dumps(components))
        run(['/usr/bin/pkgbuild','--root',component_stage,'--component-plist',components_path,'--identifier',r['package_id'],'--version',VERSION,'--install-location','/Library/Audio/Plug-Ins/VST3','--ownership','recommended','--compression','legacy',BUILD/r['component_package']], 'pkgbuild-'+r['slug']+'.log')
    distribution = ET.Element('installer-gui-script', minSpecVersion='2')
    ET.SubElement(distribution, 'title').text = 'JUST Audio Plugin Bundle'
    ET.SubElement(distribution, 'product', id=PKG_ID, version=VERSION)
    for element,file in [('welcome','Welcome.html'),('readme','ReadMe.html'),('conclusion','Conclusion.html')]:
        ET.SubElement(distribution,element,{'file':file,'mime-type':'text/html'})
    ET.SubElement(distribution,'options',{'customize':'always','require-scripts':'false','hostArchitectures':'arm64'})
    ET.SubElement(distribution,'domains',{'enable_anywhere':'false','enable_currentUserHome':'true','enable_localSystem':'false'})
    ET.SubElement(ET.SubElement(ET.SubElement(distribution,'volume-check'),'allowed-os-versions'),'os-version',min='11.0')
    outline = ET.SubElement(distribution,'choices-outline')
    for r in reports:
        ET.SubElement(outline,'line',choice=r['choice_id'])
        choice = ET.SubElement(distribution,'choice',{'id':r['choice_id'],'title':'JUST '+r['title'],'description':'Yee Huang · 0.1.0 · Apple Silicon arm64. Installs '+r['bundle']+' in ~/Library/Audio/Plug-Ins/VST3. Clear this checkbox to skip installation; an existing copy is kept.','start_selected':'true','start_enabled':'true','start_visible':'true'})
        ET.SubElement(choice,'pkg-ref',id=r['package_id'])
        ET.SubElement(distribution,'pkg-ref',{'id':r['package_id'],'version':VERSION,'onConclusion':'None'}).text=r['component_package']
    ET.indent(distribution,space='  ')
    ET.ElementTree(distribution).write(BUILD/'Distribution.xml',encoding='utf-8',xml_declaration=True)
    final = OUT/NAME
    run(['/usr/bin/productbuild','--distribution',BUILD/'Distribution.xml','--resources',ROOT/'resources','--package-path',BUILD,final], 'productbuild.log')
    expanded = BUILD/'expanded'
    run(['/usr/sbin/pkgutil','--expand-full',final,expanded], 'pkgutil-expand.log')
    assert not list(expanded.rglob('Scripts'))
    expanded_files = {}
    for r in reports:
        component = expanded/r['component_package']
        payload = component/'Payload'
        assert len(list(payload.iterdir())) == 1
        assert file_map(payload) == {name:h for name,h in expected.items() if name.startswith(r['bundle']+'/')}
        expanded_files.update(file_map(payload))
        info = ET.parse(component/'PackageInfo').getroot()
        assert info.attrib['identifier'] == r['package_id'] and info.attrib['version'] == VERSION
        assert info.attrib['install-location'] == '/Library/Audio/Plug-Ins/VST3'
        assert len(info.findall('./bundle')) == 1
        for key in ('bundle-version','upgrade-bundle','strict-identifier'):
            assert [b.attrib['id'] for b in info.findall('./'+key+'/bundle')] == [r['bundle_id']]
        assert not info.findall('./relocate/*') and info.attrib['relocatable'] == 'false'
        bom = run(['/usr/bin/lsbom',component/'Bom'], 'payload-bom-'+r['slug']+'.txt').stdout
        for row in bom.splitlines():
            parts = row.split('\t')
            mode = int(parts[1], 8)
            assert parts[2] == '0/0', row
            assert stat.S_IMODE(mode) in (0o644,0o755), row
        run(['/usr/bin/codesign','--verify','--deep','--strict',payload/r['bundle']])
    assert expanded_files == expected
    dist = ET.parse(expanded/'Distribution').getroot()
    assert dist.find('domains').attrib == {'enable_anywhere':'false','enable_currentUserHome':'true','enable_localSystem':'false'}
    assert dist.find('options').attrib['hostArchitectures'] == 'arm64'
    assert dist.find('./volume-check/allowed-os-versions/os-version').attrib['min'] == '11.0'
    assert dist.find('script') is None
    choices = dist.findall('choice')
    assert len(choices) == 10 and len(dist.findall('./choices-outline/line')) == 10
    for choice,r in zip(choices,reports):
        assert choice.attrib['id'] == r['choice_id']
        assert all(choice.attrib['start_'+flag]=='true' for flag in ['selected','enabled','visible'])
        assert all(flag not in choice.attrib for flag in ['selected','enabled','visible'])
        assert [ref.attrib['id'] for ref in choice.findall('pkg-ref')] == [r['package_id']]
    for page in ('welcome','readme','conclusion'):
        assert (expanded/'Resources'/dist.find(page).attrib['file']).is_file()
    signature = run(['/usr/sbin/pkgutil','--check-signature',final], 'installer-signature.log', check=False)
    assert 'no signature' in (signature.stdout+signature.stderr).lower()
    report = {'head':HEAD,'source_archive':str(archive),'source_archive_sha256':sha(archive),'source_handoff_sha256':sha(FROZEN/'release-handoff.json'),'product':'JUST Audio Plugin Bundle','vendor':'Yee Huang','version':VERSION,'package_id':PKG_ID,'installer':str(final),'installer_bytes':final.stat().st_size,'installer_sha256':sha(final),'architecture':'arm64','binary_minimum_macos':'11.0','old_os_runtime_tested':False,'build_sdk':'27.0','package_signed':False,'developer_id_signed':False,'notarized':False,'plugin_signature':'ad-hoc (preserved, not Developer ID)','installation_domain':'CurrentUserHomeDirectory only','destination':'~/Library/Audio/Plug-Ins/VST3','user_presets_untouched':'~/Library/Application Support/JUST/Presets/v1/','payload_file_count':len(expected),'uncompressed_payload_bytes':sum(p.stat().st_size for p in stage.rglob('*') if p.is_file()),'bundles':reports,'checks':{'source_zip_hash':True,'all_source_bundle_files_match':True,'expanded_payload_matches_source':True,'strict_bundle_signatures':True,'bundle_ids_and_versions':True,'payload_modes_755_directories_and_executables_644_data':True,'recommended_ownership_0_0_in_bom_user_domain_remaps_to_user':True,'no_install_scripts':True,'no_distribution_script':True,'no_symlinks':True,'no_relocation':True,'strict_bundle_ids':True,'atomic_bundle_upgrade':True,'version_check_enabled':True,'no_payload_outside_ten_bundles':True,'standard_pages_present':True,'ten_independent_choices':True,'all_ten_default_selected':True,'unchecking_does_not_uninstall':True,'one_bundle_per_component':True},'not_performed':['Installer GUI or live install','DAW launch/load test in this task','old macOS runtime test','Intel/Universal build','Developer ID signing','notarization','public publishing'],'upgrade':'Apple Installer atomic upgrade of the exact named bundles, strict identifiers, no relocation; newer bundle versions are version checked. Equal 0.1.0 content can be reinstalled. Only selected component packages are installed. Deselecting leaves existing copies unchanged. Preserve the per-plugin package ids for future installers.','uninstall':'Close DAWs, remove only the ten named .vst3 bundles from the current user VST3 folder. Keep user preset folders; receipt removal alone is not uninstall.','universal_feasibility':'Repository includes mac-universal preset. Would require rebuilding all ten plugins and dependencies plus Intel and host QA; no Intel artifact or validation is supplied. Not attempted.'}
    (EVIDENCE/'package-validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    (EVIDENCE/'payload-manifest.json').write_text(json.dumps(expected,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['installer','installer_bytes','installer_sha256','payload_file_count','architecture','binary_minimum_macos']},indent=2))

if __name__ == '__main__': main()
