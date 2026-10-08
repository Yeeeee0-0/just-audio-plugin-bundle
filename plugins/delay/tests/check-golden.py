#!/usr/bin/env python3
import json, subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
golden = json.loads((root / 'parameter-golden.json').read_text())['parameters']
lines = subprocess.check_output([sys.argv[1], '--registry'], text=True).splitlines()
assert len(lines) == len(golden) == 45
for line, expected in zip(lines, golden):
    id_, key, low, high, init, mapping, steps, unit, modes, transition, smoothing, enum = line.split('|')
    assert (int(id_), key, mapping, int(steps), unit, modes, transition) == (expected['paramID'], expected['stableKey'], expected['mapping'], expected['stepCount'], expected['unit'], expected['applicableModes'], expected['transition']), key
    for actual, field in zip((low, high, init, smoothing), ('min', 'max', 'default', 'smoothingMs')):
        assert abs(float(actual) - expected[field]) < 1e-12, (key, field)
    assert (enum.split(',') if enum else None) == expected['enumLabels'], key
print('PASS explicit golden: 45 compiled fields and all enum ordinals match reviewed first allocation')
