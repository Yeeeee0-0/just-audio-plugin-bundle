#!/usr/bin/env python3
"""Compare entire generated baseline/new streams without committing audio binaries."""
import hashlib
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
base = '0be3ed7f65ac84bdb58abf5ed425f3f712efcb8b'
source = root / 'plugins/fake_stereo/tests/LegacyGoldenTests.cpp'
with tempfile.TemporaryDirectory(prefix='just-wider-golden-') as path:
    temp = pathlib.Path(path)
    frozen = temp / 'plugins/fake_stereo/Dsp.hpp'
    frozen.parent.mkdir(parents=True)
    frozen.write_bytes(subprocess.check_output(['git', 'show', base + ':plugins/fake_stereo/Dsp.hpp'], cwd=root))
    for name, includes in [('old', [temp, root]), ('new', [root])]:
        command = ['clang++', '-std=c++17', '-O2']
        for include in includes:
            command += ['-I', str(include)]
        subprocess.run(command + [str(source), '-o', str(temp / name)], check=True)
    golden = temp / 'baseline.bin'
    subprocess.run([str(temp / 'old'), '--record', str(golden)], check=True)
    print('baselineCommit=' + base, flush=True)
    print('baselineAudioSHA256=' + hashlib.sha256(golden.read_bytes()).hexdigest(), flush=True)
    subprocess.run([str(temp / 'new'), '--verify', str(golden)], check=True)
