#!/usr/bin/env python3
"""Verify downloaded preview assets without executing a Windows binary."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import zipfile

SLUGS = {'eq', 'reverb', 'delay', 'tremolo', 'compressor', 'limiter',
         'gate', 'flanger', 'fake_stereo', 'distortion'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pe_version(data):
    if data[:2] != b'MZ':
        raise ValueError('Not a PE file')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe+4] != b'PE\0\0':
        raise ValueError('Missing PE signature')
    machine = struct.unpack_from('<H', data, pe+4)[0]
    if machine != 0x8664 or struct.unpack_from('<H', data, pe+24)[0] != 0x20b:
        raise ValueError('Plugin is not AMD64 PE32+')
    key = 'VS_VERSION_INFO\0'.encode('utf-16le')
    offset = data.find(key)
    if offset < 6:
        raise ValueError('Version resource missing')
    begin = offset-6
    length, value_length, kind = struct.unpack_from('<HHH', data, begin)
    fixed = (offset+len(key)+3) & ~3
    if value_length != 52 or fixed+52 > begin+length:
        raise ValueError('Invalid version resource')
    info = struct.unpack_from('<13I', data, fixed)
    if info[0] != 0xfeef04bd or info[2:6] != (1, 0, 1, 0):
        raise ValueError('Expected fixed file/product version 0.1.0.0')
    strings = {}

    def visit(pos, end):
        if pos+6 > end:
            return
        size, count, text = struct.unpack_from('<HHH', data, pos)
        if size < 6 or pos+size > end:
            raise ValueError('Malformed version resource node')
        cursor = pos+6
        key_start = cursor
        while cursor+2 <= pos+size and data[cursor:cursor+2] != b'\0\0':
            cursor += 2
        name = data[key_start:cursor].decode('utf-16le')
        cursor = (cursor+2+3) & ~3
        value_bytes = count*2 if text else count
        if cursor+value_bytes > pos+size:
            raise ValueError('Version resource value overflow')
        if text and count:
            strings[name] = data[cursor:cursor+value_bytes].decode('utf-16le').rstrip('\0')
        cursor = (cursor+value_bytes+3) & ~3
        while cursor+6 <= pos+size:
            child_size = struct.unpack_from('<H', data, cursor)[0]
            if not child_size:
                break
            visit(cursor, pos+size)
            cursor = (cursor+child_size+3) & ~3

    visit(begin, begin+length)
    for name, expected in [('FileVersion', '0.1.0'), ('ProductVersion', '0.1.0'),
                           ('CompanyName', 'Yee Huang')]:
        if strings.get(name) != expected:
            raise ValueError('Version string mismatch: '+name)
    return {'machine': 'AMD64', 'format': 'PE32+', 'fileVersion': strings['FileVersion'],
            'productVersion': strings['ProductVersion'], 'vendor': strings['CompanyName']}


def verify(directory):
    sums = directory/'SHA256SUMS.txt'
    verified_files = []
    for line in sums.read_text(encoding='utf-8-sig').splitlines():
        if not line.strip():
            continue
        digest, name = line.split(None, 1)
        name = name.lstrip('*')
        if Path(name).name != name or '/' in name or '\\' in name or len(digest) != 64:
            raise ValueError('Invalid checksum filename or digest')
        if any(item['name'] == name for item in verified_files):
            raise ValueError('Duplicate checksum filename')
        actual = sha((directory/name).read_bytes())
        if actual.lower() != digest.lower():
            raise ValueError('Download hash mismatch: '+name)
        verified_files.append({'name': name, 'sha256': actual, 'bytes': (directory/name).stat().st_size})
    manifest = json.loads((directory/'preview-manifest.json').read_text(encoding='utf-8-sig'))
    covered = {entry['name']: entry for entry in verified_files}
    if 'preview-manifest.json' not in covered:
        raise ValueError('Release manifest is not covered by checksums')
    for kind in ['installer', 'portable', 'source', 'evidence']:
        record = manifest[kind]
        actual = covered.get(record['file_name'])
        if actual is None or actual['sha256'] != record['sha256'] or actual['bytes'] != record['bytes']:
            raise ValueError('Manifest asset mismatch: '+kind)
    archive = directory/manifest['portable']['file_name']
    plugins = []
    with zipfile.ZipFile(archive) as package:
        # ZipInfo.filename normalizes backslashes on Windows. Check the raw
        # archive spelling too, so the native and Linux release gates agree.
        names = [entry.orig_filename for entry in package.infolist()]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate ZIP members')
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or '\\' in name:
                raise ValueError('Unsafe ZIP member: '+name)
        candidates = [n for n in names if n.endswith('/candidate-manifest.json')]
        if len(candidates) != 1:
            raise ValueError('Expected exactly one candidate manifest')
        base = candidates[0].rsplit('/', 1)[0]+'/'
        candidate = json.loads(package.read(candidates[0]).decode('utf-8-sig'))
        if len(candidate['plugins']) != 10 or {p['slug'] for p in candidate['plugins']} != SLUGS:
            raise ValueError('Expected all ten plugin identities')
        files = {n for n in names if n.startswith(base+'VST3/') and not n.endswith('/')}
        expected_files = {base+'VST3/'+n for n in candidate['hashes']}
        if files != expected_files:
            raise ValueError('Unexpected or missing VST3 files')
        for path, digest in candidate['hashes'].items():
            if sha(package.read(base+'VST3/'+path)) != digest:
                raise ValueError('VST3 asset mismatch: '+path)
        for plugin in candidate['plugins']:
            binary = package.read(base+plugin['binary'])
            digest = sha(binary)
            if digest != plugin['sha256']:
                raise ValueError('Plugin binary mismatch: '+plugin['slug'])
            plugins.append(dict(slug=plugin['slug'], sha256=digest, **pe_version(binary)))
    return {'result': 'PASS_DOWNLOADED_BYTES', 'source_commit': manifest['source_commit'],
            'files': verified_files, 'plugins': plugins,
            'windows_binaries_executed_by_this_check': False,
            'user_machine_reaper': 'NOT_RUN'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = verify(args.directory.resolve())
    text = json.dumps(result, indent=2)+'\n'
    if args.report:
        args.report.write_text(text, encoding='utf-8')
    print(text)
