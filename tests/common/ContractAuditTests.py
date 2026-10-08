#!/usr/bin/env python3
"""Mutation regressions for the source-only audit; no build, GUI or product writes."""
from contextlib import contextmanager
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('contract_audit', ROOT / 'scripts/check-contracts.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ContractAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='just-contract-audit-')
        cls.root = Path(cls.temporary.name)
        files = {'scripts/check-contracts.py', 'common/vst3/plugin-identities.json',
                 'common/vst3/PluginIdentities.hpp', 'common/ui/resources/manifest.json'}
        for path in (ROOT / 'plugins').glob('*/manifest.json'):
            directory = path.parent
            manifest = json.loads(path.read_text())
            files.update(str((directory / name).relative_to(ROOT))
                         for name in ('manifest.json', 'Parameters.hpp', 'Module.cpp'))
            for key in ('parameterGolden', 'parameterProposal', 'parameterMap'):
                if key in manifest:
                    files.add(str((directory / manifest[key]).relative_to(ROOT)))
        assets = json.loads((ROOT / 'common/ui/resources/manifest.json').read_text())['assets']
        files.update('common/ui/resources/' + item['path'] for item in assets.values() if item['kind'] == 'image')
        for name in files:
            target = cls.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @contextmanager
    def changed(self, name, transform, json_data=True):
        path = self.root / name
        original = path.read_bytes()
        try:
            if json_data:
                data = json.loads(original)
                transform(data)
                path.write_text(json.dumps(data), encoding='utf-8')
            else:
                path.write_bytes(transform(original))
            yield
        finally:
            path.write_bytes(original)

    def rejects(self):
        with self.assertRaises(audit.ContractError):
            audit.audit(self.root)

    def test_actual_candidate_and_all_schema_adapters(self):
        result = audit.audit(ROOT)
        self.assertEqual((len(result['manifestSources']), result['uids'], result['assetFiles']), (10, 20, 10))
        for slug, filename in [('delay', 'parameter-golden.json'), ('compressor', 'parameter-proposal.json'),
                               ('distortion', 'parameter-map.json'), ('tremolo', 'manifest.json')]:
            self.assertEqual(result['manifestSources'][slug], f'plugins/{slug}/{filename}')
        self.assertEqual(audit.audit(self.root), result)

    def test_all_ten_bypass_keys_remain_checked(self):
        for slug, name in audit.audit(self.root)['manifestSources'].items():
            with self.subTest(slug=slug):
                def corrupt(document):
                    records = document.get('parameterSpecs', document.get('parameters'))
                    records[0]['stableKey'] = 'corrupted.bypass'
                    if 'key' in records[0]:
                        records[0]['key'] = 'corrupted.bypass'
                with self.changed(name, corrupt):
                    self.rejects()

    def test_missing_external_reference_fails(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.pop('parameterGolden')):
            self.rejects()

    def test_missing_referenced_file_fails(self):
        path = self.root / 'plugins/distortion/parameter-map.json'
        original = path.read_bytes()
        try:
            path.unlink()
            self.rejects()
        finally:
            path.write_bytes(original)

    def test_ambiguous_source_fails(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.update(parameterMap='parameter-golden.json')):
            self.rejects()

    def test_malformed_external_schema_fails(self):
        with self.changed('plugins/compressor/parameter-proposal.json', lambda m: m.pop('parameters')):
            self.rejects()

    def test_empty_inline_does_not_fall_back(self):
        with self.changed('plugins/gate/manifest.json', lambda m: m.update(parameterSpecs=[])):
            self.rejects()

    def test_external_reference_must_stay_with_its_module(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.update(parameterGolden='../distortion/parameter-map.json')):
            self.rejects()

    def test_wrong_bypass_id_fails(self):
        with self.changed('plugins/delay/parameter-golden.json', lambda m: m['parameters'][0].update(paramID=99)):
            self.rejects()

    def test_duplicate_parameter_id_fails(self):
        with self.changed('plugins/compressor/parameter-proposal.json', lambda m: m['parameters'][1].update(paramID=0)):
            self.rejects()

    def test_duplicate_parameter_key_fails(self):
        with self.changed('plugins/distortion/parameter-map.json', lambda m: m['parameters'][1].update(stableKey=m['parameters'][0]['stableKey'])):
            self.rejects()

    def test_conflicting_parameter_aliases_fail(self):
        for field, value in [('paramID', 8), ('key', 'wrong')]:
            with self.subTest(field=field), self.changed('plugins/tremolo/manifest.json', lambda m: m['parameterSpecs'][0].update({field: value})):
                self.rejects()

    def test_boolean_id_alias_is_not_a_numeric_id(self):
        with self.changed('plugins/tremolo/manifest.json', lambda m: m['parameterSpecs'][0].update(paramID=0, id=False)):
            self.rejects()

    def test_declared_count_remains_checked(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.update(parameterCount=44)):
            self.rejects()

    def test_registered_ids_remain_checked(self):
        with self.changed('plugins/compressor/manifest.json', lambda m: m['registeredHostIDs'].pop()):
            self.rejects()

    def test_duplicate_uid_fails(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m[0].update(controllerUID=m[0]['processorUID'])):
            self.rejects()

    def test_malformed_uid_fails(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m[0].update(processorUID=[1, 2, 3])):
            self.rejects()

    def test_cpp_uid_mismatch_fails(self):
        with self.changed('common/vst3/PluginIdentities.hpp', lambda b: b.replace(b'0x50524F43u', b'0x51524F43u'), False):
            self.rejects()

    def test_plugin_count_fails(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m.pop()):
            self.rejects()

    def test_duplicate_plugin_bypass_key_fails(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m[1].update(bypassKey=m[0]['bypassKey'])):
            self.rejects()

    def test_identity_bypass_id_fails(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m[0].update(bypassParamID=1)):
            self.rejects()

    def test_manifest_slug_fails(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.update(slug='eq')):
            self.rejects()

    def test_ordinary_simple_label_change_fails(self):
        with self.changed('plugins/eq/manifest.json', lambda m: m['simpleControlLabels'].reverse()):
            self.rejects()

    def test_every_approved_label_contract_remains_exact(self):
        for slug in ('limiter', 'gate', 'fake_stereo'):
            with self.subTest(slug=slug), self.changed(f'plugins/{slug}/manifest.json', lambda m: m['simpleControlLabels'].append('Unapproved')):
                self.rejects()

    def test_legacy_label_table_still_pinned_for_approved_variants(self):
        with self.changed('common/vst3/plugin-identities.json', lambda rows: next(m for m in rows if m['slug'] == 'limiter')['simpleControls'].reverse()):
            self.rejects()

    def test_legacy_label_count_still_checked(self):
        with self.changed('common/vst3/plugin-identities.json', lambda m: m[0].update(simpleControls=['Only'])):
            self.rejects()

    def test_native_simple_binding_drift_fails(self):
        with self.changed('plugins/limiter/Module.cpp', lambda b: b.replace(b'"Output Gain"', b'"Ceiling"'), False):
            self.rejects()

    def test_native_parameter_bypass_key_fails(self):
        with self.changed('plugins/compressor/Parameters.hpp', lambda b: b.replace(b'"compressor.bypass"', b'"wrong.bypass"'), False):
            self.rejects()

    def test_runtime_asset_count_fails(self):
        with self.changed('common/ui/resources/manifest.json', lambda m: m['assets'].pop('just.eq.icon')):
            self.rejects()

    def test_runtime_asset_path_must_stay_local(self):
        with self.changed('common/ui/resources/manifest.json', lambda m: m['assets']['just.eq.icon'].update(path='../outside.png')):
            self.rejects()

    def test_runtime_asset_hash_fails_with_same_size(self):
        with self.changed('common/ui/resources/icons/eq.png', lambda b: bytes([b[0] ^ 1]) + b[1:], False):
            self.rejects()

    def test_optimized_python_does_not_disable_checks(self):
        with self.changed('plugins/delay/manifest.json', lambda m: m.update(parameterCount=1)):
            result = subprocess.run([sys.executable, '-O', str(self.root / 'scripts/check-contracts.py')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('declared parameter count mismatch', result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
