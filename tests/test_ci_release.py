import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ci_release import prepare, release_notes, verify_package


class ReleaseTest(unittest.TestCase):
    def test_notes_preserve_exact_section_and_exclude_next_version_and_links(self):
        section = '## [v1.2.3]\n\n### 新增\n\n- 中文内容。\n'
        changelog = '# 更新日志\n\n## [Unreleased]\n\n' + section + '\n## [v1.2.2]\n\n### 修复\n\n- Older\n'
        self.assertEqual(release_notes(changelog, 'v1.2.3'), section)
        self.assertEqual(release_notes(section + '\n[v1.2.3]: https://example.com\n', 'v1.2.3'), section)

    def test_reject_missing_duplicate_dated_empty_and_nonstandard_sections(self):
        for source in ['', '## [v1.2.3]\n\n## [v1.2.3]\n',
                       '## [v1.2.3] - 2026-01-01\n### 修复\n- item\n',
                       '## [v1.2.3]\n### 修复\n', '## [v1.2.3]\n### 验证说明\n- item\n']:
            with self.subTest(source=source), self.assertRaises(ValueError):
                release_notes(source, 'v1.2.3')

    def test_tag_must_match_source_version(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'module').mkdir()
            (root / 'module/module.prop').write_text('version=v1.2.3\nversionCode=123\n')
            (root / 'CHANGELOG.md').write_text('## [v1.2.3]\n\n### 修复\n\n- 内容\n', encoding='utf-8')
            output = root / 'dist/notes.md'
            with self.assertRaises(ValueError):
                prepare(root, 'v1.2.4', output)
            self.assertFalse(output.exists())
            prepare(root, 'v1.2.3', output)
            self.assertIn('内容', output.read_text(encoding='utf-8'))

    def package(self, path, *, extra=None, version='v1.2.3-r5', architecture=183):
        revision = 'a' * 40
        elf = bytearray(64)
        elf[:6] = b'\x7fELF\x02\x01'
        struct.pack_into('<H', elf, 18, architecture)
        jar = io.BytesIO()
        with zipfile.ZipFile(jar, 'w') as z:
            z.writestr('classes.dex', b'dex\n035\0')
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('module.prop', f'version={version}\nversionCode=123\n')
            for name in ['zygisk/arm64-v8a.so', 'libllc_core.so', 'bin/llc-manager-native']:
                z.writestr(name, elf + revision.encode())
            z.writestr('bin/llc-https.jar', jar.getvalue())
            for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/nlohmann-json-LICENSE.MIT',
                         'licenses/zygisk.hpp', 'webroot/index.html', 'META-INF/com/google/android/update-binary']:
                z.writestr(name, 'fixture')
            if extra:
                z.writestr(*extra)

    def test_package_rejects_dirty_wrong_revision_architecture_and_unsafe_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'LLC-Zygisk-Localizer-v1.2.3-r5.zip'
            prop = {'version': 'v1.2.3', 'versionCode': '123'}
            self.package(path)
            verify_package(path, prop, 'a' * 40)
            with self.assertRaises(ValueError):
                verify_package(path, prop, 'b' * 40)
            for options in [{'version': 'v1.2.3-r5-local'}, {'architecture': 62},
                            {'extra': ('../escape', 'bad')}, {'extra': ('MODULE.PROP', 'duplicate')},
                            {'extra': ('service.sh', '#!/bin/sh\r\n')}]:
                with self.subTest(options=options):
                    self.package(path, **options)
                    with self.assertRaises(ValueError):
                        verify_package(path, prop, 'a' * 40)


if __name__ == '__main__':
    unittest.main()
