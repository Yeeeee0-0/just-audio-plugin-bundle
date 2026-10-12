#!/usr/bin/env python3
"""Read-only Apple Installer queries. No installation commands or GUI operations."""
from pathlib import Path
import argparse, hashlib, json, plistlib, subprocess
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',required=True,type=Path,
                    help='Output directory previously produced by build_installer.py')
args=parser.parse_args()
ROOT=args.output_dir.expanduser().resolve()
E=ROOT/'evidence'
report=json.loads((E/'package-validation.json').read_text())
pkg=Path(report['installer'])
bundles=report['bundles']
assert len(bundles)==10

def query(args,name):
    r=subprocess.run(['/usr/sbin/installer',*args,'-pkg',str(pkg)],capture_output=True,text=True)
    (E/(name+'.stdout')).write_text(r.stdout)
    (E/(name+'.stderr')).write_text(r.stderr)
    assert r.returncode==0,(name,r.returncode,r.stderr,r.stdout)
    return r.stdout

domain=query(['-dominfo'],'installer-domains')
assert domain.strip()=='CurrentUserHomeDirectory'
assert query(['-query','RestartAction'],'installer-restart').strip()=='None'
native=plistlib.loads(query(['-showChoicesXML','-target','CurrentUserHomeDirectory'],'installer-default-choices').encode())

def leaves(items):
    for x in items:
        if x['childItems']: yield from leaves(x['childItems'])
        else: yield x

defaults=list(leaves(native))
assert len(defaults)==10
for c,b in zip(defaults,bundles):
    assert c['choiceIdentifier']==b['choice_id']
    assert c['choiceIsSelected']==1 and c['choiceIsEnabled'] and c['choiceIsVisible']
    assert len(c['pathsOfActivePackagesInChoice'])==1
    assert c['pathsOfActivePackagesInChoice'][0].endswith('#'+b['component_package'])

scenarios=[('all',[b['slug'] for b in bundles]),('none',[])]
scenarios += [(b['slug']+'-only',[b['slug']]) for b in bundles]
scenarios += [('eq-reverb-delay',['eq','reverb','delay']),('other-seven',[b['slug'] for b in bundles if b['slug'] not in ['eq','reverb','delay']])]
results=[]
source_manifest=json.loads((E/'payload-manifest.json').read_text())
distribution=ET.parse(ROOT/'build/expanded/Distribution').getroot()
for name,slugs in scenarios:
    changes=[{'choiceIdentifier':b['choice_id'],'choiceAttribute':'selected','attributeSetting':int(b['slug'] in slugs)} for b in bundles]
    request=E/('choices-'+name+'.plist')
    request.write_bytes(plistlib.dumps(changes))
    native=plistlib.loads(query(['-showChoicesAfterApplyingChangesXML',str(request),'-target','CurrentUserHomeDirectory'],'native-'+name).encode())
    actual={c['choiceIdentifier']:bool(c['attributeSetting']) for c in native if c['choiceAttribute']=='selected'}
    expected={b['choice_id']:b['slug'] in slugs for b in bundles}
    assert actual==expected,(name,actual,expected)
    files={}
    active=[]
    for b in bundles:
        if not actual[b['choice_id']]:continue
        choice=next(c for c in distribution.findall('choice') if c.attrib['id']==b['choice_id'])
        assert [x.attrib['id'] for x in choice.findall('pkg-ref')]==[b['package_id']]
        component=ROOT/'build/expanded'/b['component_package']
        info=ET.parse(component/'PackageInfo').getroot()
        assert info.attrib['install-location']=='/Library/Audio/Plug-Ins/VST3'
        active.append(b['component_package'])
        for p in (component/'Payload').rglob('*'):
            if p.is_file():
                relative=str(p.relative_to(component/'Payload'))
                assert relative not in files
                files[relative]=hashlib.sha256(p.read_bytes()).hexdigest()
    desired_names=[b['bundle']+'/' for b in bundles if b['slug'] in slugs]
    expected_files={p:h for p,h in source_manifest.items() if any(p.startswith(n) for n in desired_names)}
    assert files==expected_files
    results.append({'scenario':name,'selected_plugins':slugs,'native_selection_exact':True,'active_component_packages':active,'payload_file_count':len(files),'payload_matches_exact_selected_frozen_bundles':True,'excluded_plugins_have_no_active_package':True})
summary={'status':'PASS','default_choices':10,'all_default_selected':True,'all_enabled_and_visible':True,'domain':'CurrentUserHomeDirectory','effective_destination':'~/Library/Audio/Plug-Ins/VST3','restart':'None','scenario_count':len(results),'scenarios':results,'no_install_performed':True,'no_gui_launched':True,'package_sha256':hashlib.sha256(pkg.read_bytes()).hexdigest(),'meaning':'Native Installer selection evaluation plus exact per-component expanded payload verification. This is not an actual installation or GUI acceptance test.'}
(E/'selectable-install-validation.json').write_text(json.dumps(summary,indent=2)+'\n')
report['checks'].update({'native_installer_default_choices':True,'native_installer_14_subset_scenarios':True,'exact_selected_payloads':True,'native_installer_readonly_domain':True,'no_restart':True})
(E/'package-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:summary[k] for k in ['status','default_choices','all_default_selected','all_enabled_and_visible','scenario_count','domain','package_sha256']},indent=2))
