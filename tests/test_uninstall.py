"""Tests for module uninstallation script (module/uninstall.sh)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UNINSTALL_SH = ROOT / "module" / "uninstall.sh"


class UninstallTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory(prefix="llc-uninstall-test-")
        self.root = Path(self.temp_dir.name)
        self.pkg = "com.ProjectMoon.LimbusCompany"

        # Game private data dirs
        for base in ["data/data", "data/user/0"]:
            app_dir = self.root / base / self.pkg
            (app_dir / "cache").mkdir(parents=True, exist_ok=True)
            (app_dir / "files/Lang/.llc-packs/releases").mkdir(parents=True, exist_ok=True)
            (app_dir / "files/Lang/LLC_zh-CN").mkdir(parents=True, exist_ok=True)
            (app_dir / "shared_prefs").mkdir(parents=True, exist_ok=True)

            # LLC created files
            (app_dir / "libllc_core.so").write_text("root-core")
            (app_dir / "cache/libllc_core.so").write_text("core")
            (app_dir / "files/libllc_core.so").write_text("files-core")
            (app_dir / "files/tmpchinesefont").write_text("font")
            (app_dir / "files/fonts").mkdir(parents=True, exist_ok=True)
            (app_dir / "files/font_config.json").write_text("{}")
            (app_dir / "files/language_config.json").write_text("{}")
            (app_dir / "files/static_overrides.json").write_text("{}")
            (app_dir / "files/title_overrides.json").write_text("{}")
            (app_dir / "files/custom_overrides.json").write_text("{}")
            (app_dir / "files/llc_reload.flag").write_text("1")
            (app_dir / "files/Lang/.llc-packs/state.json").write_text("{}")
            (app_dir / "files/Lang/static_overrides.json").write_text("{}")
            (app_dir / "files/Lang/title_overrides.json").write_text("{}")
            (app_dir / "files/Lang/custom_overrides.json").write_text("{}")

            # Official game files that MUST NOT be touched
            (app_dir / "shared_prefs/playerprefs.xml").write_text("<map/>")
            (app_dir / "files/game_save.bin").write_text("save")

        # External storage
        ext_files = self.root / "sdcard/Android/data" / self.pkg / "files"
        (ext_files / "fonts").mkdir(parents=True, exist_ok=True)
        (ext_files / "Lang/LLC_zh-CN").mkdir(parents=True, exist_ok=True)
        (ext_files / "Lang/LLC_zh-CN/StoryData.json").write_text("{}")
        (ext_files / "llc_status.json").write_text("{}")
        (ext_files / "fonts/SourceHanSans.bundle").write_text("official-font")
        (ext_files / "fonts/CustomUserFont.bundle").write_text("user-font")

        # Staging & module dirs
        staging = self.root / "data/local/tmp/llc"
        staging.mkdir(parents=True, exist_ok=True)
        (staging / "libllc_core.so").write_text("core")
        (staging / "tmpchinesefont").write_text("font")
        (self.root / "data/local/tmp/llc_font_123.tmp").write_text("tmp")
        (self.root / "data/local/tmp/llc_chunk_456.tmp").write_text("chunk")

        mod_dir = self.root / "data/adb/llc_zygisk"
        (mod_dir / "downloads").mkdir(parents=True, exist_ok=True)
        (mod_dir / "network.json").write_text("{}")

    def tearDown(self):
        self.temp_dir.cleanup()

    def run_uninstall(self):
        env = os.environ.copy()
        env["LLC_TEST_ROOT"] = str(self.root).replace("\\", "/")
        # Convert path to WSL if running through bash on Windows
        if os.name == "nt":
            # Convert Windows path to WSL /mnt path
            drive = self.root.drive.rstrip(":").lower()
            wsl_root = f"/mnt/{drive}{self.root.as_posix()[2:]}"
            wsl_script = f"/mnt/{UNINSTALL_SH.drive.rstrip(':').lower()}{UNINSTALL_SH.as_posix()[2:]}"
            cmd = ["wsl", "bash", "-c", f"LLC_TEST_ROOT='{wsl_root}' bash '{wsl_script}'"]
        else:
            cmd = ["sh", str(UNINSTALL_SH)]

        res = subprocess.run(cmd, env=env, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"uninstall.sh failed: {res.stderr}")

    def test_uninstall_removes_llc_files_and_preserves_game_and_external_fonts(self):
        self.run_uninstall()

        # 1. Verify game private LLC files are deleted
        for base in ["data/data", "data/user/0"]:
            app_dir = self.root / base / self.pkg
            self.assertFalse((app_dir / "libllc_core.so").exists())
            self.assertFalse((app_dir / "cache/libllc_core.so").exists())
            self.assertFalse((app_dir / "files/libllc_core.so").exists())
            self.assertFalse((app_dir / "files/tmpchinesefont").exists())
            self.assertFalse((app_dir / "files/fonts").exists())
            self.assertFalse((app_dir / "files/font_config.json").exists())
            self.assertFalse((app_dir / "files/language_config.json").exists())
            self.assertFalse((app_dir / "files/static_overrides.json").exists())
            self.assertFalse((app_dir / "files/title_overrides.json").exists())
            self.assertFalse((app_dir / "files/custom_overrides.json").exists())
            self.assertFalse((app_dir / "files/llc_reload.flag").exists())
            self.assertFalse((app_dir / "files/Lang").exists())

            # Verify official game files remain intact!
            self.assertTrue((app_dir / "shared_prefs/playerprefs.xml").exists())
            self.assertTrue((app_dir / "files/game_save.bin").exists())

        # 2. Verify external diagnostic file and legacy translations are removed
        ext_files = self.root / "sdcard/Android/data" / self.pkg / "files"
        self.assertFalse((ext_files / "llc_status.json").exists())
        self.assertFalse((ext_files / "Lang").exists())

        # 3. Verify external fonts directory is PRESERVED (Option A)
        self.assertTrue((ext_files / "fonts/SourceHanSans.bundle").exists())
        self.assertTrue((ext_files / "fonts/CustomUserFont.bundle").exists())

        # 4. Verify hidden staging and temp files are removed
        self.assertFalse((self.root / "data/local/tmp/llc").exists())
        self.assertFalse((self.root / "data/local/tmp/llc_font_123.tmp").exists())
        self.assertFalse((self.root / "data/local/tmp/llc_chunk_456.tmp").exists())

        # 5. Verify module state directory is removed
        self.assertFalse((self.root / "data/adb/llc_zygisk").exists())


if __name__ == "__main__":
    unittest.main()
