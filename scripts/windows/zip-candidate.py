#!/usr/bin/env python3
"""Package a staged candidate with portable, forward-slash ZIP member names."""
import argparse
from pathlib import Path
import zipfile


def package_candidate(stage, output):
    stage = stage.resolve(strict=True)
    output = output.resolve()
    if not stage.is_dir() or stage == output or stage in output.parents:
        raise ValueError('Output ZIP must be outside the staged candidate directory')
    members = []
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            raise ValueError('Symlinks are not allowed in a candidate: '+str(path))
        if path.is_file():
            name = path.relative_to(stage.parent).as_posix()
            if '\\' in name or ':' in name:
                raise ValueError('Nonportable candidate filename: '+name)
            members.append((path, name))
    if not members:
        raise ValueError('Candidate directory is empty')
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for path, name in members:
            archive.write(path, arcname=name)
    print('Portable ZIP created: '+str(output))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    package_candidate(args.stage, args.output)
