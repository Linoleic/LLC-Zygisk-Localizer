"""Validate release inputs and packages; keep Release notes identical to CHANGELOG."""
import argparse
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile


def properties(text):
    return dict(line.split('=', 1) for line in text.splitlines()
                if '=' in line and not line.startswith('#'))


def release_notes(changelog, version):
    headings = list(re.finditer(r'^## \[([^\]]+)\][^\n]*$', changelog, re.M))
    matches = [i for i, h in enumerate(headings) if h[1] == version]
    if len(matches) != 1:
        raise ValueError(f'Expected exactly one CHANGELOG section for {version}')
    i = matches[0]
    heading = headings[i]
    if heading[0] != f'## [{version}]':
        raise ValueError('Release heading must not contain a date or suffix')
    end = headings[i + 1].start() if i + 1 < len(headings) else len(changelog)
    section = changelog[heading.start():end]
    section = re.split(r'^\[[^\]]+\]: ', section, maxsplit=1, flags=re.M)[0]
    categories = re.findall(r'^### (.+)$', section, re.M)
    if not categories or any(x not in {'新增', '变更', '弃用', '移除', '修复', '安全'} for x in categories):
        raise ValueError('Use Chinese Keep a Changelog categories')
    if not re.search(r'^- \S', section, re.M):
        raise ValueError('Release section is empty')
    return section.rstrip() + '\n'


def prepare(root, tag, output):
    prop = properties((root / 'module/module.prop').read_text(encoding='utf-8'))
    version = prop['version']
    if not re.fullmatch(r'v\d+\.\d+\.\d+', version) or not re.fullmatch(r'[1-9]\d*', prop['versionCode']):
        raise ValueError('Invalid module version or versionCode')
    if tag and tag != version:
        raise ValueError(f'Tag {tag} does not match module version {version}')
    notes = release_notes((root / 'CHANGELOG.md').read_text(encoding='utf-8'), version)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(notes.encode('utf-8'))
    return prop


def verify_package(package, prop, revision):
    with zipfile.ZipFile(package) as z:
        names = z.namelist()
        if len({name.casefold() for name in names}) != len(names):
            raise ValueError('Duplicate ZIP entries')
        for name in names:
            path = PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
                raise ValueError(f'Unsafe ZIP path: {name}')
        if z.testzip():
            raise ValueError('ZIP CRC failure')
        packed = properties(z.read('module.prop').decode('utf-8'))
        if (packed['versionCode'] != prop['versionCode'] or
                not re.fullmatch(re.escape(prop['version']) + r'-r[1-9]\d*', packed['version'])):
            raise ValueError('Package version mismatch or dirty build')
        if package.name != f"LLC-Zygisk-Localizer-{packed['version']}.zip":
            raise ValueError('Unexpected package filename')
        for name in ['zygisk/arm64-v8a.so', 'libllc_core.so', 'bin/llc-manager-native']:
            data = z.read(name)
            if data[:6] != b'\x7fELF\x02\x01' or struct.unpack_from('<H', data, 18)[0] != 183:
                raise ValueError(f'Expected arm64 ELF: {name}')
            if name != 'zygisk/arm64-v8a.so' and revision.encode() not in data:
                raise ValueError(f'Missing build revision: {name}')
        import io
        with zipfile.ZipFile(io.BytesIO(z.read('bin/llc-https.jar'))) as jar:
            if not jar.read('classes.dex').startswith(b'dex\n'):
                raise ValueError('Missing HTTPS helper DEX')
        for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/nlohmann-json-LICENSE.MIT',
                     'licenses/zygisk.hpp', 'webroot/index.html', 'META-INF/com/google/android/update-binary']:
            if not z.read(name):
                raise ValueError(f'Empty required file: {name}')
        for name in names:
            if name.endswith('.sh') or name == 'bin/llc-manager':
                if b'\r' in z.read(name):
                    raise ValueError(f'Shell script must use LF: {name}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', default='')
    parser.add_argument('--package', type=Path)
    parser.add_argument('--revision', default='')
    parser.add_argument('--output', type=Path, default=Path('dist/release-notes.md'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    prop = prepare(root, args.tag, args.output)
    if args.package:
        if not re.fullmatch(r'[a-f0-9]{40}', args.revision):
            parser.error('--package requires a full --revision')
        verify_package(args.package, prop, args.revision)
    print(f"Validated {prop['version']} / {prop['versionCode']}")


if __name__ == '__main__':
    main()
