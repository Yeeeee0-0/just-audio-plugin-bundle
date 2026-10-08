#!/usr/bin/env python3
"""Audit identity/bypass ownership and runtime assets across accepted schemas.

Some manifests inline parameterSpecs; Delay, Compressor and Distortion instead
name their reviewed parameter document. These are explicit schema adapters, not a
fallback that skips a missing or malformed registry. Full DSP/registry acceptance
remains the module tests' responsibility (EQ's inline list contains bypass only).
"""
import hashlib
import json
from pathlib import Path
import re
import sys


class ContractError(ValueError):
    pass


def require(condition, message):
    # Unlike assert, audit checks must also run under python -O.
    if not condition:
        raise ContractError(message)


def local_file(root, name):
    require(isinstance(name, str) and bool(name), f"{root}: invalid file reference")
    relative = Path(name)
    require(not relative.is_absolute() and '..' not in relative.parts,
            f"{root}: file reference must stay local: {name}")
    path = (root / relative).resolve()
    require(root.resolve() in path.parents, f"{root}: file reference escapes owner: {name}")
    require(path.is_file(), f"Missing contract file: {path}")
    return path


def read_json(path):
    try:
        return json.loads(path.read_text(encoding='utf-8'))
    except (OSError, ValueError) as error:
        raise ContractError(f"{path}: {error}") from error


def parameter_records(directory, manifest):
    if 'parameterSpecs' in manifest:
        # Malformed inline data must fail, even if another reference is present.
        records, source = manifest['parameterSpecs'], directory / 'manifest.json'
    else:
        fields = [key for key in ('parameterGolden', 'parameterProposal', 'parameterMap')
                  if key in manifest]
        require(len(fields) == 1, f"{directory.name}: require one explicit parameter source")
        source = local_file(directory, manifest[fields[0]])
        document = read_json(source)
        require(isinstance(document, dict) and 'parameters' in document,
                f"{source}: expected reviewed parameters list")
        records = document['parameters']
    require(isinstance(records, list) and bool(records), f"{source}: empty/invalid parameter list")
    ids, keys = [], []
    for record in records:
        require(isinstance(record, dict), f"{source}: invalid parameter record")
        # Tremolo uses id/key and retains stableKey; all other accepted sources
        # use paramID/stableKey. If both aliases occur they must agree.
        param_id = record.get('paramID', record.get('id'))
        key = record.get('stableKey', record.get('key'))
        require(type(param_id) is int and 0 <= param_id <= 0xffffffff,
                f"{source}: invalid parameter ID")
        require(isinstance(key, str) and bool(key), f"{source}: missing stable parameter key")
        if 'id' in record:
            require(type(record['id']) is int and record['id'] == param_id,
                    f"{source}: conflicting parameter ID aliases")
        if 'key' in record:
            require(record['key'] == key, f"{source}: conflicting stable key aliases")
        require(param_id not in ids and key not in keys, f"{source}: duplicate parameter ID/key")
        ids.append(param_id)
        keys.append(key)
    return ids, keys, source


# Accepted 485d2d2 has three display contracts that intentionally differ from
# the immutable foundation's placeholder labels. Pin both sides instead of
# accepting arbitrary labels or loosening a global count to any nonempty list.
# Limiter comes from accepted75944b5; Gate/Wider come from accepted7d0631b.
APPROVED_SIMPLE_LABELS = {
    'limiter': (['Input', 'Ceiling', 'Release'], ['Input Gain', 'Output Gain']),
    'gate': (['Threshold', 'Range', 'Release'], ['Threshold', 'Range', 'Attack', 'Release']),
    'fake_stereo': (['Generated Width', 'Low Protect', 'Character'], ['Gain', 'Width', 'Asymmetry', 'Rotation']),
}


def check_simple_labels(directory, identity, manifest):
    slug, legacy, labels = identity['slug'], identity['simpleControls'], manifest['simpleControlLabels']
    require(isinstance(legacy, list) and 3 <= len(legacy) <= 4,
            f"{slug}: foundation Simple label capacity changed")
    if slug not in APPROVED_SIMPLE_LABELS:
        require(isinstance(labels, list) and labels == legacy,
                f"{slug}: baseline Simple labels/count changed")
        return
    expected_legacy, expected_current = APPROVED_SIMPLE_LABELS[slug]
    require(legacy == expected_legacy and labels == expected_current,
            f"{slug}: accepted Simple label contract changed")
    source = (directory / 'Module.cpp').read_text(encoding='utf-8')
    block = re.search(r'SimpleControlBinding\s+bindings\s*\[\s*\]\s*=\s*\{(.*?)\n\};', source, re.S)
    require(block is not None, f"{slug}: cannot read explicit module Simple bindings")
    declared = re.findall(r'\{\s*"([^"\n]+)"\s*,\s*\{', block.group(1))
    require(declared == expected_current and len(re.findall(r'^\s*\{', block.group(1), re.M)) == len(declared),
            f"{slug}: manifest Simple labels disagree with module bindings")


def audit(root):
    root = Path(root).resolve()
    identities = read_json(root / 'common/vst3/plugin-identities.json')
    require(isinstance(identities, list) and len(identities) == 10, 'Require ten plugin identities')
    uids, bypass_keys, slugs = set(), set(), set()
    table = (root / 'common/vst3/PluginIdentities.hpp').read_text(encoding='utf-8')
    manifest_sources = {}
    for identity in identities:
        for field in ('processorUID', 'controllerUID'):
            words = identity[field]
            require(isinstance(words, list) and len(words) == 4 and
                    all(type(word) is int and 0 <= word <= 0xffffffff for word in words),
                    f"{identity['slug']}: invalid {field}")
            uid = tuple(words)
            require(uid not in uids, f"{identity['slug']}: duplicate UID")
            uids.add(uid)
            require(', '.join('0x%08Xu' % word for word in uid) in table,
                    f"{identity['slug']}: UID does not match literal C++ table")
        bypass_key = identity['bypassKey']
        require(bypass_key not in bypass_keys, 'Duplicate plugin bypass key')
        bypass_keys.add(bypass_key)
        require(type(identity['bypassParamID']) is int and identity['bypassParamID'] == 0,
                f"{identity['slug']}: bypass ID must remain zero")
        slug = identity['slug']
        require(slug not in slugs, f"Duplicate plugin slug: {slug}")
        slugs.add(slug)
        directory = local_file(root / 'plugins', slug + '/manifest.json').parent
        manifest = read_json(directory / 'manifest.json')
        require(manifest['slug'] == slug, f"{slug}: manifest ownership mismatch")
        check_simple_labels(directory, identity, manifest)
        ids, keys, source = parameter_records(directory, manifest)
        require(ids[0] == 0 and keys[0] == bypass_key, f"{slug}: first parameter must be fixed bypass ID/key")
        if 'parameterCount' in manifest:
            require(type(manifest['parameterCount']) is int and manifest['parameterCount'] == len(ids),
                    f"{slug}: declared parameter count mismatch")
        if 'registeredHostIDs' in manifest:
            require(isinstance(manifest['registeredHostIDs'], list) and
                    all(type(value) is int for value in manifest['registeredHostIDs']) and
                    manifest['registeredHostIDs'] == ids, f"{slug}: registeredHostIDs mismatch")
        parameter_source = local_file(directory, manifest.get('parameterSpecsSource', 'Parameters.hpp'))
        require(parameter_source.name == 'Parameters.hpp', f"{slug}: expected native parameter source")
        require('"' + bypass_key + '"' in parameter_source.read_text(encoding='utf-8'),
                f"{slug}: native parameter source lost bypass key")
        manifest_sources[slug] = str(source.relative_to(root))
    # Public snapshots do not include private planning originals. Validate the
    # actual runtime artwork instead; identity/parameter checks above are intact.
    resources = root / 'common/ui/resources'
    assets = read_json(resources / 'manifest.json')['assets']
    checked = 0
    require(isinstance(assets, dict) and bool(assets), 'Missing runtime asset entries')
    for item in assets.values():
        if item['kind'] != 'image':
            continue
        path = local_file(resources, item['path'])
        checked += 1
        require(hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256'],
                f"Runtime asset SHA256 mismatch: {item['path']}")
    require(checked == 10 and len(uids) == 20, 'Runtime asset/UID count changed')
    return {'manifestSources': manifest_sources, 'assetFiles': checked, 'uids': len(uids)}


def main():
    try:
        result = audit(Path(__file__).resolve().parents[1])
    except (ContractError, OSError, KeyError, TypeError) as error:
        print(f'FAIL contract audit: {error}', file=sys.stderr)
        return 1
    print('PASS 10 plugin manifests, 20 unique literal UIDs, stable bypass ID/key pairs,',
          result['assetFiles'], 'runtime-asset SHA256 checks')
    return 0


if __name__ == '__main__':
    sys.exit(main())
